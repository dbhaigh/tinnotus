# Shared native core and legacy scaffolding

The active native engine and pitch detector live in `audio_engine.*`, `audio_engine_ffi.*`, and `pitch_detector.*`. They are built by the repository's root CMake project and consumed by the Rust backend.

`shared/Cargo.toml` is intentionally excluded from the root Cargo workspace. It belongs to an older Rust prototype and is not the active FFI crate. The adjacent legacy Rust stub files, `tinnotus.hpp`, and `shared/CMakeLists.txt` are not part of the current build. `dsp/PROTOTYPE.md` marks the separate DSP experiments that are also not compiled by the active target.

Keep the distinction clear when adding shared code: the directory contains both the active C++ core and retained historical scaffolding.
