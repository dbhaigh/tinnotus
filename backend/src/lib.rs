use std::cell::Cell;
use std::ffi::c_int;
use std::fmt;
use std::marker::PhantomData;
use std::ptr::NonNull;
use std::sync::Arc;

const MAX_CHANNELS: usize = 8;
const MAX_NOTCHES: usize = 8;
const TINNOTUS_AUDIO_OK: c_int = 0;
const TINNOTUS_AUDIO_INVALID_ARGUMENT: c_int = -1;
const TINNOTUS_AUDIO_QUEUE_FULL: c_int = -2;
const MIN_PITCH_WINDOW_SAMPLES: usize = 64;
const MAX_PITCH_WINDOW_SAMPLES: usize = 8192;
const MIN_PITCH_SAMPLE_RATE: u32 = 8000;
const MAX_PITCH_SAMPLE_RATE: u32 = 192_000;
const MIN_PITCH_CONFIDENCE: f64 = 0.8;

#[repr(C)]
struct RawAudioEngine {
    _private: [u8; 0],
}

extern "C" {
    fn tinnotus_audio_engine_create(sample_rate: c_int, channels: usize) -> *mut RawAudioEngine;
    fn tinnotus_audio_engine_destroy(engine: *mut RawAudioEngine);
    fn tinnotus_audio_engine_set_notch(
        engine: *mut RawAudioEngine,
        index: usize,
        center_frequency_hz: f64,
        bandwidth_hz: f64,
    ) -> c_int;
    fn tinnotus_audio_engine_remove_notch(engine: *mut RawAudioEngine, index: usize) -> c_int;
    fn tinnotus_audio_engine_process(
        engine: *mut RawAudioEngine,
        input: *const f32,
        output: *mut f32,
        frame_count: usize,
    ) -> c_int;
    fn tinnotus_pitch_detect(
        samples: *const f32,
        sample_count: usize,
        sample_rate: c_int,
        frequency_hz: *mut f64,
        confidence: *mut f64,
    ) -> c_int;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioError {
    InvalidConfiguration,
    InvalidArgument,
    QueueFull,
    AllocationFailed,
    NativeStatus(c_int),
}

impl fmt::Display for AudioError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidConfiguration => formatter.write_str("invalid audio-engine configuration"),
            Self::InvalidArgument => formatter.write_str("invalid audio-engine argument"),
            Self::QueueFull => formatter.write_str("audio-engine control queue is full"),
            Self::AllocationFailed => formatter.write_str("could not allocate the audio engine"),
            Self::NativeStatus(status) => {
                write!(formatter, "audio engine returned unknown status {status}")
            }
        }
    }
}

impl std::error::Error for AudioError {}

pub type AudioEngineError = AudioError;

/// Pitch estimate for one bounded, mono audio window.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PitchEstimate {
    /// Fundamental frequency in hertz when confidence is at least 0.8.
    pub frequency_hz: Option<f64>,
    /// YIN periodicity confidence in the range 0.0–1.0.
    pub confidence: f64,
}

/// Estimates pitch with YIN on a bounded mono window.
///
/// The function accepts 64–8,192 samples at 8–192 kHz. It does not allocate,
/// but its bounded analysis cost is intended for a background analysis thread,
/// not the real-time audio callback. A missing frequency indicates silence,
/// noise, or confidence below the pitch threshold.
pub fn detect_pitch(samples: &[f32], sample_rate: u32) -> Result<PitchEstimate, AudioError> {
    if !(MIN_PITCH_WINDOW_SAMPLES..=MAX_PITCH_WINDOW_SAMPLES).contains(&samples.len())
        || !(MIN_PITCH_SAMPLE_RATE..=MAX_PITCH_SAMPLE_RATE).contains(&sample_rate)
    {
        return Err(AudioError::InvalidArgument);
    }

    let sample_rate = c_int::try_from(sample_rate).map_err(|_| AudioError::InvalidArgument)?;
    let mut frequency_hz = 0.0;
    let mut confidence = 0.0;
    let status = unsafe {
        tinnotus_pitch_detect(
            samples.as_ptr(),
            samples.len(),
            sample_rate,
            &mut frequency_hz,
            &mut confidence,
        )
    };
    map_status(status)?;

    Ok(PitchEstimate {
        frequency_hz: (frequency_hz > 0.0 && confidence >= MIN_PITCH_CONFIDENCE)
            .then_some(frequency_hz),
        confidence,
    })
}

