# Windows device prototypes

The C++ device files in this folder are unbuilt prototypes and are not part of the root CMake target. The active output-only playback path is the shared Rust CPAL adapter in [../audio_device.rs](../audio_device.rs); CPAL uses WASAPI shared mode on Windows. Capture and automatic recovery are not implemented.
