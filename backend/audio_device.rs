use super::{
    detect_pitch, AudioEngine, AudioEngineControl, AudioEngineProcessor, AudioError, PitchEstimate,
};
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use cpal::{
    BuildStreamError, Device, SampleFormat, Stream, StreamConfig, StreamInstant,
    SupportedStreamConfig,
};
use std::cmp::Reverse;
use std::fmt;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicU8, AtomicUsize, Ordering};
use std::sync::Arc;
use std::thread::{self, JoinHandle};
use std::time::Duration;
#[cfg(test)]
use std::time::Instant;

const MAX_CHANNELS: usize = 8;
const MAX_CALLBACK_FRAMES: usize = 16_384;
const MAX_CALLBACK_SAMPLES: usize = MAX_CHANNELS * MAX_CALLBACK_FRAMES;
const RESAMPLER_TAPS: usize = 32;
const RESAMPLER_PHASES: usize = 1024;
const PITCH_ANALYSIS_WINDOW_FRAMES: usize = 4096;
const PITCH_ANALYSIS_INTERVAL: Duration = Duration::from_millis(250);

const STATUS_PLAYING: u8 = 0;
const STATUS_FINISHED: u8 = 1;
const STATUS_STOPPED: u8 = 2;
const STATUS_DEVICE_ERROR: u8 = 3;
const STATUS_CALLBACK_ERROR: u8 = 4;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OutputDeviceInfo {
    pub index: usize,
    pub name: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioPlaybackStatus {
    Playing,
    Finished,
    Stopped,
    DeviceError,
    CallbackError,
}

#[derive(Debug)]
pub enum AudioDeviceError {
    NoOutputDevice,
    OutputDeviceUnavailable(usize),
    OutputDeviceEnumeration(String),
    OutputConfiguration(String),
    UnsupportedSampleFormat(String),
    InvalidAudio(String),
    Engine(AudioError),
    Stream(String),
    PitchAnalysisThread(String),
    PitchAnalysisThreadPanicked,
}

impl fmt::Display for AudioDeviceError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::NoOutputDevice => formatter.write_str("no audio output device is available"),
            Self::OutputDeviceUnavailable(index) => {
                write!(
                    formatter,
                    "audio output device {index} is no longer available"
                )
            }
            Self::OutputDeviceEnumeration(error) => {
                write!(
                    formatter,
                    "could not enumerate audio output devices: {error}"
                )
            }
            Self::OutputConfiguration(error) => {
                write!(formatter, "could not configure audio output: {error}")
            }
            Self::UnsupportedSampleFormat(format) => {
                write!(
                    formatter,
                    "unsupported audio output sample format: {format}"
                )
            }
            Self::InvalidAudio(error) => formatter.write_str(error),
            Self::Engine(error) => write!(formatter, "audio engine error: {error}"),
            Self::Stream(error) => write!(formatter, "audio stream error: {error}"),
            Self::PitchAnalysisThread(error) => {
                write!(formatter, "could not start pitch analysis: {error}")
            }
            Self::PitchAnalysisThreadPanicked => {
                formatter.write_str("pitch analysis thread panicked")
            }
        }
    }
}

impl std::error::Error for AudioDeviceError {}

impl From<AudioError> for AudioDeviceError {
    fn from(error: AudioError) -> Self {
        Self::Engine(error)
    }
}

pub struct AudioPlayback {
    stream: Stream,
    control: AudioEngineControl,
    shared: Arc<SharedPlaybackState>,
    stream_running: bool,
    pitch_analysis: Option<PitchAnalysisWorker>,
}

impl AudioPlayback {
    pub fn start(
        source_samples: Vec<f32>,
        source_sample_rate: u32,
        source_channels: usize,
        output_device: Option<OutputDeviceInfo>,
        notch_center_hz: f64,
        notch_bandwidth_hz: f64,
    ) -> Result<Self, AudioDeviceError> {
        if source_sample_rate == 0
            || !(1..=MAX_CHANNELS).contains(&source_channels)
            || source_samples.is_empty()
            || !source_samples.len().is_multiple_of(source_channels)
            || source_samples.iter().any(|sample| !sample.is_finite())
        {
            return Err(AudioDeviceError::InvalidAudio(
                "WAV samples must be finite, non-empty, and have complete frames".to_owned(),
            ));
        }

        let host = cpal::default_host();
        let device = get_output_device(&host, output_device.as_ref())?;
        let supported_config = choose_output_config(&device, source_sample_rate, source_channels)?;
        let config = supported_config.config();
        let output_channels = usize::from(config.channels);
        let output_rate = config.sample_rate.0;
        let output_samples: Arc<[f32]> = convert_audio(
            &source_samples,
            source_sample_rate,
            source_channels,
            output_rate,
            output_channels,
        )?
        .into();

        let mut engine = AudioEngine::new(output_rate, output_channels)?;
        engine.set_notch(0, notch_center_hz, notch_bandwidth_hz)?;
        let (processor, control) = engine.split_for_realtime();
        let shared = Arc::new(SharedPlaybackState::new());
        let renderer = PlaybackRenderer::new(
            processor,
            Arc::clone(&output_samples),
            output_rate,
            output_channels,
            Arc::clone(&shared),
        );
        let stream = build_output_stream(
            &device,
            &config,
            supported_config.sample_format(),
            renderer,
            Arc::clone(&shared),
        )?;

        let mut pitch_analysis = PitchAnalysisWorker::start(
            output_samples,
            output_rate,
            output_channels,
            Arc::clone(&shared),
        )?;
        shared.status.store(STATUS_PLAYING, Ordering::Release);
        if let Err(error) = stream.play() {
            shared.status.store(STATUS_STOPPED, Ordering::Release);
            pitch_analysis.stop()?;
            return Err(AudioDeviceError::Stream(error.to_string()));
        }

        Ok(Self {
            stream,
            control,
            shared,
            stream_running: true,
            pitch_analysis: Some(pitch_analysis),
        })
    }