/// Owned, safe access to the native audio engine.
///
/// Processing accepts borrowed interleaved sample slices and does not allocate.
/// For separate control and audio threads, consume the engine with
/// [`split_for_realtime`](Self::split_for_realtime).
///
/// ```
/// use tinnotus_audio_backend::{AudioEngine, AudioError};
///
/// fn main() -> Result<(), AudioError> {
///     let mut engine = AudioEngine::new(48_000, 2)?;
///     engine.set_notch(0, 1_000.0, 100.0)?;
///     let input = [0.0_f32; 128];
///     let mut output = [0.0_f32; 128];
///     engine.process(&input, &mut output)?;
///     Ok(())
/// }
/// ```
pub struct AudioEngine {
    handle: Arc<AudioEngineHandle>,
    sample_rate: u32,
    channels: usize,
    enabled_notches: [bool; MAX_NOTCHES],
    _not_sync: PhantomData<Cell<()>>,
}

/// The single control-thread producer for a split audio engine.
pub struct AudioEngineControl {
    handle: Arc<AudioEngineHandle>,
    sample_rate: u32,
    channels: usize,
    enabled_notches: [bool; MAX_NOTCHES],
    _not_sync: PhantomData<Cell<()>>,
}

/// The single processing-thread consumer for a split audio engine.
///
/// Its `process` method uses only caller-owned buffers and performs no
/// allocation or locking.
pub struct AudioEngineProcessor {
    handle: Arc<AudioEngineHandle>,
    sample_rate: u32,
    channels: usize,
    _not_sync: PhantomData<Cell<()>>,
}

struct AudioEngineHandle(NonNull<RawAudioEngine>);

// The native engine supports one control producer and one processing consumer.
// The wrappers below are !Sync and split those roles before crossing threads.
unsafe impl Send for AudioEngineHandle {}
unsafe impl Sync for AudioEngineHandle {}

impl AudioEngineHandle {
    fn set_notch(
        &self,
        index: usize,
        center_frequency_hz: f64,
        bandwidth_hz: f64,
    ) -> Result<(), AudioError> {
        if index >= MAX_NOTCHES {
            return Err(AudioError::InvalidArgument);
        }

        let status = unsafe {
            tinnotus_audio_engine_set_notch(
                self.0.as_ptr(),
                index,
                center_frequency_hz,
                bandwidth_hz,
            )
        };
        map_status(status)
    }

    fn remove_notch(&self, index: usize) -> Result<(), AudioError> {
        if index >= MAX_NOTCHES {
            return Err(AudioError::InvalidArgument);
        }

        let status = unsafe { tinnotus_audio_engine_remove_notch(self.0.as_ptr(), index) };
        map_status(status)
    }

    fn process(
        &self,
        channels: usize,
        input: &[f32],
        output: &mut [f32],
    ) -> Result<(), AudioError> {
        if channels == 0 || input.len() != output.len() || !input.len().is_multiple_of(channels) {
            return Err(AudioError::InvalidArgument);
        }

        let status = unsafe {
            tinnotus_audio_engine_process(
                self.0.as_ptr(),
                input.as_ptr(),
                output.as_mut_ptr(),
                input.len() / channels,
            )
        };
        map_status(status)
    }
}

impl Drop for AudioEngineHandle {
    fn drop(&mut self) {
        unsafe { tinnotus_audio_engine_destroy(self.0.as_ptr()) };
    }
}

impl AudioEngine {
    pub const MAX_CHANNELS: usize = MAX_CHANNELS;
    pub const MAX_NOTCHES: usize = MAX_NOTCHES;

    /// Creates an engine with a positive sample rate and 1–8 interleaved channels.
    pub fn new(sample_rate: u32, channels: usize) -> Result<Self, AudioError> {
        if sample_rate == 0 || !(1..=Self::MAX_CHANNELS).contains(&channels) {
            return Err(AudioError::InvalidConfiguration);
        }
        let sample_rate =
            c_int::try_from(sample_rate).map_err(|_| AudioError::InvalidConfiguration)?;
        let handle = unsafe { tinnotus_audio_engine_create(sample_rate, channels) };
        let handle = NonNull::new(handle).ok_or(AudioError::AllocationFailed)?;
        Ok(Self {
            handle: Arc::new(AudioEngineHandle(handle)),
            sample_rate: sample_rate as u32,
            channels,
            enabled_notches: [false; MAX_NOTCHES],
            _not_sync: PhantomData,
        })
    }

