pub mod audio_file;

pub use audio_file::{load_wav_for_playback, process_wav, WavAudio, WavError};
pub use tinnotus_audio_backend::{
    detect_pitch, AudioEngine, AudioEngineControl, AudioEngineError, AudioEngineProcessor,
    AudioError, PitchEstimate,
};
#[cfg(target_os = "windows")]
pub use tinnotus_audio_backend::{
    enumerate_output_devices, AudioDeviceError, AudioPlaybackStatus, OutputDeviceInfo,
    WindowsAudioPlayback,
};