    pub fn set_notch(
        &mut self,
        index: usize,
        center_frequency_hz: f64,
        bandwidth_hz: f64,
    ) -> Result<(), AudioDeviceError> {
        self.control
            .set_notch(index, center_frequency_hz, bandwidth_hz)
            .map_err(AudioDeviceError::Engine)
    }

    pub fn status(&self) -> AudioPlaybackStatus {
        match self.shared.status.load(Ordering::Acquire) {
            STATUS_FINISHED => AudioPlaybackStatus::Finished,
            STATUS_STOPPED => AudioPlaybackStatus::Stopped,
            STATUS_DEVICE_ERROR => AudioPlaybackStatus::DeviceError,
            STATUS_CALLBACK_ERROR => AudioPlaybackStatus::CallbackError,
            _ => AudioPlaybackStatus::Playing,
        }
    }

    pub fn callback_cadence_misses(&self) -> u64 {
        self.shared.callback_cadence_misses.load(Ordering::Relaxed)
    }

    pub fn pitch_estimate(&self) -> Option<PitchEstimate> {
        self.shared.pitch_estimate()
    }

    pub fn pitch_analysis_failed(&self) -> bool {
        self.shared.pitch_analysis_failed.load(Ordering::Acquire)
    }

    pub fn poll(&mut self) -> Result<AudioPlaybackStatus, AudioDeviceError> {
        let status = self.status();
        if self.stream_running && status != AudioPlaybackStatus::Playing {
            self.stream_running = false;
            let pause_result = self
                .stream
                .pause()
                .map_err(|error| AudioDeviceError::Stream(error.to_string()));
            let analysis_result = self.stop_pitch_analysis();
            pause_result?;
            analysis_result?;
        }
        Ok(status)
    }

    pub fn stop(&mut self) -> Result<(), AudioDeviceError> {
        let _ = self.shared.status.compare_exchange(
            STATUS_PLAYING,
            STATUS_STOPPED,
            Ordering::AcqRel,
            Ordering::Acquire,
        );
        let pause_result = if self.stream_running {
            self.stream_running = false;
            self.stream
                .pause()
                .map_err(|error| AudioDeviceError::Stream(error.to_string()))
        } else {
            Ok(())
        };
        let analysis_result = self.stop_pitch_analysis();
        pause_result?;
        analysis_result
    }

    fn stop_pitch_analysis(&mut self) -> Result<(), AudioDeviceError> {
        match self.pitch_analysis.take() {
            Some(mut analysis) => analysis.stop(),
            None => Ok(()),
        }
    }
}

#[cfg(target_os = "windows")]
pub type WindowsAudioPlayback = AudioPlayback;

impl Drop for AudioPlayback {
    fn drop(&mut self) {
        self.shared.status.store(STATUS_STOPPED, Ordering::Release);
        if let Some(mut analysis) = self.pitch_analysis.take() {
            let _ = analysis.stop();
        }
    }
}

pub fn enumerate_output_devices() -> Result<Vec<OutputDeviceInfo>, AudioDeviceError> {
    let host = cpal::default_host();
    let devices = host
        .output_devices()
        .map_err(|error| AudioDeviceError::OutputDeviceEnumeration(error.to_string()))?;

    devices
        .enumerate()
        .map(|(index, device)| {
            let name = device
                .name()
                .map_err(|error| AudioDeviceError::OutputDeviceEnumeration(error.to_string()))?;
            Ok(OutputDeviceInfo { index, name })
        })
        .collect()
}

fn get_output_device(
    host: &cpal::Host,
    selected: Option<&OutputDeviceInfo>,
) -> Result<Device, AudioDeviceError> {
    match selected {
        None => host
            .default_output_device()
            .ok_or(AudioDeviceError::NoOutputDevice),
        Some(selected) => {
            let device = host
                .output_devices()
                .map_err(|error| AudioDeviceError::OutputDeviceEnumeration(error.to_string()))?
                .nth(selected.index)
                .ok_or(AudioDeviceError::OutputDeviceUnavailable(selected.index))?;
            let current_name = device
                .name()
                .map_err(|error| AudioDeviceError::OutputDeviceEnumeration(error.to_string()))?;
            if current_name != selected.name {
                return Err(AudioDeviceError::OutputDeviceUnavailable(selected.index));
            }
            Ok(device)
        }
    }
}

