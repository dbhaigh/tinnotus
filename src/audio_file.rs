use hound::{SampleFormat, WavReader, WavWriter};
use std::fs::{self, File, OpenOptions};
use std::io::{self, ErrorKind};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use tinnotus_audio_backend::{AudioEngine, AudioError};

const BLOCK_FRAMES: usize = 1024;
static TEMPORARY_FILE_SEQUENCE: AtomicU64 = AtomicU64::new(0);

#[derive(Debug)]
/// Typed errors returned while loading or processing WAV files.
pub enum WavError {
    InputNotFound(PathBuf),
    InputIo {
        path: PathBuf,
        source: io::Error,
    },
    InvalidWav(hound::Error),
    UnsupportedFormat {
        sample_format: SampleFormat,
        bits_per_sample: u16,
    },
    InvalidAudio(String),
    OutputAlreadyExists(PathBuf),
    InvalidOutputPath(PathBuf),
    OutputIo {
        path: PathBuf,
        source: io::Error,
    },
    OutputWav {
        path: PathBuf,
        source: hound::Error,
    },
    MemoryAllocation(String),
    Engine(AudioError),
    Cancelled,
}

impl std::fmt::Display for WavError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InputNotFound(path) => {
                write!(formatter, "input WAV was not found: {}", path.display())
            }
            Self::InputIo { path, source } => {
                write!(
                    formatter,
                    "could not read input WAV {}: {source}",
                    path.display()
                )
            }
            Self::InvalidWav(source) => write!(formatter, "invalid WAV file: {source}"),
            Self::UnsupportedFormat {
                sample_format,
                bits_per_sample,
            } => write!(
                formatter,
                "unsupported WAV format: {sample_format:?}, {bits_per_sample} bits per sample"
            ),
            Self::InvalidAudio(message) => write!(formatter, "invalid audio data: {message}"),
            Self::OutputAlreadyExists(path) => {
                write!(formatter, "output file already exists: {}", path.display())
            }
            Self::InvalidOutputPath(path) => {
                write!(formatter, "invalid output path: {}", path.display())
            }
            Self::OutputIo { path, source } => {
                write!(
                    formatter,
                    "could not write output {}: {source}",
                    path.display()
                )
            }
            Self::OutputWav { path, source } => {
                write!(
                    formatter,
                    "could not write output WAV {}: {source}",
                    path.display()
                )
            }
            Self::MemoryAllocation(message) => {
                write!(
                    formatter,
                    "could not allocate WAV processing memory: {message}"
                )
            }
            Self::Engine(error) => write!(formatter, "audio engine error: {error}"),
            Self::Cancelled => formatter.write_str("WAV processing was cancelled"),
        }
    }
}

impl std::error::Error for WavError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::InputIo { source, .. } | Self::OutputIo { source, .. } => Some(source),
            Self::InvalidWav(source) | Self::OutputWav { source, .. } => Some(source),
            Self::Engine(error) => Some(error),
            _ => None,
        }
    }
}

impl From<AudioError> for WavError {
    fn from(error: AudioError) -> Self {
        Self::Engine(error)
    }
}

pub struct WavAudio {
    pub sample_rate: u32,
    pub channels: usize,
    pub samples: Vec<f32>,
}

