# Tinnotus Native Audio Core

The current CMake target builds the portable processing engine in `shared/audio_engine.cpp` and its C ABI. It does not build the platform-specific device adapters under `windows/`, `macos/`, or `linux/`; those files are prototypes and are not yet connected to this engine.

## Build and test

From the repository root:

```powershell
cmake -S . -B build -DTINNOTUS_BUILD_TEST=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The core requires CMake 3.16 or later and a C++17 compiler. It has no platform audio SDK dependency.

## Threading and processing

Configure notches from one control thread with `AudioEngine::setNotch` or `removeNotch`; process blocks from one audio thread. Updates are handed off through a bounded single-producer/single-consumer queue and are applied at the start of a processing call. A full queue is reported to the caller.

`AudioEngine::process` performs no allocation and takes no locks. `SpscAudioBuffer` also uses preallocated storage and is bounded in samples, not frames. Its single-producer/single-consumer restriction must be respected.

## Rust application status

The Rust backend package wraps the C ABI, and the root egui application provides bounded-block WAV processing for PCM 16/24/32-bit and IEEE float 32-bit files. These Rust targets are not built by CMake; `cargo test --workspace` passes in the current Windows workspace, while validation on other platforms remains outstanding.

Device enumeration, capture, playback, and pitch detection are not implemented. Real-time latency, CPU, and underrun benchmarks have not been measured.