fn choose_output_config(
    device: &Device,
    source_rate: u32,
    source_channels: usize,
) -> Result<SupportedStreamConfig, AudioDeviceError> {
    let ranges = device
        .supported_output_configs()
        .map_err(|error| AudioDeviceError::OutputConfiguration(error.to_string()))?;
    let source_channels = u16::try_from(source_channels).map_err(|_| {
        AudioDeviceError::OutputConfiguration("invalid source channel count".to_owned())
    })?;

    let selected = ranges
        .filter(|range| (1..=MAX_CHANNELS as u16).contains(&range.channels()))
        .map(|range| {
            let rate = source_rate.clamp(range.min_sample_rate().0, range.max_sample_rate().0);
            range.with_sample_rate(cpal::SampleRate(rate))
        })
        .max_by_key(|config| {
            (
                sample_format_preference(config.sample_format()),
                Reverse(config.sample_rate().0.abs_diff(source_rate)),
                Reverse(config.channels().abs_diff(source_channels)),
            )
        });

    if let Some(config) = selected {
        return Ok(config);
    }

    let default = device
        .default_output_config()
        .map_err(|error| AudioDeviceError::OutputConfiguration(error.to_string()))?;
    if !(1..=MAX_CHANNELS as u16).contains(&default.channels()) {
        return Err(AudioDeviceError::OutputConfiguration(format!(
            "device default has {} channels; the engine supports 1 through {MAX_CHANNELS}",
            default.channels()
        )));
    }
    Ok(default)
}

fn sample_format_preference(format: SampleFormat) -> u8 {
    match format {
        SampleFormat::F32 => 100,
        SampleFormat::F64 => 90,
        SampleFormat::I16 => 80,
        SampleFormat::I32 => 70,
        SampleFormat::I8 => 60,
        SampleFormat::U16 => 50,
        SampleFormat::U32 => 40,
        SampleFormat::U8 => 30,
        SampleFormat::I64 | SampleFormat::U64 => 20,
        _ => 0,
    }
}

fn convert_audio(
    input: &[f32],
    input_rate: u32,
    input_channels: usize,
    output_rate: u32,
    output_channels: usize,
) -> Result<Vec<f32>, AudioDeviceError> {
    if input_rate == 0
        || output_rate == 0
        || !(1..=MAX_CHANNELS).contains(&input_channels)
        || !(1..=MAX_CHANNELS).contains(&output_channels)
        || !input.len().is_multiple_of(input_channels)
    {
        return Err(AudioDeviceError::InvalidAudio(
            "invalid sample rate, channel count, or frame alignment".to_owned(),
        ));
    }

    let input_frames = input.len() / input_channels;
    if input_frames == 0 {
        return Err(AudioDeviceError::InvalidAudio(
            "the WAV contains no audio frames".to_owned(),
        ));
    }
    let output_frames = (input_frames as u128 * u128::from(output_rate) + u128::from(input_rate)
        - 1)
        / u128::from(input_rate);
    let output_frames = usize::try_from(output_frames)
        .map_err(|_| AudioDeviceError::InvalidAudio("audio is too large to convert".to_owned()))?;
    let output_len = output_frames.checked_mul(output_channels).ok_or_else(|| {
        AudioDeviceError::InvalidAudio("audio is too large to convert".to_owned())
    })?;
    let mut output = Vec::new();
    output
        .try_reserve_exact(output_len)
        .map_err(|error| AudioDeviceError::InvalidAudio(error.to_string()))?;
    output.resize(output_len, 0.0);

    if input_rate == output_rate {
        for frame in 0..input_frames {
            for channel in 0..output_channels {
                output[frame * output_channels + channel] =
                    mix_sample(input, frame, input_channels, output_channels, channel);
            }
        }
        return Ok(output);
    }

    let kernel = build_resampler_kernel(input_rate, output_rate)?;
    let last_input_frame = isize::try_from(input_frames - 1)
        .map_err(|_| AudioDeviceError::InvalidAudio("audio is too large to convert".to_owned()))?;
    for output_frame in 0..output_frames {
        let source_position = output_frame as u128 * u128::from(input_rate);
        let center_frame = (source_position / u128::from(output_rate)) as isize;
        let phase = ((source_position % u128::from(output_rate) * RESAMPLER_PHASES as u128)
            / u128::from(output_rate)) as usize;

        for output_channel in 0..output_channels {
            let mut sample = 0.0_f64;
            let kernel_start = phase * RESAMPLER_TAPS;
            for tap in 0..RESAMPLER_TAPS {
                let offset = tap as isize - (RESAMPLER_TAPS as isize / 2 - 1);
                let source_frame = center_frame
                    .saturating_add(offset)
                    .clamp(0, last_input_frame) as usize;
                let source_sample = mix_sample(
                    input,
                    source_frame,
                    input_channels,
                    output_channels,
                    output_channel,
                );
                sample += f64::from(source_sample) * kernel[kernel_start + tap];
            }
            output[output_frame * output_channels + output_channel] = sample as f32;
        }
    }

    Ok(output)
}