    pub fn set_notch(
        &mut self,
        index: usize,
        center_frequency_hz: f64,
        bandwidth_hz: f64,
    ) -> Result<(), AudioError> {
        self.handle
            .set_notch(index, center_frequency_hz, bandwidth_hz)?;
        self.enabled_notches[index] = true;
        Ok(())
    }

    pub fn remove_notch(&mut self, index: usize) -> Result<(), AudioError> {
        self.handle.remove_notch(index)?;
        self.enabled_notches[index] = false;
        Ok(())
    }

    /// Returns the configured sample rate in hertz.
    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    /// Returns the number of interleaved channels.
    pub fn channels(&self) -> usize {
        self.channels
    }

    /// Reports the latest successfully queued state; processing applies queued
    /// changes at the start of a block.
    pub fn is_notch_enabled(&self, index: usize) -> Result<bool, AudioError> {
        self.enabled_notches
            .get(index)
            .copied()
            .ok_or(AudioError::InvalidArgument)
    }

    /// Processes one interleaved block without allocating.
    ///
    /// `input` and `output` must have equal lengths containing complete frames.
    pub fn process(&mut self, input: &[f32], output: &mut [f32]) -> Result<(), AudioError> {
        self.handle.process(self.channels, input, output)
    }

    /// Compatibility alias for [`process`](Self::process).
    pub fn process_interleaved(
        &mut self,
        input: &[f32],
        output: &mut [f32],
    ) -> Result<(), AudioError> {
        self.process(input, output)
    }

    /// Splits the engine into its control producer and processing consumer.
    ///
    /// Keep `control` on the configuration thread and move `processor` to the
    /// audio thread. The queue is bounded, so control operations can return
    /// [`AudioError::QueueFull`].
    ///
    /// ```
    /// use tinnotus_audio_backend::{AudioEngine, AudioError};
    ///
    /// fn main() -> Result<(), AudioError> {
    ///     let engine = AudioEngine::new(48_000, 1)?;
    ///     let (mut processor, mut control) = engine.split_for_realtime();
    ///     let audio_thread = std::thread::spawn(move || {
    ///         let input = [0.0_f32; 128];
    ///         let mut output = [0.0_f32; 128];
    ///         processor.process(&input, &mut output)
    ///     });
    ///     control.set_notch(0, 1_000.0, 100.0)?;
    ///     audio_thread.join().expect("audio thread panicked")?;
    ///     Ok(())
    /// }
    /// ```
    pub fn split_for_realtime(self) -> (AudioEngineProcessor, AudioEngineControl) {
        let control = AudioEngineControl {
            handle: Arc::clone(&self.handle),
            sample_rate: self.sample_rate,
            channels: self.channels,
            enabled_notches: self.enabled_notches,
            _not_sync: PhantomData,
        };
        let processor = AudioEngineProcessor {
            handle: self.handle,
            sample_rate: self.sample_rate,
            channels: self.channels,
            _not_sync: PhantomData,
        };
        (processor, control)
    }
}

impl AudioEngineControl {
    pub fn set_notch(
        &mut self,
        index: usize,
        center_frequency_hz: f64,
        bandwidth_hz: f64,
    ) -> Result<(), AudioError> {
        self.handle
            .set_notch(index, center_frequency_hz, bandwidth_hz)?;
        self.enabled_notches[index] = true;
        Ok(())
    }

    pub fn remove_notch(&mut self, index: usize) -> Result<(), AudioError> {
        self.handle.remove_notch(index)?;
        self.enabled_notches[index] = false;
        Ok(())
    }

    /// Returns the configured sample rate in hertz.
    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    /// Returns the number of interleaved channels.
    pub fn channels(&self) -> usize {
        self.channels
    }

    /// Reports the latest successfully queued state for this control handle.
    pub fn is_notch_enabled(&self, index: usize) -> Result<bool, AudioError> {
        self.enabled_notches
            .get(index)
            .copied()
            .ok_or(AudioError::InvalidArgument)
    }
}