pub fn load_wav_for_playback(input_path: &Path) -> Result<WavAudio, WavError> {
    let mut reader =
        WavReader::open(input_path).map_err(|error| map_input_error(input_path, error))?;
    let spec = reader.spec();
    let channels = usize::from(spec.channels);
    if spec.sample_rate == 0 || !(1..=AudioEngine::MAX_CHANNELS).contains(&channels) {
        return Err(WavError::InvalidAudio(
            "unsupported sample rate or channel count".to_owned(),
        ));
    }

    match spec.sample_format {
        SampleFormat::Int if !matches!(spec.bits_per_sample, 16 | 24 | 32) => {
            return Err(WavError::UnsupportedFormat {
                sample_format: spec.sample_format,
                bits_per_sample: spec.bits_per_sample,
            });
        }
        SampleFormat::Float if spec.bits_per_sample != 32 => {
            return Err(WavError::UnsupportedFormat {
                sample_format: spec.sample_format,
                bits_per_sample: spec.bits_per_sample,
            });
        }
        _ => {}
    }

    let expected_samples = usize::try_from(reader.duration())
        .ok()
        .and_then(|frames| frames.checked_mul(channels))
        .ok_or_else(|| WavError::InvalidAudio("WAV is too large to load".to_owned()))?;
    let mut samples = Vec::new();
    samples
        .try_reserve_exact(expected_samples)
        .map_err(|error| WavError::MemoryAllocation(error.to_string()))?;

    match spec.sample_format {
        SampleFormat::Int => {
            let scale = 2_f64.powi(i32::from(spec.bits_per_sample) - 1);
            for sample in reader.samples::<i32>() {
                let sample = sample.map_err(|error| map_input_error(input_path, error))?;
                append_playback_sample(&mut samples, (f64::from(sample) / scale) as f32)?;
            }
        }
        SampleFormat::Float => {
            for sample in reader.samples::<f32>() {
                let sample = sample.map_err(|error| map_input_error(input_path, error))?;
                if !sample.is_finite() {
                    return Err(WavError::InvalidAudio(
                        "input contains a non-finite sample".to_owned(),
                    ));
                }
                append_playback_sample(&mut samples, sample)?;
            }
        }
    }

    if samples.is_empty() || !samples.len().is_multiple_of(channels) {
        return Err(WavError::InvalidAudio(
            "input contains no complete audio frames".to_owned(),
        ));
    }

    Ok(WavAudio {
        sample_rate: spec.sample_rate,
        channels,
        samples,
    })
}

fn append_playback_sample(samples: &mut Vec<f32>, sample: f32) -> Result<(), WavError> {
    if samples.len() == samples.capacity() {
        samples
            .try_reserve(1024)
            .map_err(|error| WavError::MemoryAllocation(error.to_string()))?;
    }
    samples.push(sample);
    Ok(())
}

struct TemporaryOutput {
    temporary_path: PathBuf,
    output_path: PathBuf,
    committed: bool,
}

impl TemporaryOutput {
    fn create(output_path: &Path) -> Result<(Self, File), WavError> {
        if output_path.exists() {
            return Err(WavError::OutputAlreadyExists(output_path.to_owned()));
        }

        let parent = output_path
            .parent()
            .filter(|path| !path.as_os_str().is_empty())
            .unwrap_or_else(|| Path::new("."));
        let file_name = output_path
            .file_name()
            .ok_or_else(|| WavError::InvalidOutputPath(output_path.to_owned()))?
            .to_string_lossy();

        for _ in 0..64 {
            let sequence = TEMPORARY_FILE_SEQUENCE.fetch_add(1, Ordering::Relaxed);
            let temporary_path = parent.join(format!(
                ".{file_name}.tinnotus-{}-{sequence}.tmp",
                std::process::id()
            ));

            match OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&temporary_path)
            {
                Ok(file) => {
                    return Ok((
                        Self {
                            temporary_path,
                            output_path: output_path.to_owned(),
                            committed: false,
                        },
                        file,
                    ));
                }
                Err(error) if error.kind() == ErrorKind::AlreadyExists => continue,
                Err(source) => {
                    return Err(WavError::OutputIo {
                        path: temporary_path,
                        source,
                    });
                }
            }
        }

        Err(WavError::OutputIo {
            path: output_path.to_owned(),
            source: io::Error::new(
                ErrorKind::AlreadyExists,
                "could not reserve a unique temporary output file",
            ),
        })
    }

    fn commit(mut self) -> Result<(), WavError> {
        fs::hard_link(&self.temporary_path, &self.output_path).map_err(|source| {
            if source.kind() == ErrorKind::AlreadyExists || self.output_path.exists() {
                WavError::OutputAlreadyExists(self.output_path.clone())
            } else {
                WavError::OutputIo {
                    path: self.output_path.clone(),
                    source,
                }
            }
        })?;
        if fs::remove_file(&self.temporary_path).is_ok() {
            self.committed = true;
        }
        Ok(())
    }
}

