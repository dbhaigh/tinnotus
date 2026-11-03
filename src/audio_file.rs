use hound::{SampleFormat, WavReader, WavWriter};
use std::fs::{self, File, OpenOptions};
use std::io::ErrorKind;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use tinnotus_audio_backend::{AudioEngine, AudioEngineError};

const BLOCK_FRAMES: usize = 1024;
static TEMPORARY_FILE_SEQUENCE: AtomicU64 = AtomicU64::new(0);

struct TemporaryOutput {
    temporary_path: PathBuf,
    output_path: PathBuf,
    committed: bool,
}

impl TemporaryOutput {
    fn create(output_path: &Path) -> Result<(Self, File), String> {
        if output_path.exists() {
            return Err("The output file already exists; choose a different path".to_owned());
        }

        let parent = output_path
            .parent()
            .filter(|path| !path.as_os_str().is_empty())
            .unwrap_or_else(|| Path::new("."));
        let file_name = output_path
            .file_name()
            .ok_or_else(|| "The output path must include a file name".to_owned())?
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
                Err(error) => {
                    return Err(format!("Could not create temporary output: {error}"));
                }
            }
        }

        Err("Could not reserve a unique temporary output file".to_owned())
    }

    fn commit(mut self) -> Result<(), String> {
        fs::rename(&self.temporary_path, &self.output_path)
            .map_err(|error| format!("Could not finalize output file: {error}"))?;
        self.committed = true;
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

pub fn process_wav(
    input_path: &Path,
    output_path: &Path,
    center_frequency_hz: f64,
    bandwidth_hz: f64,
) -> Result<(), String> {
    let mut reader = WavReader::open(input_path)
        .map_err(|error| format!("Could not open input WAV: {error}"))?;
    let spec = reader.spec();
    if spec.channels == 0 {
        return Err("The input WAV has no channels".to_owned());
    }

    let channels = usize::from(spec.channels);
    let mut engine = AudioEngine::new(spec.sample_rate, channels)
        .map_err(|error| format_engine_error(error))?;
    engine
        .set_notch(0, center_frequency_hz, bandwidth_hz)
        .map_err(format_engine_error)?;

    let (temporary_output, file) = TemporaryOutput::create(output_path)?;
    let mut writer = WavWriter::new(file, spec)
        .map_err(|error| format!("Could not create output WAV: {error}"))?;

    match spec.sample_format {
        SampleFormat::Int => {
            if !matches!(spec.bits_per_sample, 16 | 24 | 32) {
                return Err(format!(
                    "Unsupported PCM bit depth: {}",
                    spec.bits_per_sample
                ));
            }

            let scale = (1_i64 << (spec.bits_per_sample - 1)) as f64;
            process_samples(
                reader.samples::<i32>(),
                channels,
                &mut engine,
                |sample| Ok(sample as f32 / scale as f32),
                |sample| {
                    if !sample.is_finite() {
                        return Err("Audio processing produced a non-finite sample".to_owned());
                    }
                    let encoded = (sample as f64 * scale)
                        .round()
                        .clamp(-scale, scale - 1.0) as i32;
                    writer
                        .write_sample(encoded)
                        .map_err(|error| format!("Could not write output WAV: {error}"))
                },
            )?;
        }
        SampleFormat::Float => {
            if spec.bits_per_sample != 32 {
                return Err(format!(
                    "Unsupported floating-point bit depth: {}",
                    spec.bits_per_sample
                ));
            }

            process_samples(
                reader.samples::<f32>(),
                channels,
                &mut engine,
                |sample| {
                    if sample.is_finite() {
                        Ok(sample)
                    } else {
                        Err("Input WAV contains a non-finite sample".to_owned())
                    }
                },
                |sample| {
                    if !sample.is_finite() {
                        return Err("Audio processing produced a non-finite sample".to_owned());
                    }
                    writer
                        .write_sample(sample)
                        .map_err(|error| format!("Could not write output WAV: {error}"))
                },
            )?;
        }
    }

    writer
        .finalize()
        .map_err(|error| format!("Could not finalize output WAV: {error}"))?;
    temporary_output.commit()
}

fn process_samples<T>(
    samples: impl Iterator<Item = Result<T, hound::Error>>,
    channels: usize,
    engine: &mut AudioEngine,
    mut decode: impl FnMut(T) -> Result<f32, String>,
    mut write: impl FnMut(f32) -> Result<(), String>,
) -> Result<(), String> {
    let block_samples = BLOCK_FRAMES
        .checked_mul(channels)
        .ok_or_else(|| "Audio block size overflow".to_owned())?;
    let mut input = vec![0.0_f32; block_samples];
    let mut output = vec![0.0_f32; block_samples];
    let mut samples = samples;

    loop {
        let mut count = 0;
        while count < block_samples {
            match samples.next() {
                Some(Ok(sample)) => {
                    input[count] = decode(sample)?;
                    count += 1;
                }
                Some(Err(error)) => {
                    return Err(format!("Could not read input WAV samples: {error}"));
                }
                None => break,
            }
        }

        if count == 0 {
            break;
        }
        if count % channels != 0 {
            return Err("Input WAV ends with an incomplete audio frame".to_owned());
        }

        engine
            .process_interleaved(&input[..count], &mut output[..count])
            .map_err(format_engine_error)?;
        for sample in &output[..count] {
            write(*sample)?;
        }
    }

    Ok(())
}

fn format_engine_error(error: AudioEngineError) -> String {
    format!("Audio engine error: {error}")
}

#[cfg(test)]
mod tests {
    use super::process_wav;
    use hound::{SampleFormat, WavReader, WavSpec, WavWriter};
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicU64, Ordering};

    static TEST_FILE_SEQUENCE: AtomicU64 = AtomicU64::new(0);

    #[test]
    fn processes_pcm_wav_in_bounded_blocks() {
        let sequence = TEST_FILE_SEQUENCE.fetch_add(1, Ordering::Relaxed);
        let base = format!("tinnotus-wav-test-{}-{sequence}", std::process::id());
        let input_path = PathBuf::from(std::env::temp_dir()).join(format!("{base}-input.wav"));
        let output_path = PathBuf::from(std::env::temp_dir()).join(format!("{base}-output.wav"));
        let spec = WavSpec {
            channels: 1,
            sample_rate: 48_000,
            bits_per_sample: 16,
            sample_format: SampleFormat::Int,
        };

        let mut writer = WavWriter::create(&input_path, spec).expect("create test input");
        for frame in 0..48_000 {
            let sample = (0.5 * (2.0 * std::f64::consts::PI * 1000.0 * frame as f64
                / f64::from(spec.sample_rate))
                .sin()
                * f64::from(i16::MAX)) as i16;
            writer.write_sample(sample).expect("write test sample");
        }
        writer.finalize().expect("finalize test input");

        process_wav(&input_path, &output_path, 1000.0, 100.0)
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
        assert!(output_rms / input_rms < 0.1);

        std::fs::remove_file(input_path).expect("remove test input");
        std::fs::remove_file(output_path).expect("remove test output");
    }
}
