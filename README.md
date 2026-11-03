# Tinnotus

Tinnotus is an experimental audio-processing project exploring frequency-specific notch filtering. It is not a medical device or treatment, does not diagnose tinnitus, and does not promise symptom relief. Keep audio at a comfortable, safe listening level and consult a qualified hearing professional about health concerns.

## Versioning

The product version is `0.0.0.1`. Cargo requires SemVer rather than four numeric components, so Rust package manifests use `0.0.0+1`; Cargo treats `+1` as build metadata.

## Current implementation

The verified native build is a portable C++ audio-processing core:

- Stable second-order notch filters with independent state for each channel.
- Interleaved audio processing for one to eight channels.
- A fixed-capacity control queue for filter changes; queue overflow is reported.
- A preallocated single-producer/single-consumer audio buffer.
- A small C ABI for creating an engine, configuring notches, and processing blocks.
- Tests for notch response, passband preservation, channel isolation, buffer wraparound, invalid inputs, and C ABI behavior.

The Rust application now wraps the C ABI and provides an egui workflow to process WAV files in bounded blocks. It supports PCM 16/24/32-bit and IEEE float 32-bit files, and does not overwrite an existing output file. The Rust workspace builds and its tests pass on Windows; CMake builds only the native core, and Rust validation on other platforms remains outstanding. Pitch detection and hardware playback/recording are not implemented. Platform adapter files are prototypes, and no latency or CPU-performance figures have been measured.

## Build and test the native core

Requirements: CMake 3.16 or later and a C++17 compiler.

```powershell
cmake -S . -B out/build -DTINNOTUS_BUILD_TEST=ON
cmake --build out/build --config Release
ctest --test-dir out/build -C Release --output-on-failure
```

The build produces the `tinnotus_backend` library and the `tinnotus_audio_engine` test. Platform-specific audio SDKs are not required for this core target.

## Rust application

The Rust app requires a Rust toolchain, CMake, and a C++17 compiler. From the repository root:

```powershell
cargo test --workspace
cargo run
```

`cargo test --workspace` passes in the current Windows workspace. Launching the interactive UI is not part of that test run.
The Cargo workspace contains the application and backend packages; the legacy `shared/Cargo.toml` is excluded. Its native C++ sources are built through CMake.

## Audio-engine contract

`tinnotus::audio::AudioEngine` is configured with a sample rate and channel count (1–8). Control changes are submitted from one producer thread and applied by `process` on one audio thread. Processing uses fixed filter state and does not allocate or acquire locks. Configuration requests use a bounded queue and return an explicit error when invalid or full.

`SpscAudioBuffer` is a preallocated single-producer/single-consumer queue measured in samples. Its caller must preserve the one-writer/one-reader contract.

The C-compatible declarations are in `shared/audio_engine_ffi.h`. The C ABI uses `TINNOTUS_AUDIO_OK`, `TINNOTUS_AUDIO_INVALID_ARGUMENT`, and `TINNOTUS_AUDIO_QUEUE_FULL`; callers must handle failures rather than assume a request succeeded.

## Next milestones

1. Manually verify the interactive UI and broaden WAV format and error-path tests.
2. Implement and validate a single desktop device path with callback-safe preallocation.
3. Validate native and Rust builds across supported platforms.
4. Measure latency, CPU use, and underruns before publishing performance claims or expanding platform support.
