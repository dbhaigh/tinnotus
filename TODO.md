# Tinnotus Development Roadmap

The previous checklist described unverified platform and UI features as complete. This roadmap tracks the current implementation and remaining acceptance criteria.

## Completed foundation

- [x] Establish a portable C++17 processing target with CMake.
- [x] Implement a stable notch filter with independent state per channel.
- [x] Add bounded, preallocated single-producer/single-consumer audio and control queues.
- [x] Expose a C ABI with explicit invalid-input and queue-full errors.
- [x] Add automated tests for DSP response, channel separation, queue behavior, and the C ABI.

## Priority 1: Verify the native core

- [x] Build the native target and run its test executable in the current Windows workspace.
- [ ] Configure, build, and run the native tests on macOS and Linux.
- [x] Add long-duration and concurrent producer/consumer tests for the SPSC buffer and command queue.
- [x] Add sanitizer coverage where supported.
- [x] Benchmark processing across sample rates, channel counts, notch counts, and block sizes; document the hardware and settings.
- [x] Configure GitHub Actions for Release native/Rust checks on Windows, macOS, and Linux, with sanitizer jobs where supported.
- [ ] Verify the hosted cross-platform CI matrix passes on all supported operating systems.

## Priority 2: Integrate the Rust application

- [x] Replace missing Cargo workspace members and targets with the Rust application and backend package.
- [x] Add a typed Rust wrapper around the C ABI, including explicit queue-full handling.
- [x] Implement a bounded-block WAV processing workflow and connect it to the egui UI.
- [x] Build and test the Rust workspace with `cargo test --workspace` on Windows.
- [x] Prevent a destination created during WAV processing from being overwritten at commit; cover the race with a regression test.
- [x] Add WAV tests for PCM 24/32-bit, float32, multichannel, and malformed or incomplete files.
- [ ] Manually verify the interactive UI and playback error paths.
- [ ] Validate the Rust workspace on macOS and Linux.

## Priority 3: Implement one real device path

- [x] Implement CPAL output enumeration, format selection, start/stop, and stream errors (WASAPI shared mode on Windows).
- [x] Preallocate callback resources and keep locks, allocation, file I/O, and logging out of callbacks.
- [x] Play decoded WAV samples through `AudioEngine` and expose live notch updates.
- [x] Add a headless callback test covering 30 seconds of audio data.
- [x] Add opt-in endpoint tests for default-device completion/live notch changes and repeated selected-device start/stop; validate on the current Windows host.
- [x] Document the real-time contract and add a CI source guard for known callback and processing regions.
- [ ] Manually test real-device start/stop, notch changes, unplug/reconnect, and high CPU load.
- [x] Replace linear interpolation with band-limited resampling and define/test conservative channel-count conversion; speaker-layout-aware surround mixing remains unsupported without channel masks.

## Priority 4: Expand based on measurements

- [x] Establish a repeatable native processing benchmark with documented hardware and settings.
- [ ] Measure end-to-end device latency, CPU use, and underruns with real hardware.
- [x] Add a Windows Release CTest gate requiring native-engine p99 processing time to stay below 25% of block deadlines for representative and worst-supported cases.
- [ ] Optimize only measured bottlenecks; the current native benchmark shows ample headroom, while device-level measurements remain pending.
- [x] Add a portable CPAL output-only path for macOS/Linux, reusing the Windows playback behavior and callback tests.
- [ ] Validate the macOS/Linux build matrix and physical output endpoints before claiming those paths as verified.
- [x] Add bounded YIN pitch detection and a playback UI estimate with synthetic-tone, noise, and silence tests.
- [ ] Manually validate pitch estimates and UI behavior with representative real WAV files.
- [x] Add smoothed filter-coefficient transitions and signal tests for update discontinuities; manual listening remains pending.
- [x] Audit active user-facing product claims and keep them appropriately qualified; excluded legacy prototypes are not shipped.
