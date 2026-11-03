# Architecture

## Offline WAV processing

The UI runs file processing on a worker, not on the UI or device callback thread. It reads and writes bounded blocks, reports frame progress, and checks cancellation between blocks. Output is written to a temporary file and committed only after successful completion.

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

## Windows live playback

The first device path plays a decoded WAV through the selected Windows output device using CPAL's WASAPI shared-mode host.

```text
UI/control thread
  ├─ enumerates the system output devices
  ├─ decodes the WAV and converts its rate/channels before stream start
  ├─ creates the engine and starts the CPAL stream
  └─ sends notch updates through AudioEngineControl
                         │
                         ▼
                 AudioEngine command queue
                         │
                         ▼
WASAPI shared-mode callback thread
  ├─ owns AudioEngineProcessor and preallocated scratch storage
  ├─ calls AudioEngine::process for the next interleaved WAV block
  ├─ converts samples to the negotiated output format
  ├─ publishes the playhead with an atomic
  └─ reports status and callback-cadence misses with atomics
                         │
                         ▼
                 Windows output device

Throttled pitch-analysis thread
  ├─ reads an immutable source window near the published playhead
  ├─ runs bounded YIN analysis outside the callback
  └─ publishes frequency and confidence for the UI
```

The callback performs no allocation, locking, file I/O, or logging. The source WAV is fully decoded before the stream starts. Float32 output is preferred; if unavailable, the adapter converts to a supported CPAL sample format. Sample-rate and channel conversion also happen before playback begins.

`AudioEngineControl` is the single configuration producer; `AudioEngineProcessor` is the single processing consumer. The engine's fixed-capacity queue applies changes at the start of a processing block. Queue-full and invalid requests are returned to the control thread.

Pitch analysis uses a separate YIN API and a throttled worker. It selects the loudest source channel and analyzes a fixed-size window; the callback only stores the current playhead, so pitch analysis never executes on the real-time path. Estimates with confidence below 0.8 are shown as unpitched rather than as a frequency.

Stream errors and callback failures are stored atomically and polled by the UI, which pauses the stream and reports the failure. The callback-cadence indicator compares CPAL-provided callback timestamps with the expected block interval. It measures callback cadence, not callback execution time, and is only an estimate of possible output underruns—not a device-reported hardware counter. A disconnected device or changed format requires the user to refresh/select a device and restart playback.

## Future device path

Microphone capture and live input/output processing are not implemented. A future adapter should negotiate and allocate device buffers on a control thread, then hand bounded blocks through preallocated buffers to the engine. Device enumeration, stream changes, recovery, logging, and user notifications must remain outside the callback. Any capture ring must preserve its documented single-producer/single-consumer ownership.

## Real-time contract

`AudioEngine::process` and the Windows output callback must stay allocation-free, lock-free, and free of file I/O and logging. The engine is linear in frame count, channel count, and active notch count; a real-time caller must enforce a maximum block size. The Windows adapter caps callback blocks at 16,384 frames, supports at most 8 channels and 8 notches, and processes at most 1,048,576 frame/channel/notch combinations per callback, plus at most 16 pending commands at block start.

See [docs/REALTIME_RULES.md](./docs/REALTIME_RULES.md) for the full checklist and the limits of the CI source guard.

## Scope and verification

Windows output-only WAV playback is implemented. Microphone capture, automatic device recovery, and live-device paths on macOS/Linux are not implemented. Platform folders with unimplemented adapters are marked as prototypes. The fast headless test processes 30 seconds of audio data, and an ignored timed virtual-null-output test runs the callback for 30 seconds without requiring an audio endpoint. An optional silent WASAPI smoke test enumerates devices, opens the default endpoint, queues a live notch update, checks playback completion, and verifies stop without an audible signal. Audible playback, device changes, and end-to-end latency still require manual verification.

Manual Windows checklist:

1. Select the system default or a listed output device; play and stop a 30–60 second WAV.
2. Change the notch frequency and bandwidth during playback.
3. Unplug the active output device; confirm the UI reports the stream failure, then refresh and restart after reconnecting.
4. Repeat under high CPU load and watch the callback-cadence-miss count.
5. Try WAV rates/channel counts that differ from the selected output and verify playback starts with conversion.
