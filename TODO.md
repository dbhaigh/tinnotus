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
- [ ] Add long-duration and concurrent producer/consumer tests for the SPSC buffer.
- [ ] Add sanitizer coverage where supported.
- [ ] Benchmark processing at supported sample rates, channel counts, and notch counts.

## Priority 2: Integrate the Rust application

- [x] Replace missing Cargo workspace members and targets with the Rust application and backend package.
- [x] Add a typed Rust wrapper around the C ABI, including explicit queue-full handling.
- [x] Implement a bounded-block WAV processing workflow and connect it to the egui UI.
- [x] Build and test the Rust workspace with `cargo test --workspace` on Windows.
- [ ] Manually verify the interactive UI and broaden WAV format and error-path tests.
- [ ] Validate the Rust workspace on macOS and Linux.

## Priority 3: Implement one real device path

- [ ] Choose one desktop API and implement device setup, format negotiation, start/stop, and errors.
- [ ] Preallocate all callback resources and keep locks, allocation, file I/O, and logging out of callbacks.
- [ ] Connect input or file playback through the engine to output.
- [ ] Test sustained playback, device changes, underruns, and shutdown behavior.

## Priority 4: Expand based on measurements

- [ ] Establish repeatable latency, CPU, and underrun benchmarks with documented hardware and settings.
- [ ] Optimize only measured bottlenecks and add regression thresholds.
- [ ] Add additional desktop platforms after the first path passes the same tests.
- [ ] Implement pitch detection and visualization only with accuracy and usability tests.
- [ ] Keep product and medical-adjacent claims appropriately qualified.