fn build_resampler_kernel(input_rate: u32, output_rate: u32) -> Result<Vec<f64>, AudioDeviceError> {
    let kernel_len = RESAMPLER_PHASES
        .checked_mul(RESAMPLER_TAPS)
        .ok_or_else(|| AudioDeviceError::InvalidAudio("resampler kernel overflow".to_owned()))?;
    let mut kernel = Vec::new();
    kernel
        .try_reserve_exact(kernel_len)
        .map_err(|error| AudioDeviceError::InvalidAudio(error.to_string()))?;
    kernel.resize(kernel_len, 0.0);

    let cutoff = 0.5 * (f64::from(output_rate) / f64::from(input_rate)).min(1.0) * 0.94;
    let pi = std::f64::consts::PI;
    let half_taps = RESAMPLER_TAPS as f64 / 2.0;

    for phase in 0..RESAMPLER_PHASES {
        let fraction = phase as f64 / RESAMPLER_PHASES as f64;
        let kernel_start = phase * RESAMPLER_TAPS;
        let mut coefficient_sum = 0.0;

        for tap in 0..RESAMPLER_TAPS {
            let offset = tap as f64 - (RESAMPLER_TAPS as f64 / 2.0 - 1.0) - fraction;
            let normalized_offset = 2.0 * cutoff * offset;
            let sinc = if normalized_offset.abs() < f64::EPSILON {
                1.0
            } else {
                (pi * normalized_offset).sin() / (pi * normalized_offset)
            };
            let window_position = offset / half_taps;
            let window = if window_position.abs() <= 1.0 {
                0.42 + 0.5 * (pi * window_position).cos()
                    + 0.08 * (2.0 * pi * window_position).cos()
            } else {
                0.0
            };
            let coefficient = 2.0 * cutoff * sinc * window;
            kernel[kernel_start + tap] = coefficient;
            coefficient_sum += coefficient;
        }

        if !coefficient_sum.is_finite() || coefficient_sum.abs() < f64::EPSILON {
            return Err(AudioDeviceError::InvalidAudio(
                "could not construct a stable resampler kernel".to_owned(),
            ));
        }
        for coefficient in &mut kernel[kernel_start..kernel_start + RESAMPLER_TAPS] {
            *coefficient /= coefficient_sum;
        }
    }

    Ok(kernel)
}

fn mix_sample(
    input: &[f32],
    frame: usize,
    input_channels: usize,
    output_channels: usize,
    output_channel: usize,
) -> f32 {
    let frame_start = frame * input_channels;
    if input_channels == output_channels {
        return input[frame_start + output_channel];
    }
    if input_channels == 1 {
        return input[frame_start];
    }
    if output_channels == 1 {
        let sum = input[frame_start..frame_start + input_channels]
            .iter()
            .copied()
            .sum::<f32>();
        return sum / input_channels as f32;
    }
    if output_channel < input_channels {
        input[frame_start + output_channel]
    } else {
        0.0
    }
}

struct SharedPlaybackState {
    status: AtomicU8,
    callback_cadence_misses: AtomicU64,
    playhead_frames: AtomicUsize,
    pitch_result: AtomicU64,
    pitch_ready: AtomicBool,
    pitch_analysis_failed: AtomicBool,
}

impl SharedPlaybackState {
    fn new() -> Self {
        Self {
            status: AtomicU8::new(STATUS_STOPPED),
            callback_cadence_misses: AtomicU64::new(0),
            playhead_frames: AtomicUsize::new(0),
            pitch_result: AtomicU64::new(0),
            pitch_ready: AtomicBool::new(false),
            pitch_analysis_failed: AtomicBool::new(false),
        }
    }

    fn set_pitch_estimate(&self, estimate: PitchEstimate) {
        let frequency_hz = estimate.frequency_hz.unwrap_or(0.0) as f32;
        let confidence = estimate.confidence as f32;
        let packed = (u64::from(frequency_hz.to_bits()) << 32) | u64::from(confidence.to_bits());
        self.pitch_result.store(packed, Ordering::Release);
        self.pitch_ready.store(true, Ordering::Release);
    }

    fn pitch_estimate(&self) -> Option<PitchEstimate> {
        if !self.pitch_ready.load(Ordering::Acquire) {
            return None;
        }

        let packed = self.pitch_result.load(Ordering::Acquire);
        let frequency_hz = f32::from_bits((packed >> 32) as u32) as f64;
        let confidence = f32::from_bits(packed as u32) as f64;
        Some(PitchEstimate {
            frequency_hz: (frequency_hz > 0.0).then_some(frequency_hz),
            confidence,
        })
    }

    fn fail_callback(&self) {
        let _ = self.status.compare_exchange(
            STATUS_PLAYING,
            STATUS_CALLBACK_ERROR,
            Ordering::AcqRel,
            Ordering::Acquire,
        );
    }

    fn fail_device(&self) {
        let _ = self.status.compare_exchange(
            STATUS_PLAYING,
            STATUS_DEVICE_ERROR,
            Ordering::AcqRel,
            Ordering::Acquire,
        );
    }

    fn finish(&self) {
        let _ = self.status.compare_exchange(
            STATUS_PLAYING,
            STATUS_FINISHED,
            Ordering::AcqRel,
            Ordering::Acquire,
        );
    }
}

struct PitchAnalysisWorker {
    stop: Arc<AtomicBool>,
    thread: Option<JoinHandle<()>>,
}