impl Drop for TemporaryOutput {
    fn drop(&mut self) {
        if !self.committed {
            let _ = fs::remove_file(&self.temporary_path);
        }
    }
}

/// Processes a WAV file in bounded blocks, reporting completed and total frames.
///
/// Cancellation is checked between blocks. A cancelled operation removes its
/// temporary output and does not commit the requested output path.
///
/// ```
/// use std::path::Path;
/// use std::sync::atomic::AtomicBool;
/// use tinnotus::{process_wav, WavError};
///
/// fn process(input: &Path, output: &Path) -> Result<(), WavError> {
///     let cancelled = AtomicBool::new(false);
///     process_wav(input, output, 1_000.0, 100.0, &cancelled, |done, total| {
///         println!("{done} / {total} frames");
///     })
/// }
/// ```
pub fn process_wav(
    input_path: &Path,
    output_path: &Path,
    center_frequency_hz: f64,
    bandwidth_hz: f64,
    cancellation: &AtomicBool,
    mut progress: impl FnMut(u64, u64),
) -> Result<(), WavError> {
    if cancellation.load(Ordering::Acquire) {
        return Err(WavError::Cancelled);
    }

    let mut reader =
        WavReader::open(input_path).map_err(|error| map_input_error(input_path, error))?;
    let spec = reader.spec();
    let channels = usize::from(spec.channels);
    if spec.sample_rate == 0 || !(1..=AudioEngine::MAX_CHANNELS).contains(&channels) {
        return Err(WavError::InvalidAudio(
            "unsupported sample rate or channel count".to_owned(),
        ));
    }

    match spec.sample_format {
        SampleFormat::Int if !matches!(spec.bits_per_sample, 16 | 24 | 32) => {
            return Err(WavError::UnsupportedFormat {
                sample_format: spec.sample_format,
                bits_per_sample: spec.bits_per_sample,
            });
        }
        SampleFormat::Float if spec.bits_per_sample != 32 => {
            return Err(WavError::UnsupportedFormat {
                sample_format: spec.sample_format,
                bits_per_sample: spec.bits_per_sample,
            });
        }
        _ => {}
    }

    let total_frames = u64::from(reader.duration());
    if total_frames == 0 {
        return Err(WavError::InvalidAudio(
            "input contains no frames".to_owned(),
        ));
    }

    let mut engine = AudioEngine::new(spec.sample_rate, channels)?;
    engine.set_notch(0, center_frequency_hz, bandwidth_hz)?;
    progress(0, total_frames);
    if cancellation.load(Ordering::Acquire) {
        return Err(WavError::Cancelled);
    }

    let (temporary_output, file) = TemporaryOutput::create(output_path)?;
    let mut writer =
        WavWriter::new(file, spec).map_err(|error| map_output_error(output_path, error))?;
    let mut frames_done = 0_u64;

    match spec.sample_format {
        SampleFormat::Int => {
            let scale = (1_i64 << (spec.bits_per_sample - 1)) as f64;
            process_samples(
                reader.samples::<i32>(),
                input_path,
                channels,
                &mut engine,
                cancellation,
                total_frames,
                &mut frames_done,
                &mut progress,
                |sample| Ok(sample as f32 / scale as f32),
                |sample| {
                    if !sample.is_finite() {
                        return Err(WavError::InvalidAudio(
                            "processing produced a non-finite sample".to_owned(),
                        ));
                    }
                    let encoded = (sample as f64 * scale).round().clamp(-scale, scale - 1.0) as i32;
                    writer
                        .write_sample(encoded)
                        .map_err(|error| map_output_error(output_path, error))
                },
            )?;
        }
        SampleFormat::Float => {
            process_samples(
                reader.samples::<f32>(),
                input_path,
                channels,
                &mut engine,
                cancellation,
                total_frames,
                &mut frames_done,
                &mut progress,
                |sample| {
                    if sample.is_finite() {
                        Ok(sample)
                    } else {
                        Err(WavError::InvalidAudio(
                            "input contains a non-finite sample".to_owned(),
                        ))
                    }
                },
                |sample| {
                    if !sample.is_finite() {
                        return Err(WavError::InvalidAudio(
                            "processing produced a non-finite sample".to_owned(),
                        ));
                    }
                    writer
                        .write_sample(sample)
                        .map_err(|error| map_output_error(output_path, error))
                },
            )?;
        }
    }

    if frames_done != total_frames {
        return Err(WavError::InvalidAudio(
            "decoded frame count did not match the WAV header".to_owned(),
        ));
    }

    writer
        .finalize()
        .map_err(|error| map_output_error(output_path, error))?;
    temporary_output.commit()
}

