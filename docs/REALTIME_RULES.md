# Real-time audio rules

These rules apply to `AudioEngine::process`, the native functions it calls on the processing path, and the CPAL output callback. The offline WAV worker is not a real-time thread, but it should continue to process bounded blocks so progress and cancellation remain responsive.

## Callback requirements

- Allocate and reserve engine storage during construction, then allocate stream, scratch, and sample storage before starting the stream. Do not allocate, resize, or free memory after engine construction on the processing path or inside the callback.
- Do not take locks, wait on a condition variable, sleep, yield, or block on a queue. Control requests use the fixed-capacity SPSC command queue; a full queue is reported to the producer.
- Do not perform file or console I/O, logging, formatting, device enumeration, stream management, or UI work.
- Keep each callback's work bounded. `AudioEngine::process` is linear in frame count, channel count, and active notch count, and accepts an arbitrary frame count. A real-time caller must impose its own limit. The current adapter accepts at most 16,384 frames, 8 channels, and 8 notches (1,048,576 frame/channel/notch combinations), plus at most 16 queued commands at the start of a block.
- Avoid panics, exceptions, and user-provided callbacks on the audio thread. Validate configuration and buffers before stream start wherever possible.
- Use atomics or a correctly-owned preallocated SPSC buffer to publish small status or analysis data. Poll and report status from a control/UI thread.
- Do not query clocks or collect detailed metrics from the callback. The adapter uses CPAL-provided callback timestamps to estimate callback cadence; this is not callback execution-time measurement.
- Keep analysis, resampling, format conversion setup, device changes, and recovery off the callback. The implementation decodes and converts the source WAV before starting playback; YIN pitch analysis runs on a separate throttled worker.

## Ownership and queue rules

- The Rust `AudioEngineControl` is the sole command producer; `AudioEngineProcessor` is the sole processing consumer.
- `SpscAudioBuffer` has exactly one producer and one consumer. Its capacity is measured in samples and fixed at construction.
- Do not add a second producer or consumer to either SPSC queue. If ownership needs to change, coordinate it on a non-real-time thread or introduce a separately reviewed queue design.
- Keep command application bounded and preserve explicit queue-full/error reporting. Never silently drop a control request or block the audio thread to make room.

## Review checklist

For every new or changed audio callback path:

1. Identify the callback entry point and every helper it reaches.
2. Verify that storage is allocated before stream start and that the maximum work per callback is bounded.
3. Check all called functions for allocation, locks, syscalls, I/O, logging, blocking, and panic/exception paths.
4. Verify single-producer/single-consumer ownership and atomic memory ordering.
5. Add the source region to the CI guard when appropriate, and run callback, concurrency, and sanitizer tests.
6. Document platform-specific limits and distinguish measured facts from estimates.

## Static guard

Run the repository's token-based check with:

```powershell
python scripts/check_realtime_safety.py
```

CI checks the marked regions of `AudioEngine::process`, `AudioEngine::applyPendingCommands`, the native process C wrapper, `PlaybackRenderer::render`, and the CPAL callback. It flags common allocation, locking, I/O, logging, blocking, and clock-query APIs. The check is intentionally small and is not a proof: it does not follow arbitrary calls, inspect dependency implementations, or detect every indirect allocation or operating-system interaction. New callback paths and helpers still require manual review and tests.