impl PitchAnalysisWorker {
    fn start(
        samples: Arc<[f32]>,
        sample_rate: u32,
        channels: usize,
        shared: Arc<SharedPlaybackState>,
    ) -> Result<Self, AudioDeviceError> {
        let stop = Arc::new(AtomicBool::new(false));
        let thread_stop = Arc::clone(&stop);
        let thread = thread::Builder::new()
            .name("tinnotus-pitch-analysis".to_owned())
            .spawn(move || {
                let total_frames = samples.len() / channels;
                let window_capacity = total_frames.min(PITCH_ANALYSIS_WINDOW_FRAMES);
                let mut mono_samples = vec![0.0_f32; window_capacity];
                let mut last_position = 0;

                loop {
                    let position = shared
                        .playhead_frames
                        .load(Ordering::Relaxed)
                        .min(total_frames);
                    if position > 0 && position != last_position {
                        let frame_count = position.min(PITCH_ANALYSIS_WINDOW_FRAMES);
                        let start_frame = position - frame_count;
                        if frame_count >= 64 {
                            let channel =
                                loudest_channel(&samples, start_frame, frame_count, channels);
                            for frame in 0..frame_count {
                                mono_samples[frame] =
                                    samples[(start_frame + frame) * channels + channel];
                            }
                            match detect_pitch(&mono_samples[..frame_count], sample_rate) {
                                Ok(estimate) => shared.set_pitch_estimate(estimate),
                                Err(_) => {
                                    shared.pitch_analysis_failed.store(true, Ordering::Release);
                                    break;
                                }
                            }
                        } else if position == total_frames {
                            shared.set_pitch_estimate(PitchEstimate {
                                frequency_hz: None,
                                confidence: 0.0,
                            });
                        }
                        last_position = position;
                    }

                    if thread_stop.load(Ordering::Acquire) {
                        break;
                    }
                    thread::park_timeout(PITCH_ANALYSIS_INTERVAL);
                }
            })
            .map_err(|error| AudioDeviceError::PitchAnalysisThread(error.to_string()))?;

        Ok(Self {
            stop,
            thread: Some(thread),
        })
    }

    fn stop(&mut self) -> Result<(), AudioDeviceError> {
        self.stop.store(true, Ordering::Release);
        if let Some(thread) = self.thread.take() {
            thread.thread().unpark();
            thread
                .join()
                .map_err(|_| AudioDeviceError::PitchAnalysisThreadPanicked)?;
        }
        Ok(())
    }
}

impl Drop for PitchAnalysisWorker {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
        if let Some(thread) = self.thread.take() {
            thread.thread().unpark();
            let _ = thread.join();
        }
    }
}

fn loudest_channel(
    samples: &[f32],
    start_frame: usize,
    frame_count: usize,
    channels: usize,
) -> usize {
    let mut energy = [0.0_f64; MAX_CHANNELS];
    for frame in start_frame..start_frame + frame_count {
        let sample_start = frame * channels;
        for channel in 0..channels {
            let sample = f64::from(samples[sample_start + channel]);
            energy[channel] += sample * sample;
        }
    }
    (1..channels).fold(0, |loudest, channel| {
        if energy[channel] > energy[loudest] {
            channel
        } else {
            loudest
        }
    })
}

struct PlaybackRenderer {
    processor: AudioEngineProcessor,
    samples: Arc<[f32]>,
    cursor: usize,
    sample_rate: u32,
    channels: usize,
    scratch: Vec<f32>,
    shared: Arc<SharedPlaybackState>,
    last_callback_timestamp: Option<StreamInstant>,
}

impl PlaybackRenderer {
    fn new(
        processor: AudioEngineProcessor,
        samples: Arc<[f32]>,
        sample_rate: u32,
        channels: usize,
        shared: Arc<SharedPlaybackState>,
    ) -> Self {
        Self {
            processor,
            samples,
            cursor: 0,
            sample_rate,
            channels,
            scratch: vec![0.0; MAX_CALLBACK_SAMPLES],
            shared,
            last_callback_timestamp: None,
        }
    }

    fn render<T: OutputSample>(
        &mut self,
        output: &mut [T],
        callback_timestamp: Option<StreamInstant>,
    ) {
        if output.is_empty() {
            return;
        }
        if self.shared.status.load(Ordering::Acquire) != STATUS_PLAYING {
            output.fill(T::silence());
            return;
        }
        if output.len() > self.scratch.len() || !output.len().is_multiple_of(self.channels) {
            output.fill(T::silence());
            self.shared.fail_callback();
            return;
        }

        let scratch = &mut self.scratch[..output.len()];
        scratch.fill(0.0);

        let available = self.samples.len() - self.cursor;
        let process_count = available.min(output.len()) / self.channels * self.channels;
        if process_count > 0 {
            let input = &self.samples[self.cursor..self.cursor + process_count];
            if self
                .processor
                .process(input, &mut scratch[..process_count])
                .is_err()
            {
                output.fill(T::silence());
                self.shared.fail_callback();
                return;
            }
            self.cursor += process_count;
        }
        self.shared
            .playhead_frames
            .store(self.cursor / self.channels, Ordering::Relaxed);

        let mut invalid_sample = false;
        for (destination, sample) in output.iter_mut().zip(scratch.iter().copied()) {
            if sample.is_finite() {
                *destination = T::from_float(sample);
            } else {
                *destination = T::silence();
                invalid_sample = true;
            }
        }
        if invalid_sample {
            self.shared.fail_callback();
            return;
        }

        if self.cursor == self.samples.len() {
            self.shared.finish();
        }

        let frame_count = output.len() / self.channels;
        if let Some(timestamp) = callback_timestamp {
            if let Some(previous) = self.last_callback_timestamp {
                let budget_nanos =
                    frame_count as u128 * 1_000_000_000 / u128::from(self.sample_rate);
                if timestamp
                    .duration_since(&previous)
                    .is_some_and(|elapsed| elapsed.as_nanos() > budget_nanos)
                {
                    self.shared
                        .callback_cadence_misses
                        .fetch_add(1, Ordering::Relaxed);
                }
            }
            self.last_callback_timestamp = Some(timestamp);
        }
    }
}