fn process_samples<T>(
    samples: impl Iterator<Item = Result<T, hound::Error>>,
    input_path: &Path,
    channels: usize,
    engine: &mut AudioEngine,
    cancellation: &AtomicBool,
    total_frames: u64,
    frames_done: &mut u64,
    progress: &mut impl FnMut(u64, u64),
    mut decode: impl FnMut(T) -> Result<f32, WavError>,
    mut write: impl FnMut(f32) -> Result<(), WavError>,
) -> Result<(), WavError> {
    let block_samples = BLOCK_FRAMES
        .checked_mul(channels)
        .ok_or_else(|| WavError::InvalidAudio("audio block size overflow".to_owned()))?;
    let mut input = Vec::new();
    input
        .try_reserve_exact(block_samples)
        .map_err(|error| WavError::MemoryAllocation(error.to_string()))?;
    input.resize(block_samples, 0.0_f32);
    let mut output = Vec::new();
    output
        .try_reserve_exact(block_samples)
        .map_err(|error| WavError::MemoryAllocation(error.to_string()))?;
    output.resize(block_samples, 0.0_f32);
    let mut samples = samples;

    loop {
        if cancellation.load(Ordering::Acquire) {
            return Err(WavError::Cancelled);
        }

        let mut count = 0;
        while count < block_samples {
            match samples.next() {
                Some(Ok(sample)) => {
                    input[count] = decode(sample)?;
                    count += 1;
                }
                Some(Err(error)) => {
                    return Err(map_input_error(input_path, error));
                }
                None => break,
            }
        }

        if count == 0 {
            break;
        }
        if count % channels != 0 {
            return Err(WavError::InvalidAudio(
                "input ends with an incomplete audio frame".to_owned(),
            ));
        }

        engine.process(&input[..count], &mut output[..count])?;
        for sample in &output[..count] {
            write(*sample)?;
        }
        let block_frames = u64::try_from(count / channels)
            .map_err(|_| WavError::InvalidAudio("frame count overflow".to_owned()))?;
        *frames_done = frames_done
            .checked_add(block_frames)
            .ok_or_else(|| WavError::InvalidAudio("frame count overflow".to_owned()))?;
        if *frames_done > total_frames {
            return Err(WavError::InvalidAudio(
                "decoded more frames than the WAV header reports".to_owned(),
            ));
        }
        progress(*frames_done, total_frames);
    }

    Ok(())
}

fn map_input_error(path: &Path, error: hound::Error) -> WavError {
    match error {
        hound::Error::IoError(source) if source.kind() == ErrorKind::NotFound => {
            WavError::InputNotFound(path.to_owned())
        }
        hound::Error::IoError(source) => WavError::InputIo {
            path: path.to_owned(),
            source,
        },
        error => WavError::InvalidWav(error),
    }
}

fn map_output_error(path: &Path, error: hound::Error) -> WavError {
    match error {
        hound::Error::IoError(source) => WavError::OutputIo {
            path: path.to_owned(),
            source,
        },
        source => WavError::OutputWav {
            path: path.to_owned(),
            source,
        },
    }
}