impl AudioEngineProcessor {
    /// Returns the configured sample rate in hertz.
    pub fn sample_rate(&self) -> u32 {
        self.sample_rate
    }

    /// Returns the number of interleaved channels.
    pub fn channels(&self) -> usize {
        self.channels
    }

    /// Processes one interleaved block without allocating.
    pub fn process(&mut self, input: &[f32], output: &mut [f32]) -> Result<(), AudioError> {
        self.handle.process(self.channels, input, output)
    }
}

fn map_status(status: c_int) -> Result<(), AudioError> {
    match status {
        TINNOTUS_AUDIO_OK => Ok(()),
        TINNOTUS_AUDIO_INVALID_ARGUMENT => Err(AudioError::InvalidArgument),
        TINNOTUS_AUDIO_QUEUE_FULL => Err(AudioError::QueueFull),
        status => Err(AudioError::NativeStatus(status)),
    }
}

#[cfg(any(target_os = "windows", target_os = "macos", target_os = "linux"))]
#[path = "../audio_device.rs"]
mod audio_device;

#[cfg(any(target_os = "windows", target_os = "macos", target_os = "linux"))]
pub use audio_device::{
    enumerate_output_devices, AudioDeviceError, AudioPlayback, AudioPlaybackStatus,
    OutputDeviceInfo,
};

#[cfg(target_os = "windows")]
pub use audio_device::WindowsAudioPlayback;

#[cfg(test)]
mod tests {
    use super::{
        detect_pitch, map_status, AudioEngine, AudioError, TINNOTUS_AUDIO_INVALID_ARGUMENT,
        TINNOTUS_AUDIO_OK, TINNOTUS_AUDIO_QUEUE_FULL,
    };
    use std::f64::consts::TAU;

    #[test]
    fn validates_engine_configuration_and_exposes_cached_format() {
        assert_eq!(
            AudioEngine::new(0, 1).err(),
            Some(AudioError::InvalidConfiguration)
        );
        assert_eq!(
            AudioEngine::new(48_000, 0).err(),
            Some(AudioError::InvalidConfiguration)
        );
        assert_eq!(
            AudioEngine::new(48_000, AudioEngine::MAX_CHANNELS + 1).err(),
            Some(AudioError::InvalidConfiguration)
        );
        assert_eq!(
            AudioEngine::new(u32::MAX, 1).err(),
            Some(AudioError::InvalidConfiguration)
        );

        let engine = AudioEngine::new(48_000, 2).expect("valid engine");
        assert_eq!(engine.sample_rate(), 48_000);
        assert_eq!(engine.channels(), 2);
    }

    #[test]
    fn maps_native_status_codes_without_collapsing_unknown_values() {
        assert_eq!(map_status(TINNOTUS_AUDIO_OK), Ok(()));
        assert_eq!(
            map_status(TINNOTUS_AUDIO_INVALID_ARGUMENT),
            Err(AudioError::InvalidArgument)
        );
        assert_eq!(
            map_status(TINNOTUS_AUDIO_QUEUE_FULL),
            Err(AudioError::QueueFull)
        );
        assert_eq!(map_status(-99), Err(AudioError::NativeStatus(-99)));
    }

    #[test]
    fn tracks_queued_notch_state_and_rejects_invalid_indices() {
        let mut engine = AudioEngine::new(48_000, 1).expect("valid engine");
        assert_eq!(engine.is_notch_enabled(0), Ok(false));
        engine.set_notch(0, 1_000.0, 100.0).expect("valid notch");
        assert_eq!(engine.is_notch_enabled(0), Ok(true));
        engine.remove_notch(0).expect("remove notch");
        assert_eq!(engine.is_notch_enabled(0), Ok(false));
        assert_eq!(
            engine.is_notch_enabled(AudioEngine::MAX_NOTCHES),
            Err(AudioError::InvalidArgument)
        );
        assert_eq!(
            engine.remove_notch(AudioEngine::MAX_NOTCHES),
            Err(AudioError::InvalidArgument)
        );
    }

    #[test]
    fn reports_full_control_queue() {
        let mut engine = AudioEngine::new(48_000, 1).expect("valid engine");
        for _ in 0..16 {
            engine
                .set_notch(0, 1_000.0, 100.0)
                .expect("command fits in queue");
        }
        assert_eq!(
            engine.set_notch(0, 1_000.0, 100.0),
            Err(AudioError::QueueFull)
        );
    }

