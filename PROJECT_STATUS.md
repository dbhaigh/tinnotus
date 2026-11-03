# Tinnotus Project Status

**Updated:** 2026-11-03
**Version:** 0.0.0.2
**Status:** Experimental audio core and cross-platform CPAL output path; macOS/Linux endpoint validation pending

## Implemented

- Portable C++17 `AudioEngine` with a stable biquad notch response.
- Interleaved processing for one to eight channels, with independent filter state per channel.
- Fixed-capacity, single-producer/single-consumer command handoff for notch updates.
- Preallocated single-producer/single-consumer sample buffer.
- C ABI for engine creation, notch changes, block processing, and destruction.
- CMake target and native tests for filter response, passband preservation, channel isolation, buffer wraparound, input validation, and ABI behavior.
- Native benchmark covering 450 sample-rate, channel, notch-count, and block-size combinations; the current Windows report is [2026-11-03-windows-x86_64-msvc.md](./docs/benchmarks/2026-11-03-windows-x86_64-msvc.md). Windows Release CI also checks native-engine p99 time against a 25% block-deadline budget for representative and worst-supported cases.
- GitHub Actions configured for Release native/Rust checks on Windows, macOS, and Linux, plus supported Debug sanitizer jobs.
- Rust C ABI wrapper and egui WAV-processing workflow for PCM 16/24/32-bit and IEEE float 32-bit files.
- CPAL output-device enumeration and WAV playback with pre-start sample conversion and a callback-safe engine/control split; Windows uses WASAPI shared mode.
- A 32-tap, 1024-phase Blackman-windowed-sinc playback resampler and conservative, tested channel-count conversion.
- Immediate initial notch configuration and twenty-millisecond live coefficient transitions that preserve state during updates and clear it after removal completes.
- A documented real-time safety contract and a CI source guard for the native processing path and CPAL output callback.
- Bounded YIN pitch detection in the native core, C ABI, and Rust API; Windows playback reports frequency and confidence from a throttled analysis thread outside the audio callback.
- A headless callback test that processes 30 seconds of audio data without requiring a physical audio endpoint.
- Synthetic-tone accuracy tests and low-confidence noise/silence tests for the pitch API and playback analyzer.
- An optional timed virtual-null-output run; the CI matrix is configured to exercise it for 30 seconds on Windows, macOS, and Linux.
- Opt-in silent endpoint smoke tests for default-device completion/live notch updates and repeated selected-device start/stop; both pass on the current Windows host.

The processing path is designed to avoid allocations and locks. This is a code-level property, not a measured device-latency or performance claim. The native-engine regression gate does not include CPAL callback conversion or hardware scheduling.

The native CTest suite, including the Windows Release performance-budget smoke test, `cargo test --workspace`, and both opt-in endpoint smoke tests pass in the current Windows workspace. The existing MSVC benchmark report's worst p99 is 0.675% of its block deadline across 450 cases; the 25% CI limit leaves substantial room for runner variation. The hosted [cross-platform CI run](https://github.com/dbhaigh/tinnotus/actions/runs/41708350985) passed all configured Windows/macOS/Linux Release, Rust, real-time guard, and sanitizer jobs. That confirms automated build/test coverage, not audible output or device behavior. macOS/Linux endpoint tests and manual playback, hotplug, high-load, and end-to-end measurement remain outstanding.
The playback UI reports callback-cadence misses as a possible-underrun indicator and displays the latest pitch estimate when available. The indicator uses CPAL callback timestamps; it is not an execution-time measurement or hardware underrun counter. Pitch behavior still needs manual verification with representative real WAVs. No real-device latency, device CPU, or hardware underrun measurements have been run. The native benchmark reports engine processing-call latency, throughput, and an elapsed-time CPU estimate.

Cargo requires three-component SemVer, so the workspace package version `0.0.0+2` encodes product version `0.0.0.2`; both active Rust packages inherit the workspace version, and Cargo treats `+2` as build metadata. The excluded legacy `shared/Cargo.toml` carries the same version but is not part of the active Cargo workspace.

## Not implemented

- Microphone capture is not implemented. The output-only WAV path is wired for Windows, macOS, and Linux, but only Windows has been tested against an active output endpoint; device removal is reported, but recovery requires refreshing/selecting a device and restarting.
- Speaker-layout-aware surround mixing is not implemented because the WAV decoder and CPAL configuration expose channel counts but no speaker masks. Count mismatches preserve common channel order, drop unmatched input channels, and silence-fill unmatched output channels. Smooth coefficient transitions are covered by signal tests, but audible playback still requires manual verification.
- No real-device latency, CPU, or sustained-playback measurements have been run.
- This project is not a medical device or treatment and makes no claim of tinnitus relief.

## Current build

The root CMake project builds the portable `tinnotus_backend` library and its tests; the native test executable passes in the current Windows workspace. `cargo test --workspace` also passes on Windows. Linux CI installs ALSA development headers for CPAL; macOS and Windows use their platform audio hosts. See [README.md](./README.md), [ARCHITECTURE.md](./ARCHITECTURE.md), [backend/README.md](./backend/README.md), and [docs/REALTIME_RULES.md](./docs/REALTIME_RULES.md) for the threading rules, source guard, and scope.

## Roadmap

1. Manually verify Windows playback, live notch changes, unplug/reconnect, and high-load behavior.
2. Validate builds across macOS and Linux, then add broader end-to-end UI tests.
3. Measure end-to-end device latency, CPU use, and underruns; publish support claims only after repeatable real-device results.
4. Add microphone capture and other platform paths after the Windows playback path is validated.
