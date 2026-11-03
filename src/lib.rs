pub mod audio_file;

pub use audio_file::{load_wav_for_playback, process_wav, WavAudio, WavError};
pub use tinnotus_audio_backend::{
    detect_pitch, AudioEngine, AudioEngineControl, AudioEngineError, AudioEngineProcessor,
    AudioError, PitchEstimate,
};
#[cfg(any(target_os = "windows", target_os = "macos", target_os = "linux"))]
pub use tinnotus_audio_backend::{
    enumerate_output_devices, AudioDeviceError, AudioPlayback, AudioPlaybackStatus,
    OutputDeviceInfo,
};

#[cfg(target_os = "windows")]
pub use tinnotus_audio_backend::WindowsAudioPlayback;