trait OutputSample: cpal::SizedSample + Copy + Send + 'static {
    fn from_float(sample: f32) -> Self;
    fn silence() -> Self;
}

impl OutputSample for f32 {
    fn from_float(sample: f32) -> Self {
        sample
    }

    fn silence() -> Self {
        0.0
    }
}

impl OutputSample for f64 {
    fn from_float(sample: f32) -> Self {
        f64::from(sample)
    }

    fn silence() -> Self {
        0.0
    }
}

macro_rules! impl_signed_output {
    ($sample_type:ty) => {
        impl OutputSample for $sample_type {
            fn from_float(sample: f32) -> Self {
                (sample.clamp(-1.0, 1.0) * <$sample_type>::MAX as f32).round() as $sample_type
            }

            fn silence() -> Self {
                0
            }
        }
    };
}

macro_rules! impl_unsigned_output {
    ($sample_type:ty) => {
        impl OutputSample for $sample_type {
            fn from_float(sample: f32) -> Self {
                (((sample.clamp(-1.0, 1.0) as f64 + 1.0) * 0.5) * <$sample_type>::MAX as f64)
                    .round() as $sample_type
            }

            fn silence() -> Self {
                <$sample_type>::MAX / 2 + 1
            }
        }
    };
}

impl_signed_output!(i8);
impl_signed_output!(i16);
impl_signed_output!(i32);
impl_signed_output!(i64);
impl_unsigned_output!(u8);
impl_unsigned_output!(u16);
impl_unsigned_output!(u32);
impl_unsigned_output!(u64);

fn build_output_stream(
    device: &Device,
    config: &StreamConfig,
    format: SampleFormat,
    renderer: PlaybackRenderer,
    shared: Arc<SharedPlaybackState>,
) -> Result<Stream, AudioDeviceError> {
    match format {
        SampleFormat::F32 => build_typed_stream::<f32>(device, config, renderer, shared),
        SampleFormat::F64 => build_typed_stream::<f64>(device, config, renderer, shared),
        SampleFormat::I8 => build_typed_stream::<i8>(device, config, renderer, shared),
        SampleFormat::I16 => build_typed_stream::<i16>(device, config, renderer, shared),
        SampleFormat::I32 => build_typed_stream::<i32>(device, config, renderer, shared),
        SampleFormat::I64 => build_typed_stream::<i64>(device, config, renderer, shared),
        SampleFormat::U8 => build_typed_stream::<u8>(device, config, renderer, shared),
        SampleFormat::U16 => build_typed_stream::<u16>(device, config, renderer, shared),
        SampleFormat::U32 => build_typed_stream::<u32>(device, config, renderer, shared),
        SampleFormat::U64 => build_typed_stream::<u64>(device, config, renderer, shared),
        other => Err(AudioDeviceError::UnsupportedSampleFormat(format!(
            "{other:?}"
        ))),
    }
}