    #[test]
    fn processes_and_filters_interleaved_audio() {
        let mut engine = AudioEngine::new(48_000, 1).expect("valid engine");
        engine.set_notch(0, 1000.0, 100.0).expect("valid notch");
        let input = vec![0.0_f32; 128];
        let mut output = vec![1.0_f32; input.len()];

        engine.process(&input, &mut output).expect("valid block");
        assert!(output.iter().all(|sample| *sample == 0.0));
    }

    #[test]
    fn rejects_mismatched_and_incomplete_process_buffers() {
        let mut engine = AudioEngine::new(48_000, 2).expect("valid engine");
        let input = [0.0_f32; 3];
        let mut output = [0.0_f32; 3];

        assert_eq!(
            engine.process(&input, &mut output),
            Err(AudioError::InvalidArgument)
        );
        assert_eq!(
            engine.process(&[0.0_f32; 2], &mut [0.0_f32; 1]),
            Err(AudioError::InvalidArgument)
        );
        assert_eq!(engine.process(&[], &mut []), Ok(()));
    }

    #[test]
    fn realtime_control_and_processor_handles_can_run_on_separate_threads() {
        let engine = AudioEngine::new(48_000, 1).expect("valid engine");
        let (mut processor, mut control) = engine.split_for_realtime();
        let processing_thread = std::thread::spawn(move || {
            let input = [0.25_f32; 128];
            let mut output = [0.0_f32; 128];
            processor.process(&input, &mut output).expect("valid block");
            assert!(output.iter().all(|sample| sample.is_finite()));
        });

        control
            .set_notch(0, 1000.0, 100.0)
            .expect("valid concurrent update");
        processing_thread.join().expect("processing thread");
    }

    #[test]
    fn detects_synthetic_sine_pitch_with_high_confidence() {
        const SAMPLE_RATE: u32 = 48_000;
        const SAMPLE_COUNT: usize = 4096;
        for expected_frequency in [55.0, 110.0, 220.0, 440.0, 880.0, 1760.0] {
            let samples: Vec<f32> = (0..SAMPLE_COUNT)
                .map(|index| {
                    (TAU * expected_frequency * index as f64 / f64::from(SAMPLE_RATE)).sin() as f32
                })
                .collect();
            let estimate = detect_pitch(&samples, SAMPLE_RATE).expect("valid pitch window");
            let actual_frequency = estimate.frequency_hz.expect("confident sine estimate");
            assert!(
                (actual_frequency - expected_frequency).abs()
                    <= (expected_frequency * 0.01).max(1.0),
                "expected {expected_frequency} Hz, got {actual_frequency} Hz"
            );
            assert!(estimate.confidence >= 0.8);
        }
    }

    #[test]
    fn noise_and_silence_return_low_confidence_without_a_pitch() {
        const SAMPLE_RATE: u32 = 48_000;
        const SAMPLE_COUNT: usize = 4096;
        let silence = vec![0.0; SAMPLE_COUNT];
        let silence_estimate = detect_pitch(&silence, SAMPLE_RATE).expect("valid silence window");
        assert_eq!(silence_estimate.frequency_hz, None);
        assert!(silence_estimate.confidence < 0.1);

        let mut random_state = 0xC0FFEE_u32;
        let noise: Vec<f32> = (0..SAMPLE_COUNT)
            .map(|_| {
                random_state = random_state
                    .wrapping_mul(1_664_525)
                    .wrapping_add(1_013_904_223);
                ((random_state >> 8) as f32 / 65_535.0) * 2.0 - 1.0
            })
            .collect();
        let noise_estimate = detect_pitch(&noise, SAMPLE_RATE).expect("valid noise window");
        assert_eq!(noise_estimate.frequency_hz, None);
        assert!(noise_estimate.confidence < 0.8);
    }

    #[test]
    fn rejects_invalid_pitch_windows_and_non_finite_samples() {
        assert_eq!(detect_pitch(&[], 48_000), Err(AudioError::InvalidArgument));
        assert_eq!(
            detect_pitch(&[0.0; 64], 7_999),
            Err(AudioError::InvalidArgument)
        );
        assert_eq!(
            detect_pitch(&[f32::NAN; 64], 48_000),
            Err(AudioError::InvalidArgument)
        );
    }
}
