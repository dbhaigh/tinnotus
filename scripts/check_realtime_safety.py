#!/usr/bin/env python3
"""Check known real-time source regions for common unsafe API patterns."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]

CPP_RULES = (
    ("dynamic allocation", re.compile(r"\b(?:new|delete)\b|\b(?:malloc|calloc|realloc|free)\s*\(")),
    (
        "heap-backed containers",
        re.compile(r"\bstd::(?:vector|string)\b|\.(?:push_back|emplace_back|reserve|resize)\s*\("),
    ),
    (
        "locking or condition variables",
        re.compile(r"\bstd::(?:mutex|recursive_mutex|lock_guard|unique_lock|condition_variable)\b"),
    ),
    (
        "I/O or logging",
        re.compile(
            r"\bstd::(?:ifstream|ofstream|fstream|filesystem|cout|cerr)\b"
            r"|\b(?:printf|fprintf|fopen|open|read|write)\s*\("
        ),
    ),
    (
        "exception path",
        re.compile(r"\b(?:throw|try|catch)\b"),
    ),
    (
        "clock query or blocking call",
        re.compile(
            r"\bstd::chrono::(?:steady_clock|system_clock)::now\s*\("
            r"|\b(?:clock_gettime|QueryPerformanceCounter|GetTickCount)\s*\("
            r"|\bstd::this_thread::(?:sleep_for|yield)\s*\("
            r"|\b(?:sleep|system)\s*\("
        ),
    ),
)

RUST_RULES = (
    (
        "allocation",
        re.compile(
            r"\b(?:Vec|String|Box)::|\b(?:Vec|String|Box)\s*<"
            r"|\.(?:to_vec|to_owned|into_owned)\s*\("
            r"|\b(?:malloc|calloc|realloc|free)\b"
        ),
    ),
    (
        "locking",
        re.compile(r"\b(?:Mutex|RwLock|Condvar)\b|\.lock\s*\(|\.read\s*\(|\.write\s*\("),
    ),
    (
        "panic path",
        re.compile(
            r"\b(?:panic|unreachable|todo|unimplemented)!"
            r"|\.(?:unwrap|expect)\s*\("
        ),
    ),
    (
        "I/O, logging, or blocking",
        re.compile(
            r"\bstd::(?:fs|io|thread|net|process)::"
            r"|\b(?:File|OpenOptions)::"
            r"|(?:println|eprintln|dbg)!"
            r"|\b(?:log|tracing)::(?:trace|debug|info|warn|error)!"
            r"|\b(?:sleep|park_timeout)\s*\("
        ),
    ),
    ("clock query", re.compile(r"\b(?:Instant|SystemTime)::now\s*\(")),
)

REGIONS = (
    (
        "shared/audio_engine.cpp",
        "bool AudioEngine::process(",
        "bool AudioEngine::calculateCoefficients(",
        "AudioEngine::process",
        CPP_RULES,
    ),
    (
        "shared/audio_engine.cpp",
        "void AudioEngine::applyPendingCommands(",
        "\n} // namespace tinnotus::audio",
        "AudioEngine::applyPendingCommands",
        CPP_RULES,
    ),
    (
        "shared/audio_engine_ffi.cpp",
        'extern "C" int tinnotus_audio_engine_process(',
        'extern "C" int tinnotus_pitch_detect(',
        "tinnotus_audio_engine_process",
        CPP_RULES,
    ),
    (
        "backend/windows/audio_device.rs",
        "    fn render<T: OutputSample>(",
        "trait OutputSample:",
        "PlaybackRenderer::render",
        RUST_RULES,
    ),
    (
        "backend/windows/audio_device.rs",
        "move |output: &mut [T], info|",
        "move |_error|",
        "Windows CPAL output callback",
        RUST_RULES,
    ),
)


def extract_region(relative_path, start_marker, end_marker):
    path = ROOT / relative_path
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        raise RuntimeError(f"cannot read {relative_path}: {error}") from error

    start = source.find(start_marker)
    if start < 0:
        raise RuntimeError(f"{relative_path}: missing start marker {start_marker!r}")

    end = source.find(end_marker, start + len(start_marker))
    if end < 0:
        raise RuntimeError(f"{relative_path}: missing end marker {end_marker!r}")

    return source, start, source[start:end]


def main():
    failures = []

    for relative_path, start_marker, end_marker, label, rules in REGIONS:
        try:
            source, region_start, region = extract_region(
                relative_path, start_marker, end_marker
            )
        except RuntimeError as error:
            failures.append(str(error))
            continue

        for rule_name, pattern in rules:
            match = pattern.search(region)
            if match:
                absolute_offset = region_start + match.start()
                line_number = source.count("\n", 0, absolute_offset) + 1
                matched_line = source.splitlines()[line_number - 1].strip()
                failures.append(
                    f"{relative_path}:{line_number}: {label}: "
                    f"forbidden {rule_name}: {matched_line}"
                )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1

    print(f"Real-time source guard passed ({len(REGIONS)} regions checked).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
