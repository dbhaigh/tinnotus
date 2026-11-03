# Tinnotus Audio Backend

The CMake target builds the portable processing engine in `shared/audio_engine.cpp` and its C ABI. The Rust backend wraps that engine and provides output-only WAV playback through the shared CPAL adapter in `audio_device.rs` (WASAPI shared mode on Windows). The older platform-specific C++ device files remain prototypes and are not part of the CMake target. macOS/Linux builds and endpoint checks for the current changes remain pending. See the [real-time rules](../docs/REALTIME_RULES.md) and the prototype markers in platform folders for current scope.

## Build and test

From the repository root:

```powershell
cmake -S . -B build -DTINNOTUS_BUILD_TEST=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The core requires CMake 3.16 or later and a C++17 compiler. It has no platform audio SDK dependency.
Building the Rust audio backend on Linux also requires `pkg-config` and ALSA development headers (for example, `libasound2-dev`); the CI workflow installs them.

The stress test runs its SPSC and command-queue producer/consumer scenarios for 30 seconds each by default. Set `TINNOTUS_AUDIO_STRESS_SECONDS` to an integer from 1 to 120 to change that duration. Sanitizer options `TINNOTUS_ENABLE_ASAN`, `TINNOTUS_ENABLE_TSAN`, and `TINNOTUS_ENABLE_UBSAN` instrument the library and tests; use separate build directories for each sanitizer. See the repository [README](../README.md#build-and-test-the-native-core) for example commands and supported compiler combinations.

## Benchmark

Enable `TINNOTUS_BUILD_BENCH` and build the `bench` target in Release mode to run the full native matrix and write a dated Markdown report under `docs/benchmarks/`:

```powershell
cmake -S . -B build-bench -DTINNOTUS_BUILD_BENCH=ON
cmake --build build-bench --config Release --target bench
```

The benchmark defaults to 300 warm-up blocks and three batches of 2,000 latency samples (6,000 total) per case. Min, average, and p99 use the pooled latency samples; p99 uses the nearest-rank percentile. Throughput is the median of three independent passes, each running for at least 50 ms and extending short cases in batches. On Windows, the benchmark thread is pinned to the lowest available logical processor; other platforms leave affinity to the OS. CPU time is estimated from the median throughput pass's elapsed wall time and includes scheduler delays. Its executable accepts `--warmup`, `--iterations`, and `--output-dir`; see the repository [README](../README.md#benchmark-the-native-engine) for the matrix and report details.

## Threading and processing

Configure notches from one control thread with `AudioEngine::setNotch` or `removeNotch`; process blocks from one audio thread. The Rust wrapper can split an engine into a unique `AudioEngineControl` and `AudioEngineProcessor` for those roles. Updates are handed off through a bounded single-producer/single-consumer queue and are applied at the start of a processing call. A full queue is reported to the caller.

`AudioEngine::process` performs no allocation and takes no locks. The CPAL output callback uses preallocated scratch storage and atomics for status reporting; it does not allocate, lock, access files, or log. Its callback-cadence indicator uses CPAL callback timestamps; it does not measure callback execution time. `SpscAudioBuffer` also uses preallocated storage and is bounded in samples, not frames. Its single-producer/single-consumer restriction must be respected.

Run `python scripts/check_realtime_safety.py` from the repository root to check the known hot-path regions. This token-based guard is a regression check, not a proof of real-time safety; review newly added helpers and callback paths as well.

Pitch detection is a separate YIN analysis API exposed as `tinnotus_pitch_detect` and Rust `detect_pitch`. It analyzes one bounded mono window (64–8,192 samples at 8–192 kHz), returns frequency only when confidence is at least 0.8, and performs no allocation. Do not call it from the audio callback. Live playback analyzes the loudest source channel on a throttled background thread and reports the latest estimate in the UI; the callback only publishes its playhead through an atomic.

The safe Rust API returns `AudioError` for invalid configuration or arguments, queue-full results, and native allocation/status failures. Its `process` method accepts equal-length borrowed slices containing complete interleaved frames and does not allocate. The split control handle also exposes the latest successfully queued notch state; the processor applies changes when it begins the next block. The C pointer is owned by an `Arc`-backed RAII handle and destroyed once after both split handles are dropped. Rustdoc examples in `backend/src/lib.rs` show single-threaded and control/audio-thread usage.

On x86-64 the engine uses SSE2, and on AArch64 it uses NEON when supported by the compiler. These paths are used for six or more channels; narrower channel layouts use the scalar implementation. AVX2 is available as an opt-in CMake build with `-DTINNOTUS_ENABLE_AVX2=ON`. An AVX2 build requires an AVX2-capable CPU and does not perform runtime CPU dispatch. See the [SIMD benchmark comparison](../docs/benchmarks/2026-11-03-windows-x86_64-simd.md) for measurements and platform caveats.

## Rust application status

The root egui application supports offline WAV processing with per-block frame progress and cancellation between 1,024-frame blocks. A cancelled operation discards its temporary file. `WavError` distinguishes missing input, unsupported formats, existing output paths, engine rejections, and cancellation. The CPAL output-only WAV path is wired for Windows, macOS, and Linux; it enumerates devices, prefers float32 when available, falls back to supported integer/float formats, and converts the WAV to the selected device rate and channel count before starting the stream. CPAL uses the Windows WASAPI shared-mode host on Windows. Device errors and callback-cadence misses are reported to the UI through atomics; cadence misses are a possible-underrun indicator, not a hardware-provided underrun counter.

The decoded WAV is currently held in memory for playback. Microphone capture and automatic stream recovery after a device/format change are not implemented. The native benchmark measures processing performance, not end-to-end device latency. macOS/Linux endpoint validation and hardware start/stop, unplug/reconnect, and high-load checks remain outstanding.

The backend test suite includes a quick 30-seconds-of-audio callback test. To run the timed virtual-null-output test (30 seconds by default) without a physical endpoint:

```powershell
cargo test -p tinnotus-audio-backend --locked virtual_null_output_runs_for_thirty_seconds -- --ignored
```

Set `TINNOTUS_AUDIO_DEVICE_TEST_SECONDS` to an integer from 1 to 120 to change its wall-clock duration.

On a machine with an active output endpoint, the optional silent endpoint tests verify device enumeration, open/start/completion, a live notch update, and repeated start/stop without playing an audible signal. The default-device test accepts `TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS` from 1 to 120; it defaults to one second.

```powershell
$env:TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS = "30"
cargo test -p tinnotus-audio-backend --locked audio_device::tests::default_device_plays_silence_to_completion -- --ignored --nocapture
Remove-Item Env:TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS
cargo test -p tinnotus-audio-backend --locked audio_device::tests::selected_device_supports_repeated_start_stop -- --ignored --nocapture
```

On macOS/Linux, use a POSIX shell and prefix the default-device test with `TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS=30`.
