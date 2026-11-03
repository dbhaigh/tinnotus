# Platform abstraction prototypes

The headers in this directory support older platform-specific C++ device experiments under `backend/{windows,macos,linux}`. They are not included by the active build. Windows playback currently uses the Rust CPAL adapter in `backend/windows/audio_device.rs`; other live-device paths are not implemented.
