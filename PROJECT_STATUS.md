# Tinnotus Project Status

**Updated:** 2026-11-02  
**Version:** 0.0.0.1  
**Status:** Experimental audio core and WAV workflow; Windows tests pass

## Implemented

- Portable C++17 `AudioEngine` with a stable biquad notch response.
- Interleaved processing for one to eight channels, with independent filter state per channel.
- Fixed-capacity, single-producer/single-consumer command handoff for notch updates.
- Preallocated single-producer/single-consumer sample buffer.
- C ABI for engine creation, notch changes, block processing, and destruction.
- CMake target and native tests for filter response, passband preservation, channel isolation, buffer wraparound, input validation, and ABI behavior.
- Rust C ABI wrapper and egui WAV-processing workflow for PCM 16/24/32-bit and IEEE float 32-bit files.

The processing path is designed to avoid allocations and locks. This is a code-level property, not a measured latency or performance claim.

The native CTest suite and `cargo test --workspace` pass in the current Windows workspace. Rust builds on macOS and Linux and manual UI verification remain outstanding.

Cargo requires three-component SemVer, so Rust package manifests use `0.0.0+1` to encode product version `0.0.0.1`; Cargo treats `+1` as build metadata.

## Not implemented

- No device enumeration, audio capture, or playback is integrated on Windows, macOS, or Linux. Files in the platform-specific directories are prototypes and are excluded from the CMake target.
- Pitch detection is not implemented.
- No latency, CPU, underrun, or long-duration stability benchmarks have been run.
- This project is not a medical device or treatment and makes no claim of tinnitus relief.

## Current build

The root CMake project builds the portable `tinnotus_backend` library and its tests; the native test executable passes in the current Windows workspace. `cargo test --workspace` also passes on Windows. Neither build requires platform audio SDKs. See [README.md](./README.md) and [backend/README.md](./backend/README.md) for the verified scope and commands.

## Next milestones

1. Manually verify the UI and broaden WAV format and error-path tests.
2. Implement one desktop device path and connect its callback to `AudioEngine`.
3. Validate builds across macOS and Linux, then add broader end-to-end UI tests.
4. Measure latency, CPU use, and underruns; publish support claims only after repeatable results.