#[cfg(test)]
mod tests {
    use super::{load_wav_for_playback, process_wav, WavError};
    use hound::{SampleFormat, WavReader, WavSpec, WavWriter};
    use std::path::{Path, PathBuf};
    use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};

    static TEST_FILE_SEQUENCE: AtomicU64 = AtomicU64::new(0);

    fn test_path(label: &str) -> PathBuf {
        let sequence = TEST_FILE_SEQUENCE.fetch_add(1, Ordering::Relaxed);
        std::env::temp_dir().join(format!(
            "tinnotus-wav-{label}-{}-{sequence}.wav",
            std::process::id()
        ))
    }

    fn write_pcm16_wav(path: &Path, frames: usize) {
        let spec = WavSpec {
            channels: 1,
            sample_rate: 48_000,
            bits_per_sample: 16,
            sample_format: SampleFormat::Int,
        };
        let mut writer = WavWriter::create(path, spec).expect("create test input");
        for frame in 0..frames {
            let sample = (0.5
                * (2.0 * std::f64::consts::PI * 1000.0 * frame as f64
                    / f64::from(spec.sample_rate))
                .sin()
                * f64::from(i16::MAX)) as i16;
            writer.write_sample(sample).expect("write test sample");
        }
        writer.finalize().expect("finalize test input");
    }

    fn write_wav(path: &Path, spec: WavSpec, samples: &[f32]) {
        let mut writer = WavWriter::create(path, spec).expect("create test WAV");
        match spec.sample_format {
            SampleFormat::Int => {
                let scale = 2_f64.powi(i32::from(spec.bits_per_sample) - 1);
                for sample in samples {
                    let encoded = (f64::from(*sample) * scale)
                        .round()
                        .clamp(-scale, scale - 1.0) as i32;
                    writer
                        .write_sample(encoded)
                        .expect("write integer test sample");
                }
            }
            SampleFormat::Float => {
                for sample in samples {
                    writer
                        .write_sample(*sample)
                        .expect("write float test sample");
                }
            }
        }
        writer.finalize().expect("finalize test WAV");
    }

    #[test]
    fn reports_monotonic_frame_progress_when_processing_pcm_wav() {
        let input_path = test_path("progress-input");
        let output_path = test_path("progress-output");
        write_pcm16_wav(&input_path, 48_000);
        let cancellation = AtomicBool::new(false);
        let mut progress = Vec::new();

        process_wav(
            &input_path,
            &output_path,
            1000.0,
            100.0,
            &cancellation,
            |done, total| progress.push((done, total)),
        )
        .expect("process test WAV");

        let mut reader = WavReader::open(&output_path).expect("open processed WAV");
        let samples: Vec<i16> = reader
            .samples::<i16>()
            .collect::<Result<_, _>>()
            .expect("read processed samples");
        let input_rms = (0.5_f64 / 2.0_f64.sqrt()) * f64::from(i16::MAX);
        let output_rms = (samples
            .iter()
            .map(|sample| f64::from(*sample).powi(2))
            .sum::<f64>()
            / samples.len() as f64)
            .sqrt();
        assert!(
            output_rms / input_rms < 0.1,
            "notch output RMS ratio was {}",
            output_rms / input_rms
        );
        assert_eq!(progress.first(), Some(&(0, 48_000)));
        assert_eq!(progress.last(), Some(&(48_000, 48_000)));
        assert!(progress.windows(2).all(|pair| pair[0].0 <= pair[1].0));

        std::fs::remove_file(input_path).expect("remove test input");
        std::fs::remove_file(output_path).expect("remove test output");
    }

    #[test]
    fn cancellation_stops_between_blocks_without_committing_output() {
        let input_path = test_path("cancel-input");
        let output_path = test_path("cancel-output");
        write_pcm16_wav(&input_path, 4096);
        let cancellation = AtomicBool::new(false);
        let mut progress = Vec::new();

        let result = process_wav(
            &input_path,
            &output_path,
            1000.0,
            100.0,
            &cancellation,
            |done, total| {
                progress.push((done, total));
                if done > 0 {
                    cancellation.store(true, Ordering::Release);
                }
            },
        );

        assert!(matches!(result, Err(WavError::Cancelled)));
        assert_eq!(progress, vec![(0, 4096), (1024, 4096)]);
        assert!(!output_path.exists());
        std::fs::remove_file(input_path).expect("remove test input");
    }

    #[test]
    fn distinguishes_input_output_format_and_engine_errors() {
        let missing_input = test_path("missing-input");
        let output_path = test_path("errors-output");
        let cancellation = AtomicBool::new(false);
        assert!(matches!(
            process_wav(
                &missing_input,
                &output_path,
                1000.0,
                100.0,
                &cancellation,
                |_, _| {},
            ),
            Err(WavError::InputNotFound(path)) if path == missing_input
        ));

        let input_path = test_path("errors-input");
        write_pcm16_wav(&input_path, 16);
        let existing_output = test_path("existing-output");
        std::fs::write(&existing_output, b"preserve").expect("create existing output");
        assert!(matches!(
            process_wav(
                &input_path,
                &existing_output,
                1000.0,
                100.0,
                &cancellation,
                |_, _| {},
            ),
            Err(WavError::OutputAlreadyExists(path)) if path == existing_output
        ));
        assert_eq!(
            std::fs::read(&existing_output).expect("read existing output"),
            b"preserve"
        );

        let unsupported_input = test_path("unsupported-input");
        let unsupported_spec = WavSpec {
            channels: 1,
            sample_rate: 48_000,
            bits_per_sample: 8,
            sample_format: SampleFormat::Int,
        };
        let mut writer =
            WavWriter::create(&unsupported_input, unsupported_spec).expect("create 8-bit WAV");
        writer.write_sample(0_i8).expect("write 8-bit sample");
        writer.finalize().expect("finalize 8-bit WAV");
        assert!(matches!(
            process_wav(
                &unsupported_input,
                &output_path,
                1000.0,
                100.0,
                &cancellation,
                |_, _| {},
            ),
            Err(WavError::UnsupportedFormat {
                sample_format: SampleFormat::Int,
                bits_per_sample: 8,
            })
        ));

        assert!(matches!(
            process_wav(
                &input_path,
                &output_path,
                0.0,
                100.0,
                &cancellation,
                |_, _| {},
            ),
            Err(WavError::Engine(
                tinnotus_audio_backend::AudioError::InvalidArgument
            ))
        ));

        std::fs::remove_file(input_path).expect("remove test input");
        std::fs::remove_file(existing_output).expect("remove existing output");
        std::fs::remove_file(unsupported_input).expect("remove unsupported input");
    }

    #[test]
    fn does_not_overwrite_destination_created_during_processing() {
        let input_path = test_path("raced-output-input");
        let output_path = test_path("raced-output");
        write_pcm16_wav(&input_path, 1024);
        let cancellation = AtomicBool::new(false);

        let result = process_wav(
            &input_path,
            &output_path,
            1000.0,
            100.0,
            &cancellation,
            |done, total| {
                if done == total && done > 0 {
                    std::fs::write(&output_path, b"created during processing")
                        .expect("create output during processing");
                }
            },
        );

        assert!(matches!(
            result,
            Err(WavError::OutputAlreadyExists(path)) if path == output_path
        ));
        assert_eq!(
            std::fs::read(&output_path).expect("read output created during processing"),
            b"created during processing"
        );
        std::fs::remove_file(input_path).expect("remove test input");
        std::fs::remove_file(output_path).expect("remove test output");
    }

    #[test]
    fn loads_pcm_wav_samples_for_device_playback() {
        let path = test_path("playback-input");
        let spec = WavSpec {
            channels: 1,
            sample_rate: 48_000,
            bits_per_sample: 16,
            sample_format: SampleFormat::Int,
        };
        let mut writer = WavWriter::create(&path, spec).expect("create test WAV");
        for sample in [i16::MIN, 0, i16::MAX] {
            writer.write_sample(sample).expect("write test sample");
        }
        writer.finalize().expect("finalize test WAV");

        let audio = super::load_wav_for_playback(&path).expect("decode test WAV");
        assert_eq!(audio.sample_rate, 48_000);
        assert_eq!(audio.channels, 1);
        assert_eq!(audio.samples, [-1.0, 0.0, i16::MAX as f32 / 32768.0]);

        std::fs::remove_file(path).expect("remove test WAV");
    }

    #[test]
    fn loads_and_processes_pcm24_pcm32_float32_and_multichannel_wavs() {
        let specs = [
            (
                "pcm24",
                WavSpec {
                    channels: 2,
                    sample_rate: 48_000,
                    bits_per_sample: 24,
                    sample_format: SampleFormat::Int,
                },
            ),
            (
                "pcm32",
                WavSpec {
                    channels: 2,
                    sample_rate: 48_000,
                    bits_per_sample: 32,
                    sample_format: SampleFormat::Int,
                },
            ),
            (
                "float32",
                WavSpec {
                    channels: 2,
                    sample_rate: 48_000,
                    bits_per_sample: 32,
                    sample_format: SampleFormat::Float,
                },
            ),
            (
                "six-channel",
                WavSpec {
                    channels: 6,
                    sample_rate: 48_000,
                    bits_per_sample: 24,
                    sample_format: SampleFormat::Int,
                },
            ),
        ];

        for (label, spec) in specs {
            let input_path = test_path(&format!("{label}-input"));
            let output_path = test_path(&format!("{label}-output"));
            let sample_count = usize::from(spec.channels) * 32;
            let samples: Vec<f32> = (0..sample_count)
                .map(|index| ((index % 7) as f32 - 3.0) * 0.1)
                .collect();
            write_wav(&input_path, spec, &samples);

            let audio = load_wav_for_playback(&input_path).expect("decode supported WAV");
            assert_eq!(audio.sample_rate, spec.sample_rate);
            assert_eq!(audio.channels, usize::from(spec.channels));
            assert_eq!(audio.samples.len(), sample_count);
            assert!(audio.samples.iter().all(|sample| sample.is_finite()));
            let tolerance = match spec.sample_format {
                SampleFormat::Int => 1.0 / 2_f64.powi(i32::from(spec.bits_per_sample) - 1),
                SampleFormat::Float => 1.0e-6,
            };
            for (sample, source) in audio.samples.iter().zip(&samples) {
                assert!((f64::from(*sample) - f64::from(*source)).abs() <= tolerance);
            }

            process_wav(
                &input_path,
                &output_path,
                1000.0,
                100.0,
                &AtomicBool::new(false),
                |_, _| {},
            )
            .expect("process supported WAV");
            let reader = WavReader::open(&output_path).expect("open processed WAV");
            let output_spec = reader.spec();
            assert_eq!(output_spec.channels, spec.channels);
            assert_eq!(output_spec.sample_rate, spec.sample_rate);
            assert_eq!(output_spec.bits_per_sample, spec.bits_per_sample);
            assert_eq!(output_spec.sample_format, spec.sample_format);
            drop(reader);
            let processed = load_wav_for_playback(&output_path).expect("decode processed WAV");
            assert_eq!(processed.channels, usize::from(spec.channels));
            assert_eq!(processed.samples.len(), sample_count);
            assert!(processed.samples.iter().all(|sample| sample.is_finite()));

            std::fs::remove_file(input_path).expect("remove test input");
            std::fs::remove_file(output_path).expect("remove test output");
        }
    }

    #[test]
    fn rejects_malformed_and_truncated_multichannel_wavs() {
        let malformed_path = test_path("malformed");
        std::fs::write(&malformed_path, b"not a WAV file").expect("write malformed file");
        assert!(load_wav_for_playback(&malformed_path).is_err());

        let spec = WavSpec {
            channels: 2,
            sample_rate: 48_000,
            bits_per_sample: 16,
            sample_format: SampleFormat::Int,
        };
        let truncated_path = test_path("truncated");
        let truncated_output = test_path("truncated-output");
        write_wav(&truncated_path, spec, &[0.25, -0.25, 0.5, -0.5]);
        let file = std::fs::OpenOptions::new()
            .write(true)
            .open(&truncated_path)
            .expect("open WAV for truncation");
        let truncated_length = file.metadata().expect("read WAV metadata").len() - 1;
        file.set_len(truncated_length).expect("truncate WAV data");
        drop(file);
        assert!(load_wav_for_playback(&truncated_path).is_err());
        assert!(process_wav(
            &truncated_path,
            &truncated_output,
            1000.0,
            100.0,
            &AtomicBool::new(false),
            |_, _| {},
        )
        .is_err());
        assert!(!truncated_output.exists());

        std::fs::remove_file(malformed_path).expect("remove malformed file");
        std::fs::remove_file(truncated_path).expect("remove truncated WAV");
    }
}