fn build_typed_stream<T: OutputSample>(
    device: &Device,
    config: &StreamConfig,
    mut renderer: PlaybackRenderer,
    shared: Arc<SharedPlaybackState>,
) -> Result<Stream, AudioDeviceError> {
    let error_state = Arc::clone(&shared);
    device
        .build_output_stream::<T, _, _>(
            config,
            move |output: &mut [T], info| renderer.render(output, Some(info.timestamp().callback)),
            move |_error| error_state.fail_device(),
            None,
        )
        .map_err(|error: BuildStreamError| AudioDeviceError::Stream(error.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    fn configured_endpoint_test_seconds() -> u64 {
        let seconds = match std::env::var("TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS") {
            Ok(value) => value
                .parse::<u64>()
                .expect("TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS must be an integer"),
            Err(std::env::VarError::NotPresent) => 1,
            Err(error) => panic!("could not read TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS: {error}"),
        };
        assert!(
            (1..=120).contains(&seconds),
            "TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS must be between 1 and 120"
        );
        seconds
    }

    #[test]
    fn resamples_constant_audio_and_duplicates_mono() {
        let output = convert_audio(&[0.25, 0.25], 44_100, 1, 48_000, 2).expect("valid conversion");

        assert_eq!(output.len(), 6);
        for frame in output.chunks_exact(2) {
            assert!((frame[0] - 0.25).abs() < 1.0e-5);
            assert_eq!(frame[0], frame[1]);
        }
    }

    #[test]
    fn downsampling_preserves_passband_and_rejects_aliases() {
        const INPUT_RATE: u32 = 48_000;
        const OUTPUT_RATE: u32 = 24_000;
        const INPUT_FRAMES: usize = INPUT_RATE as usize;

        let passband: Vec<f32> = (0..INPUT_FRAMES)
            .map(|frame| {
                (std::f64::consts::TAU * 1000.0 * frame as f64 / f64::from(INPUT_RATE)).sin() as f32
            })
            .collect();
        let output =
            convert_audio(&passband, INPUT_RATE, 1, OUTPUT_RATE, 1).expect("valid conversion");
        let passband_rms = rms(&output[100..output.len() - 100]);
        assert!((passband_rms - std::f64::consts::FRAC_1_SQRT_2).abs() < 0.01);

        let stopband: Vec<f32> = (0..INPUT_FRAMES)
            .map(|frame| {
                (std::f64::consts::TAU * 18_000.0 * frame as f64 / f64::from(INPUT_RATE)).sin()
                    as f32
            })
            .collect();
        let output =
            convert_audio(&stopband, INPUT_RATE, 1, OUTPUT_RATE, 1).expect("valid conversion");
        let alias_rms = rms(&output[100..output.len() - 100]);
        assert!(alias_rms < 0.01, "downsampling alias RMS was {alias_rms}");
    }

    #[test]
    fn channel_mapping_preserves_order_and_handles_mismatched_counts_conservatively() {
        assert_eq!(
            convert_audio(&[0.5, -0.25], 48_000, 2, 48_000, 2).expect("same layout"),
            [0.5, -0.25]
        );
        assert_eq!(
            convert_audio(&[0.5, -0.25], 48_000, 2, 48_000, 1).expect("downmix to mono"),
            [0.125]
        );
        assert_eq!(
            convert_audio(&[0.5, -0.25], 48_000, 2, 48_000, 4).expect("preserve common channels"),
            [0.5, -0.25, 0.0, 0.0]
        );
        assert_eq!(
            convert_audio(&[0.5, -0.25, 0.75, -1.0], 48_000, 4, 48_000, 2)
                .expect("drop channels without a known layout"),
            [0.5, -0.25]
        );
    }

    fn rms(samples: &[f32]) -> f64 {
        (samples
            .iter()
            .map(|sample| f64::from(*sample).powi(2))
            .sum::<f64>()
            / samples.len() as f64)
            .sqrt()
    }

    #[test]
    fn callback_processes_audio_and_pads_after_end_of_file() {
        let engine = AudioEngine::new(48_000, 1).expect("valid engine");
        let (processor, _control) = engine.split_for_realtime();
        let shared = Arc::new(SharedPlaybackState::new());
        let mut renderer = PlaybackRenderer::new(
            processor,
            Arc::from(vec![0.25, -0.5]),
            48_000,
            1,
            Arc::clone(&shared),
        );
        shared.status.store(STATUS_PLAYING, Ordering::Release);
        let mut output = [0.0_f32; 4];

        renderer.render(&mut output, None);

        assert_eq!(output, [0.25, -0.5, 0.0, 0.0]);
        assert_eq!(shared.status.load(Ordering::Acquire), STATUS_FINISHED);
    }

    #[test]
    fn late_device_errors_do_not_replace_terminal_status() {
        let shared = SharedPlaybackState::new();
        shared.status.store(STATUS_PLAYING, Ordering::Relaxed);
        shared.fail_device();
        assert_eq!(shared.status.load(Ordering::Relaxed), STATUS_DEVICE_ERROR);

        for terminal_status in [
            STATUS_FINISHED,
            STATUS_STOPPED,
            STATUS_DEVICE_ERROR,
            STATUS_CALLBACK_ERROR,
        ] {
            shared.status.store(terminal_status, Ordering::Relaxed);
            shared.fail_device();
            assert_eq!(shared.status.load(Ordering::Relaxed), terminal_status);
        }
    }

    #[test]
    fn headless_callback_processes_thirty_seconds_of_audio() {
        let sample_rate = 48_000;
        let samples = vec![0.1_f32; sample_rate as usize * 30];
        let sample_count = samples.len();
        let engine = AudioEngine::new(sample_rate, 1).expect("valid engine");
        let (processor, _control) = engine.split_for_realtime();
        let shared = Arc::new(SharedPlaybackState::new());
        shared.status.store(STATUS_PLAYING, Ordering::Release);
        let mut renderer = PlaybackRenderer::new(
            processor,
            Arc::from(samples),
            sample_rate,
            1,
            Arc::clone(&shared),
        );
        let mut block = [0.0_f32; 480];

        while renderer.cursor < sample_count {
            renderer.render(&mut block, None);
            assert!(block.iter().all(|sample| sample.is_finite()));
        }

        assert_eq!(renderer.cursor, sample_count);
        assert_eq!(shared.status.load(Ordering::Acquire), STATUS_FINISHED);
    }

    #[test]
    fn pitch_analysis_runs_separately_and_updates_the_current_estimate() {
        const SAMPLE_RATE: u32 = 48_000;
        const SAMPLE_COUNT: usize = 4096;
        let samples: Vec<f32> = (0..SAMPLE_COUNT)
            .map(|index| {
                (std::f64::consts::TAU * 440.0 * index as f64 / f64::from(SAMPLE_RATE)).sin() as f32
            })
            .collect();
        let shared = Arc::new(SharedPlaybackState::new());
        shared
            .playhead_frames
            .store(SAMPLE_COUNT, Ordering::Relaxed);
        let mut analysis =
            PitchAnalysisWorker::start(Arc::from(samples), SAMPLE_RATE, 1, Arc::clone(&shared))
                .expect("start the pitch analysis thread");

        let deadline = Instant::now() + Duration::from_secs(2);
        while shared.pitch_estimate().is_none() && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(10));
        }
        analysis.stop().expect("join the pitch analysis thread");

        let estimate = shared
            .pitch_estimate()
            .expect("pitch estimate is available");
        assert!((estimate.frequency_hz.expect("sine has a pitch") - 440.0).abs() < 2.0);
        assert!(estimate.confidence >= 0.8);
    }

    #[test]
    #[ignore = "runs the virtual output callback for 30 seconds"]
    fn virtual_null_output_runs_for_thirty_seconds() {
        let seconds = std::env::var("TINNOTUS_AUDIO_DEVICE_TEST_SECONDS")
            .map(|value| {
                value
                    .parse::<u64>()
                    .expect("TINNOTUS_AUDIO_DEVICE_TEST_SECONDS must be an integer")
            })
            .unwrap_or(30);
        assert!((1..=120).contains(&seconds));

        let sample_rate = 48_000_u32;
        let sample_count = usize::try_from(u64::from(sample_rate) * seconds)
            .expect("duration fits in address space");
        let samples = vec![0.1_f32; sample_count];
        let engine = AudioEngine::new(sample_rate, 1).expect("valid engine");
        let (processor, _control) = engine.split_for_realtime();
        let shared = Arc::new(SharedPlaybackState::new());
        shared.status.store(STATUS_PLAYING, Ordering::Release);
        let mut renderer = PlaybackRenderer::new(
            processor,
            Arc::from(samples),
            sample_rate,
            1,
            Arc::clone(&shared),
        );
        let mut block = [0.0_f32; 480];
        let period = std::time::Duration::from_millis(10);
        let started = Instant::now();
        let mut next_callback = started;

        while renderer.cursor < sample_count {
            renderer.render(&mut block, None);
            assert!(block.iter().all(|sample| sample.is_finite()));
            next_callback += period;
            std::thread::sleep(next_callback.saturating_duration_since(Instant::now()));
        }

        assert!(started.elapsed() >= std::time::Duration::from_secs(seconds));
        assert_eq!(renderer.cursor, sample_count);
        assert_eq!(shared.status.load(Ordering::Acquire), STATUS_FINISHED);
    }

    #[test]
    #[ignore = "requires an active audio endpoint"]
    fn default_device_plays_silence_to_completion() {
        assert!(
            cpal::default_host().default_output_device().is_some(),
            "an active output endpoint is required for this opt-in test"
        );
        assert!(!enumerate_output_devices()
            .expect("enumerate output devices")
            .is_empty());

        let seconds = configured_endpoint_test_seconds();
        let sample_count = usize::try_from(u64::from(48_000_u32) * seconds)
            .expect("test duration fits in address space");
        let mut playback =
            AudioPlayback::start(vec![0.0; sample_count], 48_000, 1, None, 1000.0, 100.0)
                .expect("open and start the default output endpoint");
        std::thread::sleep(Duration::from_millis(50));
        playback
            .set_notch(0, 1200.0, 120.0)
            .expect("queue a live notch update");
        let timeout = Instant::now() + Duration::from_secs(seconds + 10);
        while playback.status() == AudioPlaybackStatus::Playing && Instant::now() < timeout {
            std::thread::sleep(Duration::from_millis(10));
            playback.poll().expect("poll output playback");
        }

        assert_eq!(playback.status(), AudioPlaybackStatus::Finished);

        let mut stopped_playback =
            AudioPlayback::start(vec![0.0; 48_000], 48_000, 1, None, 1000.0, 100.0)
                .expect("restart the default output endpoint");
        std::thread::sleep(Duration::from_millis(50));
        stopped_playback.stop().expect("stop output playback");
        assert_eq!(stopped_playback.status(), AudioPlaybackStatus::Stopped);
    }

    #[test]
    #[ignore = "requires an active audio endpoint"]
    fn selected_device_supports_repeated_start_stop() {
        let selected_device = enumerate_output_devices()
            .expect("enumerate output devices")
            .into_iter()
            .next()
            .expect("at least one output endpoint is available");

        for _ in 0..3 {
            let mut playback = AudioPlayback::start(
                vec![0.0; 4_800],
                48_000,
                1,
                Some(selected_device.clone()),
                1000.0,
                100.0,
            )
            .expect("start the selected output endpoint");
            playback.stop().expect("stop the selected endpoint");
            playback.stop().expect("stop is idempotent");
            assert_eq!(
                playback.poll().expect("poll after stopping"),
                AudioPlaybackStatus::Stopped
            );
        }
    }
}
