# Architecture

## Offline WAV processing

The UI runs file processing on a worker, not on the UI or device callback thread. It reads and writes bounded blocks, reports frame progress, and checks cancellation between blocks. Output is written to a temporary file and committed with a no-clobber hard link only after successful completion, so a destination created during processing is preserved. Filesystems that do not support hard links return an output error.

```text
UI/control thread
  └─ validates paths and parameters; starts/cancels the worker
                     │
                     ▼
WAV worker thread
  ├─ reads a bounded interleaved block
  ├─ calls AudioEngine::process
  ├─ writes the processed block to a temporary WAV
  └─ reports progress; checks cancellation between blocks
                     │
                     ▼
             completed output file
```

## Cross-platform live playback

The device path plays a decoded WAV through the selected system output device using CPAL. CPAL uses WASAPI shared mode on Windows and the platform audio host on macOS and Linux.

```text
UI/control thread
  ├─ enumerates system output devices
  ├─ decodes the WAV and converts its rate/channels before stream start
  ├─ creates the engine and starts the CPAL stream
  └─ sends notch updates through AudioEngineControl
                         │
                         ▼
                 AudioEngine command queue
                         │
                         ▼
CPAL output callback thread
  ├─ owns AudioEngineProcessor and preallocated scratch storage
  ├─ calls AudioEngine::process for the next interleaved WAV block
  ├─ converts samples to the negotiated output format
  ├─ publishes the playhead with an atomic
  └─ reports status and callback-cadence misses with atomics
                         │
                         ▼
                 system output device

Throttled pitch-analysis thread
  ├─ reads an immutable source window near the published playhead
  ├─ runs bounded YIN analysis outside the callback
  └─ publishes frequency and confidence for the UI
```

The callback performs no allocation, locking, file I/O, or logging. The source WAV is fully decoded before the stream starts. Float32 output is preferred; if unavailable, the adapter converts to a supported CPAL sample format. Sample-rate and channel conversion also happen before playback begins. Rate changes use a 32-tap, 1024-phase Blackman-windowed-sinc resampler. Channel conversion preserves equal channel counts, duplicates mono to each output channel, and averages all input channels for mono output. For other count mismatches it preserves the common channel order, drops excess input channels, and silence-fills extra output channels. Neither WAV decoding nor CPAL exposes a speaker mask here, so this policy does not claim layout-aware surround mixing.

`AudioEngineControl` is the single configuration producer; `AudioEngineProcessor` is the single processing consumer. The engine's fixed-capacity queue applies changes at the start of a processing block. Queue-full and invalid requests are returned to the control thread. Configuration queued before the first audio frame is applied immediately; subsequent live notch changes ramp to their target over 20 ms while preserving filter state. Removal ramps to the identity response and clears state after completion. Transition work uses fixed engine state and does not allocate or lock.

Pitch analysis uses a separate YIN API and a throttled worker. It selects the loudest source channel and analyzes a fixed-size window; the callback only stores the current playhead, so pitch analysis never executes on the real-time path. Estimates with confidence below 0.8 are shown as unpitched rather than as a frequency.

Stream errors and callback failures are stored atomically and polled by the UI, which pauses the stream and reports the failure. The callback-cadence indicator compares CPAL-provided callback timestamps with the expected block interval. It measures callback cadence, not callback execution time, and is only an estimate of possible output underruns—not a device-reported hardware counter. A disconnected device or changed format requires the user to refresh/select a device and restart playback.

## Future device path

Microphone capture and live input/output processing are not implemented. A future adapter should negotiate and allocate device buffers on a control thread, then hand bounded blocks through preallocated buffers to the engine. Device enumeration, stream changes, recovery, logging, and user notifications must remain outside the callback. Any capture ring must preserve its documented single-producer/single-consumer ownership.

## Real-time contract

`AudioEngine::process` and the CPAL output callback must stay allocation-free, lock-free, and free of file I/O and logging. The engine is linear in frame count, channel count, and active notch count; a real-time caller must enforce a maximum block size. The adapter caps callback blocks at 16,384 frames, supports at most 8 channels and 8 notches, and processes at most 1,048,576 frame/channel/notch combinations per callback, plus at most 16 pending commands at block start.

See [docs/REALTIME_RULES.md](./docs/REALTIME_RULES.md) for the full checklist and the limits of the CI source guard.

## Scope and verification

The CPAL output-only WAV path is wired for Windows, macOS, and Linux. Only Windows has been exercised against an active output endpoint so far; macOS/Linux builds and device checks for the current changes remain pending. Microphone capture and automatic device recovery are not implemented. The fast headless test processes 30 seconds of audio data, and an ignored timed virtual-null-output test runs the callback for 30 seconds without requiring an audio endpoint. Opt-in silent endpoint tests exercise default-device completion and live notch updates, plus repeated start/stop through an enumerated device. Device errors transition only an actively playing stream, so a delayed driver error cannot replace a finished or user-stopped status. These smoke tests do not verify audible quality; device unplug/reconnect, high-load behavior, and end-to-end latency still require manual verification.

Manual device checklist (repeat on each wired desktop OS):

1. Select the system default or a listed output device; play and stop a 30–60 second WAV.
2. Change the notch frequency and bandwidth during playback.
3. Unplug the active output device; confirm the UI reports the stream failure, then refresh and restart after reconnecting.
4. Repeat under high CPU load and watch the callback-cadence-miss count.
5. Try WAV rates/channel counts that differ from the selected output and verify playback starts with conversion.

Run the opt-in silent endpoint tests on a machine with an active output endpoint. The default-device test runs for one second by default; set the duration to 1–120 seconds for a longer run. Explicitly running these tests without an endpoint is a failure, not a skipped pass.

```powershell
$env:TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS = "30"
cargo test --locked -p tinnotus-audio-backend audio_device::tests::default_device_plays_silence_to_completion -- --ignored --nocapture
Remove-Item Env:TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS
cargo test --locked -p tinnotus-audio-backend audio_device::tests::selected_device_supports_repeated_start_stop -- --ignored --nocapture
```

On macOS/Linux, run the same tests from a POSIX shell:

```sh
TINNOTUS_AUDIO_ENDPOINT_TEST_SECONDS=30 cargo test --locked -p tinnotus-audio-backend audio_device::tests::default_device_plays_silence_to_completion -- --ignored --nocapture
cargo test --locked -p tinnotus-audio-backend audio_device::tests::selected_device_supports_repeated_start_stop -- --ignored --nocapture
```

The callback-cadence-miss count is only a possible-underrun indicator; it is not a hardware underrun counter or a callback execution-time measurement.
