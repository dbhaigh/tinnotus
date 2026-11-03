# Platform abstraction prototypes

The headers in this directory support older platform-specific C++ device experiments under `backend/{windows,macos,linux}`. They are not included by the active build. Output-only WAV playback uses the shared Rust CPAL adapter in `backend/audio_device.rs` on Windows, macOS, and Linux; macOS/Linux endpoint validation remains pending. Microphone capture and automatic device recovery are not implemented.
