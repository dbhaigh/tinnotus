use std::ffi::c_int;
use std::fmt;
use std::ptr::NonNull;

#[repr(C)]
struct RawAudioEngine {
    _private: [u8; 0],
}

extern "C" {
    fn tinnotus_audio_engine_create(
        sample_rate: c_int,
        channels: usize,
    ) -> *mut RawAudioEngine;
    fn tinnotus_audio_engine_destroy(engine: *mut RawAudioEngine);
    fn tinnotus_audio_engine_set_notch(
        engine: *mut RawAudioEngine,
        index: usize,
        center_frequency_hz: f64,
        bandwidth_hz: f64,
    ) -> c_int;
    fn tinnotus_audio_engine_remove_notch(
        engine: *mut RawAudioEngine,
        index: usize,
    ) -> c_int;
    fn tinnotus_audio_engine_process(
        engine: *mut RawAudioEngine,
        input: *const f32,
        output: *mut f32,
        frame_count: usize,
    ) -> c_int;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioEngineError {
    InvalidConfiguration,
    InvalidArgument,
    QueueFull,
}

impl fmt::Display for AudioEngineError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidConfiguration => formatter.write_str("invalid audio-engine configuration"),
            Self::InvalidArgument => formatter.write_str("invalid audio-engine argument"),
            Self::QueueFull => formatter.write_str("audio-engine control queue is full"),
        }
    }
}

impl std::error::Error for AudioEngineError {}

pub struct AudioEngine {
    handle: NonNull<RawAudioEngine>,
    channels: usize,
}

impl AudioEngine {
    pub fn new(sample_rate: u32, channels: usize) -> Result<Self, AudioEngineError> {
        let sample_rate =
            c_int::try_from(sample_rate).map_err(|_| AudioEngineError::InvalidConfiguration)?;
        let handle = unsafe { tinnotus_audio_engine_create(sample_rate, channels) };
        let handle = NonNull::new(handle).ok_or(AudioEngineError::InvalidConfiguration)?;
        Ok(Self { handle, channels })
    }

    pub fn set_notch(
        &mut self,
        index: usize,
        center_frequency_hz: f64,
        bandwidth_hz: f64,
    ) -> Result<(), AudioEngineError> {
        let status = unsafe {
            tinnotus_audio_engine_set_notch(
                self.handle.as_ptr(),
                index,
                center_frequency_hz,
                bandwidth_hz,
            )
        };
        map_status(status)
    }

    pub fn remove_notch(&mut self, index: usize) -> Result<(), AudioEngineError> {
        let status =
            unsafe { tinnotus_audio_engine_remove_notch(self.handle.as_ptr(), index) };
        map_status(status)
    }

    pub fn process_interleaved(
        &mut self,
        input: &[f32],
        output: &mut [f32],
    ) -> Result<(), AudioEngineError> {
        if input.len() != output.len() || input.len() % self.channels != 0 {
            return Err(AudioEngineError::InvalidArgument);
        }

        let status = unsafe {
            tinnotus_audio_engine_process(
                self.handle.as_ptr(),
                input.as_ptr(),
                output.as_mut_ptr(),
                input.len() / self.channels,
            )
        };
        map_status(status)
    }
}

impl Drop for AudioEngine {
    fn drop(&mut self) {
        unsafe { tinnotus_audio_engine_destroy(self.handle.as_ptr()) };
    }
}

fn map_status(status: c_int) -> Result<(), AudioEngineError> {
    match status {
        0 => Ok(()),
        -2 => Err(AudioEngineError::QueueFull),
        _ => Err(AudioEngineError::InvalidArgument),
    }
}

#[cfg(test)]
mod tests {
    use super::{AudioEngine, AudioEngineError};

    #[test]
    fn processes_and_filters_interleaved_audio() {
        let mut engine = AudioEngine::new(48_000, 1).expect("valid engine");
        engine.set_notch(0, 1000.0, 100.0).expect("valid notch");
        let input = vec![0.0_f32; 128];
        let mut output = vec![1.0_f32; input.len()];

        engine
            .process_interleaved(&input, &mut output)
            .expect("valid block");
        assert!(output.iter().all(|sample| *sample == 0.0));
    }

    #[test]
    fn rejects_incomplete_interleaved_frames() {
        let mut engine = AudioEngine::new(48_000, 2).expect("valid engine");
        let input = [0.0_f32; 3];
        let mut output = [0.0_f32; 3];

        assert_eq!(
            engine.process_interleaved(&input, &mut output),
            Err(AudioEngineError::InvalidArgument)
        );
    }
}
