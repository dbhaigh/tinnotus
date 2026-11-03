# Tinnotus

[![CI](https://github.com/dbhaigh/tinnotus/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/dbhaigh/tinnotus/actions/workflows/ci.yml)

Tinnotus is an experimental audio-processing project exploring frequency-specific notch filtering. It is not a medical device or treatment, does not diagnose tinnitus, and does not promise symptom relief. Keep audio at a comfortable, safe listening level and consult a qualified hearing professional about health concerns.

## Versioning

The product version is `0.0.0.2`. The Cargo workspace keeps its Rust package version in `[workspace.package]` and both active packages inherit `0.0.0+2`; Cargo requires SemVer and treats `+2` as build metadata. CMake uses the four-component product version. The legacy, excluded `shared/Cargo.toml` is kept on the same package version for consistency. The mapping is documented here rather than introducing a second generated version file.

## Current capabilities

The verified native build is a portable C++ audio-processing core:

- Stable second-order notch filters with independent state for each channel.
- Interleaved audio processing for one to eight channels.
- A fixed-capacity control queue for filter changes; queue overflow is reported.
- A preallocated single-producer/single-consumer audio buffer.
- A small C ABI for creating an engine, configuring notches, and processing blocks.
- Tests for notch response, passband preservation, channel isolation, buffer edge cases and wraparound, invalid inputs, C ABI behavior, and sustained concurrent queue use.

The Rust application wraps the C ABI and provides bounded-block WAV processing with frame progress and cancellation, plus Windows output-only WAV playback through shared-mode WASAPI. Cancelled offline processing stops between blocks and does not commit its temporary output. During playback, a separate YIN analysis worker estimates source pitch and confidence without running pitch analysis in the audio callback. Playback enumerates output devices, prefers float32, and converts the preloaded WAV to the selected device format before starting the stream. Capture and live-device support on macOS/Linux are not implemented. GitHub Actions is configured to run CMake/CTest and Rust test/release-build jobs on Windows, macOS, and Linux, plus Debug sanitizer jobs where supported and a real-time source guard. Hosted cross-platform validation remains pending. The native benchmark measures processing performance, not end-to-end device latency.

## Build and test the native core

Requirements: CMake 3.16 or later and a C++17 compiler.

```powershell
cmake -S . -B out/build -DTINNOTUS_BUILD_TEST=ON
cmake --build out/build --config Release
ctest --test-dir out/build -C Release --output-on-failure
```

The build produces the `tinnotus_backend` library and the `tinnotus_audio_engine` unit test plus a sustained concurrent stress test. Platform-specific audio SDKs are not required for this core target.

The concurrent stress test runs each producer/consumer scenario for 30 seconds by default. Set `TINNOTUS_AUDIO_STRESS_SECONDS` to an integer from 1 to 120 to change the duration per scenario. For example:

```powershell
$env:TINNOTUS_AUDIO_STRESS_SECONDS = "60"
ctest --test-dir out/build -C Release -L stress --output-on-failure
Remove-Item Env:TINNOTUS_AUDIO_STRESS_SECONDS
```

Sanitizers can be enabled in separate build directories. AddressSanitizer and UndefinedBehaviorSanitizer may be combined; AddressSanitizer and ThreadSanitizer cannot. MSVC supports AddressSanitizer; the UBSan and TSan flags require GCC or Clang, and TSan is unavailable on Windows.

```powershell
cmake -S . -B out/build-asan -DTINNOTUS_ENABLE_ASAN=ON
cmake --build out/build-asan --config Debug
ctest --test-dir out/build-asan -C Debug --output-on-failure

cmake -S . -B out/build-tsan -DTINNOTUS_ENABLE_TSAN=ON
cmake --build out/build-tsan --config Debug
ctest --test-dir out/build-tsan -C Debug --output-on-failure

cmake -S . -B out/build-ubsan -DTINNOTUS_ENABLE_UBSAN=ON
cmake --build out/build-ubsan --config Debug
ctest --test-dir out/build-ubsan -C Debug --output-on-failure
```

## Benchmark the native engine

Configure once with the benchmark option, then build the `bench` target in Release mode:

```powershell
cmake -S . -B out/bench -DTINNOTUS_BUILD_BENCH=ON
cmake --build out/bench --config Release --target bench
```

The target runs all 450 combinations of sample rate, channel count, active notches, and block size. It warms each case for 300 blocks, collects three batches of 2,000 per-block latency samples (6,000 total), and performs three independent throughput passes of at least 50 ms each (extending short passes in batches). Min, average, and p99 use the pooled latency samples; p99 uses the nearest-rank percentile. Throughput and its single-thread CPU-time estimate come from the median-rate pass, based on elapsed wall time and including scheduler delays. On Windows, the benchmark thread is pinned to the lowest available logical processor; other platforms leave affinity to the OS. Results are written to `docs/benchmarks/YYYY-MM-DD-<platform>.md`. The executable also accepts `--warmup`, `--iterations`, and `--output-dir` for custom runs; `--iterations` sets latency samples per batch and the minimum block count per throughput pass, while the 50 ms floor still applies. `--help` lists defaults. Use Release builds and avoid heavy background load when comparing runs.

## Rust application

The Rust app requires a Rust toolchain, CMake, and a C++17 compiler. From the repository root:

```powershell
cargo test --workspace
cargo run
```

`cargo test --workspace` passes in the current Windows workspace. Launching the interactive UI is not part of that test run.
The Cargo workspace contains the application and backend packages; the legacy `shared/Cargo.toml` is excluded. Its native C++ sources are built through CMake.

On Windows, the application also offers live WAV playback to a selected output device. The playback callback runs the native engine in WASAPI shared mode, and the WAV is decoded and converted before the stream starts. See [ARCHITECTURE.md](./ARCHITECTURE.md) for callback rules, failure reporting, and the manual device checklist. A headless callback test does not replace physical-device validation.

See the [real-time safety rules](./docs/REALTIME_RULES.md) for the callback contract and the source guard's scope.

## Build release executables

These commands compile the native library, native tests, benchmark executable, and Rust application without running the long benchmark matrix:

```powershell
cmake -S . -B out/build-release -DTINNOTUS_BUILD_TEST=ON -DTINNOTUS_BUILD_BENCH=ON
cmake --build out/build-release --config Release --parallel
cargo build --workspace --release --locked
```

The Rust desktop application is written to `target/release/tinnotus.exe` on Windows. Native binaries are written under `out/build-release` (the exact subdirectory depends on the CMake generator). To run the native tests, use the `ctest` command from [Build and test the native core](#build-and-test-the-native-core).

## Audio-engine contract

`tinnotus::audio::AudioEngine` is configured with a sample rate and channel count (1–8). Control changes are submitted from one producer thread and applied by `process` on one audio thread. Processing uses fixed filter state and does not allocate or acquire locks. Configuration requests use a bounded queue and return an explicit error when invalid or full.

`SpscAudioBuffer` is a preallocated single-producer/single-consumer queue measured in samples. Its caller must preserve the one-writer/one-reader contract.

The C-compatible declarations are in `shared/audio_engine_ffi.h`. The C ABI uses `TINNOTUS_AUDIO_OK`, `TINNOTUS_AUDIO_INVALID_ARGUMENT`, and `TINNOTUS_AUDIO_QUEUE_FULL`; callers must handle failures rather than assume a request succeeded.

## Roadmap

1. Manually verify Windows playback, live notch changes, device unplug/reconnect, and high-load behavior.
2. Verify the hosted native and Rust build matrix on Windows, macOS, and Linux.
3. Validate pitch estimates and UI behavior with representative real WAV files.
4. Measure end-to-end device latency, CPU use, and underruns before publishing performance claims.
5. Add capture and additional desktop device paths after Windows playback validation.
