use std::env;

fn main() {
    let manifest_dir =
        env::var("CARGO_MANIFEST_DIR").expect("Cargo provides CARGO_MANIFEST_DIR");
    let install_dir = cmake::Config::new(manifest_dir)
        .define("TINNOTUS_BUILD_TEST", "OFF")
        .build();

    println!(
        "cargo:rustc-link-search=native={}",
        install_dir.join("lib").display()
    );
    println!("cargo:rustc-link-lib=static=tinnotus_backend");

    let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if target_env == "msvc" {
        println!("cargo:rustc-link-lib=dylib=msvcprt");
    } else if target_os == "macos" {
        println!("cargo:rustc-link-lib=dylib=c++");
    } else if target_env == "gnu" {
        println!("cargo:rustc-link-lib=dylib=stdc++");
    } else {
        panic!("Unsupported C++ runtime target: {target_os}/{target_env}");
    }

    println!("cargo:rerun-if-changed=CMakeLists.txt");
    println!("cargo:rerun-if-changed=../shared/audio_engine.hpp");
    println!("cargo:rerun-if-changed=../shared/audio_engine.cpp");
    println!("cargo:rerun-if-changed=../shared/audio_engine_ffi.h");
    println!("cargo:rerun-if-changed=../shared/audio_engine_ffi.cpp");
}
