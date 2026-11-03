# Cross-Platform Deployment Guide

> **Status notice:** This is a historical architecture and deployment proposal, not a build guide or evidence that its platform adapters are verified or supported; some paths in its examples refer to archived prototypes. The current product version is `0.0.0.2`; the Cargo workspace encodes it as `0.0.0+2` because Cargo requires SemVer. The verified CMake target builds the portable C++ processing core and pitch detector. A Rust CPAL output-only path is wired for Windows, macOS, and Linux, but only Windows endpoint validation has been completed. See [ARCHITECTURE.md](../ARCHITECTURE.md), [PROJECT_STATUS.md](../PROJECT_STATUS.md), and the platform `PROTOTYPE.md` files for current scope.

## Overview

This document outlines the deployment strategy for Tinnotus across all target platforms:
- **Desktop**: Windows, macOS, Linux
- **Mobile**: iOS, Android

## Tech Stack Summary

```
┌─────────────────────────────────────────────────────┐
│                    TInNotus                         │
├─────────────────────────────────────────────────────┤
│                                                     │
│  ┌───────────────────┐     ┌───────────────────┐  │
│  │                   │     │                   │  │
│  │   Desktop UI      │     │   Mobile UI        │  │
│  │   (Rust/Egui)     │     │  (Platform Native) │  │
│  │                   │     │                   │  │
│  │                  │     │                   │  │
│  │      └───────┘     │     │                  │  │
│  │          │         │     │                  │  │
│  └──────────┼─────────┘     │                  │  │
│             │               │                  │  │
│             ▼               │                  │  │
│  ┌──────────────────────────┴────────────────────┐│
│  │              C++ Backend                       ││
│  │  (DSP, Notch Filter, Pitch Detection)          ││
│  │                                                 ││
│  │  ┌────────────┐  ┌──────────────┐            ││
│  │  │ DSP Engine │  │  Audio I/O   │            ││
│  │  │ (FFI)      │  │ (Platform)   │            ││
│  │  └────────────┘  └──────────────┘            ││
│  └───────────────────────────────────────────────┘│
│                                                     │
└─────────────────────────────────────────────────────┘
```

---

## Desktop Platforms

### Windows

**Frontend**: Rust + `winit` + `egui`  
**Backend**: C++ with Windows FFI  
**Audio**: C++ FFI + Windows Audio Session API (WASAPI)

#### Windows Backend (Desktop)

```cmake
# backend/windows/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(tinnotus_backend_windows)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 /arch:AVX2 /DNDEBUG")

# Find Windows SDK
find_package(Windows SDK 10.0.19041.0 REQUIRED)

# Core audio (WASAPI)
add_library(tinnotus_audio_core SHARED 
    src/windows/wasapi_wrapper.cpp
    src/dsp/notch_filter.cpp
    src/dsp/pitch_detector.cpp
    src/audio_service.cpp
)

target_include_directories(tinnotus_audio_core PUBLIC
    ${WINDOWS_DEVS_DIR}
)

target_link_libraries(tinnotus_audio_core PRIVATE
    winmm.lib
    comdlg32.lib
)

target_compile_definitions(tinnotus_audio_core PRIVATE
    _WIN32_WINNT=0x0A00
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# FFI shared library
add_library(tinnotus_ffi SHARED
    src/windows/ffi_wrapper.cpp
)

# Install
install(TARGETS tinnotus_ffi tinnotus_audio_core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
```

**Windows FFI Wrapper** (`src/windows/ffi_wrapper.cpp`):

```cpp
// tinnotus_ffi_wrapper.cpp
#include <windows.h>
#include <stdint.h>

extern "C" {

// Initialize backend
__declspec(dllexport)
void* tinnotus_backend_init() {
    return new tinnotus::audio::AudioProcessor(44100);
}

// Cleanup
__declspec(dllexport)
void tinnotus_backend_cleanup(void* handle) {
    delete static_cast<tinnotus::audio::AudioProcessor*>(handle);
}

// Set audio parameters
__declspec(dllexport)
int tinnotus_set_audio_params(void* handle, uint32_t sampleRate, 
                             int format, uint32_t channels) {
    auto* proc = static_cast<tinnotus::audio::AudioProcessor*>(handle);
    proc->setSampleRate(sampleRate);
    proc->setChannels(channels);
    return 0; // Success
}

// Create notch filter
__declspec(dllexport)
int tinnotus_create_notch(void* handle, const tinnotus::NotchParams* params) {
    auto* proc = static_cast<tinnotus::audio::AudioProcessor*>(handle);
    proc->createNotch(params->center_freq, params->q_factor);
    return 0; // Success
}

// Remove notch by index
__declspec(dllexport)
int tinnotus_remove_notch(void* handle, uint32_t index) {
    auto* proc = static_cast<tinnotus::audio::AudioProcessor*>(handle);
    proc->removeNotch(index);
    return 0; // Success
}

// Set pitch
__declspec(dllexport)
int tinnotus_set_pitch(void* handle, double freq, double q) {
    auto* proc = static_cast<tinnotus::audio::AudioProcessor*>(handle);
    proc->setPitch(freq, q);
    return 0; // Success
}

// Process audio buffer
__declspec(dllexport)
uint32_t tinnotus_process_audio(void* handle, 
                               const float* input, float* output,
                               uint32_t frames) {
    auto* proc = static_cast<tinnotus::audio::AudioProcessor*>(handle);
    return proc->process(input, output, frames);
}

// Get current state
__declspec(dllexport)
int tinnotus_get_current_state(void* handle, tinnotus::AudioState* state) {
    auto* proc = static_cast<tinnotus::audio::AudioProcessor*>(handle);
    proc->getCurrentState(state);
    return 0; // Success
}

}  // extern "C"
```

**Build Instructions (Windows)**:

```bash
# Install dependencies
choco install cmake -y
choco install nasm -y  # For AVX2 inline assembly

# Build
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# Or use vcpkg
vcpkg install portname --triplet x64-windows
```

#### Windows Frontend (Desktop)

```rust
// Cargo.toml
[package]
name = "tinnotus_desktop"
version = "0.0.0+2"
edition = "2021"

[dependencies]
winit = "0.29"
egui = "0.27"
eframe = { version = "0.27", default-features = false, features = ["winit"] }
crossbeam-channel = "0.5"
libloading = "0.8"  # For loading DLL

[build-dependencies]
# Platform-specific build
[target.'cfg(windows)'.build-dependencies]
winres = "0.1"  # For app identity
```

**Build Script** (`build.rs`):

```rust
fn main() {
    // Embed DLL into executable
    let out = &env::var("OUT_DIR").unwrap();
    let dst = PathBuf::from(out);
    std::fs::create_dir_all(&dst).expect("Failed to create output dir");
    
    // Copy DLL
    let dll_path = Path::new("backend/target/release/tinnotus_ffi.dll");
    let dst_path = dst.join("tinnotus_ffi.dll");
    std::fs::copy(dll_path, &dst_path).expect("Failed to copy DLL");
    
    println!("cargo:rerun-if-changed={}", dll_path.display());
}
```

**Desktop App** (`src/desktop/main.rs`):

```rust
fn main() {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([1024.0, 768.0])
            .with_resizable(true)
            .with_transparent(false),
        ..Default::default()
    };
    
    // Load backend DLL
    let _backend = libloading::win::Library::new("backend/target/release/tinnotus_ffi.dll")
        .expect("Failed to load backend");
    
    eframe::run_native("Tinnotus", options, Box::new(TinnotusApp::default));
}
```

### macOS

**Frontend**: Rust + `winit` + `egui` (or SwiftUI)  
**Backend**: C++ + macOS CoreAudio  
**Audio**: C++ FFI + CoreAudio

#### macOS Backend

```cmake
# backend/macos/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(tinnotus_backend_macos)

set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0)
set(CMAKE_CXX_STANDARD 17)

find_library(ACORE AudioToolbox REQUIRED)
find_library(AUI AudioToolbox REQUIRED)

add_library(tinnotus_audio_core SHARED
    src/macos/coreaudio_wrapper.mm
    src/dsp/notch_filter.cpp
    src/dsp/pitch_detector.cpp
    src/audio_service.cpp
)

target_link_libraries(tinnotus_audio_core PRIVATE ${ACORE} ${AUI})

add_library(tinnotus_ffi SHARED
    src/macos/ffi_wrapper.mm
)

install(TARGETS tinnotus_ffi tinnotus_audio_core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
```

**macOS FFI Wrapper** (`src/macos/ffi_wrapper.mm`):

```objc
// tinnotus_ffi_wrapper.mm
#include "tinnotus.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

extern "C" {

void* tinnotus_backend_init() {
    return new tinnotus::audio::AudioProcessor(44100);
}

void tinnotus_backend_cleanup(void* handle) {
    delete static_cast<tinnotus::audio::AudioProcessor*>(handle);
}

int tinnotus_set_audio_params(void* handle, uint32_t sampleRate, 
                             int format, uint32_t channels) {
    auto* proc = static_cast<tinnotus::audio::AudioProcessor*>(handle);
    
    // Setup CoreAudio stream
    AudioStreamBasicDescription streamFormat;
    streamFormat.mSampleRate = sampleRate;
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mBitsPerChannel = 32;
    streamFormat.mBytesPerPacket = channels * 4;
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mBytesPerFrame = channels * 4;
    streamFormat.mChannelsPerFrame = channels;
    streamFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger;
    
    // Configure backend with CoreAudio
    proc->configureCoreAudio(streamFormat);
    
    return 0;
}

// ... other methods
}
```

### Linux

**Frontend**: Rust + `winit` + `egui`  
**Backend**: C++ + ALSA/PulseAudio  
**Audio**: C++ FFI + ALSA/PulseAudio

#### Linux Backend

```cmake
# backend/linux/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(tinnotus_backend_linux)

set(CMAKE_CXX_STANDARD 17)

find_library(ALSA NAMES alsa REQUIRED)
find_library(PULSE NAMES pulse REQUIRED)

add_library(tinnotus_audio_core SHARED
    src/linux/alsa_wrapper.c
    src/linux/pulse_wrapper.c
    src/dsp/notch_filter.cpp
    src/dsp/pitch_detector.cpp
)

target_link_libraries(tinnotus_audio_core PRIVATE ${ALSA} ${PULSE})

add_library(tinnotus_ffi SHARED
    src/linux/ffi_wrapper.c
)
```

#### Linux FFI Wrapper (`src/linux/ffi_wrapper.c`):

```c
// tinnotus_ffi_wrapper.c
#include <stdint.h>
#include <stdlib.h>

struct tinnotus_context {
    void* handle;
    int32_t sample_rate;
    int channels;
};

void tinnotus_backend_init(void) {
    tinnotus::dsp::NotchFilter filter(4000.0, 10.0, 44100);
}

void tinnotus_backend_cleanup(void* handle) {
    if (handle) {
        // Cleanup context
    }
}

int tinnotus_set_audio_params(void* handle, uint32_t sampleRate, 
                             int format, uint32_t channels) {
    // Initialize ALSA/PulseAudio device
    return 0;
}

// ... other methods (same pattern as Windows/macOS)
```

---

## Mobile Platforms

### iOS

**Frontend**: Swift  
**Backend**: Swift FFI + compiled C++ (or pure Swift implementation)  
**Audio**: AVFoundation + AudioKit

#### iOS Build

```swift
// Package.swift
import PackageDescription

let package = Package(
    name: "TinnotusIOS",
    platforms: [.iOS(.v16)],
    products: [
        .library(
            name: "TinnotusAudioEngine",
            targets: ["TinnotusAudioEngine"]),
    ],
    dependencies: [
        .package(path: "../backend"),
        .package(url: "https://github.com/onevcat/Kinetic.swift", ...),
    ],
    targets: [
        .target(
            name: "TinnotusAudioEngine",
            dependencies: [.product(name: "TinnotusBackend", package: "backend")]),
        .testTarget(name: "TinnotusAudioEngineTests", dependencies: ["TinnotusAudioEngine"]),
    ],
    cxxLanguageStandard: .cxx20,
    swiftLanguageVersions = ["5"],
)
```

**Swift Implementation** (`Sources/TinnotusAudioEngine/TinnotusAudioEngine.swift`):

```swift
import AVFoundation
import AudioToolbox
import tinnotus_backend  // FFI binding

public class TinnotusAudioEngine: NSObject, AVAudioNode {
    
    private var backend: OpaquePointer?
    private var lastInputBuffer: UnsafeMutablePointer<Float32>?
    private var lastOutputBuffer: UnsafeMutablePointer<Float32>?
    private var currentFrequency: Float = 4000.0
    
    override public init() {
        super.init()
        self.backend = tinnotus_backend_init()
    }
    
    override public func turnOn() {
        print("Audio engine started")
    }
    
    override public func turnOff() {
        tinnotus_backend_cleanup(self.backend)
        self.backend = nil
    }
    
    public override func prepare(withFrameCount frameCount: Int32) {
        // Allocate buffers
        guard let bufferSize = UnsafeMutablePointer<Float32>.allocate(capacity: frameCount) else {
            return
        }
        
        self.lastOutputBuffer = bufferSize
        self.lastInputBuffer = UnsafeMutablePointer<Float32>.allocate(capacity: frameCount)
    }
    
    public override func setParameters(_ params: [AnyHashable: Any]) {
        // Configure backend
        tinnotus_set_audio_params(
            self.backend!,
            UInt32(params[.sampleRate] as? UInt32 ?? 44100),
            0,  // Float32
            2   // Stereo
        )
    }
    
    public override func copyBuffer(forOutputTo decriptor: AudioStreamPacketDescription, 
                                    to buffer: UnsafeMutablePointer<Float32>,
                                    frameCount: Int32) {
        tinnotus_process_audio(
            self.backend!,
            self.lastInputBuffer!,
            buffer,
            frameCount
        )
        
        // Update output timestamp
    }
    
    // Exposed methods for SwiftUI
    public func setFrequency(_ freq: Float) {
        self.currentFrequency = freq
        tinnotus_set_pitch(self.backend!, freq, 10.0)
    }
    
    public func addNotch(_ freq: Float) {
        tinnotus_create_notch(self.backend!, NotchParams(centerFreq: freq, qFactor: 10.0))
    }
}
```

**SwiftUI Preview** (`TinnotusApp.swift`):

```swift
import SwiftUI

struct TinnotusApp: App {
    @State private var notchFrequency: Double = 4000.0
    @State private var notchEnabled: Bool = false
    
    @StateObject private var audioEngine = TinnotusAudioEngine()
    
    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(audioEngine)
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var audioEngine: TinnotusAudioEngine
    
    var body: some View {
        VStack {
            Text("Tinnotus")
                .font(.largeTitle)
            
            // Frequency slider
            HStack {
                Label("Frequency", systemImage: "waveform")
                Slider(value: $notchFrequency, in: 200...8000)
                Text("\(Int(notchFrequency)) Hz")
                    .fontWeight(.bold)
            }
            
            // Toggle
            Toggle("Notch Filter", isOn: $notchEnabled)
            
            // Start/Stop
            Button("Start Processing") {
                audioEngine.setFrequency(notchFrequency)
                audioEngine.start()
            }
            .disabled(!notchEnabled)
            .padding()
            .foregroundColor(.green)
        }
        .padding()
    }
}
```

### Android

**Frontend**: Kotlin + Jetpack Compose  
**Backend**: C++ FFI + KTX audio  
**Audio**: OpenSL ES / AudioTrack

#### Android FFI Wrapper

```kotlin
// android-native/src/main/cpp/ffi_wrapper.cpp
// Use STLport or Android NDK
// Include tinnotus_backend FFI headers

// FFI JNI wrappers
// tinnotus_jni.cpp
```

#### Android Kotlin App

```kotlin
// app/src/main/kotlin/com/tinnotus/TinnotusActivity.k.kt
package com.tinnotus

import android.media.AudioFormat
import android.media.AudioTrack
import tinnotus_backend.TinnotusBackend

class TinnotusActivity : AppCompatActivity() {
    
    private lateinit var backend: TinnotusBackend
    private lateinit var audioTrack: AudioTrack
    
    private var notchFrequency: Float = 4000.0f
    private var qFactor: Float = 10.0f
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Initialize backend
        backend = TinnotusBackend(sampleRate = 44100)
        
        // Setup audio
        val audioFormat = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
            .setSampleRate(44100)
            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
            .build()
        
        audioTrack = AudioTrack(
            context,
            audioFormat,
            1024,  // buffer size
            AudioTrack.MODE_STREAM
        )
        
        // Setup notches
        backend.createNotch(centerFreq = notchFrequency, qFactor = qFactor)
        
        // Start playback
        audioTrack.play()
    }
    
    // Update notch frequency
    fun setNotchFrequency(freq: Float) {
        notchFrequency = freq
        backend.setPitch(freq = freq, q = qFactor)
    }
    
    // Audio processing callback
    private fun audioCallback(
        data: ByteArray,
        offset: Int,
        frames: Int
    ) {
        val input = data.copyOfRange(0, frames * 4)
        val output = data.copyOfRange(offset, offset + frames * 4)
        
        backend.processAudio(input, output, frames)
        
        // Write to audio buffer
        for (i in output.indices) {
            // Convert float32 to 16-bit PCM
        }
        audioTrack.write(output, offset, frames)
    }
}
```

#### Android Compose UI

```kotlin
// app/src/main/kotlin/com/tinnotus/compose/TinnotusScreen.kt
@Composable
fun TinnotusScreen() {
    var notchFrequency by remember { mutableStateOf(4000.0f) }
    var qFactor by remember { mutableStateOf(10.0f) }
    var notchEnabled by remember { mutableStateOf(false) }
    
    Screen(
        notchFrequency = notchFrequency,
        qFactor = qFactor,
        notchEnabled = notchEnabled
    ) {
        // Update backend
        when (action) {
            Action.SetFrequency -> {
                backend.setPitch(freq = it, q = qFactor)
            }
            Action.Enable -> {
                notchEnabled = true
                backend.setEnabled(true)
            }
            Action.Disable -> {
                notchEnabled = false
                backend.setEnabled(false)
            }
        }
    }
}

@Composable
fun Screen(
    notchFrequency: Float,
    qFactor: Float,
    notchEnabled: Bool,
    onAction: (Action) -> Unit,
) {
    ScreenContent {
        // Frequency slider
        Slider(
            value = notchFrequency,
            onValueChange = { notchFrequency = it },
            range = 200f..8000f
        )
        
        // Q factor slider
        Slider(
            value = qFactor,
            onValueChange = { qFactor = it },
            range = 1.0f..50.0f
        )
        
        // Notch toggle
        Switch(
            checked = notchEnabled,
            onCheckedChange = { notchEnabled = it }
        )
        
        // Start button
        Button(
            onClick = { onAction(Action.Enable) }
        ) {
            Text("Start")
        }
        
        // Frequency test
        FrequencyTestSection { frequency ->
            onAction(Action.SetFrequency(frequency))
        }
    }
}
```

---

## Build System Integration

### Cross-Platform Build

```bash
# Multi-platform build using CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=...

# Windows
cmake -B build/win -DCMAKE_TOOLCHAIN_FILE="vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_GENERATOR_PLATFORM=x64

# macOS
cmake -B build/macos -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0

# Linux
cmake -B build/linux

# Build
cmake --build build
```

### Platform-Specific Build Scripts

```bash
# scripts/build-windows.bat
@echo off
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

```bash
# scripts/build-macos.sh
#!/bin/bash
cmake .. -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

```bash
# scripts/build-linux.sh
#!/bin/bash
cmake .. -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

---

## Publishing

### Desktop Distribution

#### Windows
- Publish as installer (NSIS): `nsis.exe -o TinnotusSetup.exe`
- Publish as portable ZIP
- Submit to Microsoft Store (requires packaging)

#### macOS
- Code sign and notarize
- Create `.app` bundle
- Submit to Mac App Store

#### Linux
- Flatpak
- Snap
- Deb/Rpm

### Mobile Distribution

#### iOS
- Archive and export
- Submit to App Store
- TestFlight beta testing

#### Android
- Create signed APK/AAB
- Submit to Google Play Console

---

## Performance Optimization

### C++ Backend

```cpp
// Use SIMD for DSP operations
// Platform-specific intrinsics
#if defined(_MSC_VER)
    #include <intrin.h>
    #define SIMD_INTRINSICS _mm
#elif defined(__APPLE__)
    #include <quasi_neon.h>
    #define SIMD_INTRINSICS __builtin
#elif defined(__x86_64__)
    #include <immintrin.h>
    #define SIMD_INTRINSICS _mm256
#endif

// In notch filter
float NotchFilter::process(float sample) {
    #if defined(__AVX2__) || defined(__AVX512F__)
        // SIMD-optimized path
        __m256 vec = _mm256_loadu_ps(&input[0]);
        __m256 result = _mm256_mul_ps(vec, _mm256_set1_ps(coeff));
        return _mm256_hadd_ps(result, result);
    #else
        // Scalar path
        return b[0] + b[1] * sample;
    #endif
}
```

### Rust Frontend

```toml
# Enable SIMD
[profile.release]
codegen-units = 1
lto = true
opt-level = 3
```

---

## Testing Strategy

### Unit Tests

```rust
#[test]
fn test_pitch_detection() {
    let detector = YINPitchDetector(sampleRate: 44100);
    let audio = generate_sine_wave(frequency: 1000.0, duration: 1.0);
    let detected = detector.detectPitch(audio);
    assert!((detected - 1000.0).abs() < 5.0);
}

#[test]
fn test_notch_filter_attenuation() {
    let filter = NotchFilter(center: 4000.0, q: 10.0, sampleRate: 44100);
    
    // Test at notch frequency
    let signal_at_notch = filter.process(tone(4000.0));
    assert!(signal_at_notch < 0.1);  // High attenuation
    
    // Test away from notch
    let signal_off_notch = filter.process(tone(4050.0));
    assert!(signal_off_notch > 0.9);  // Minimal attenuation
}
```

### Integration Tests

```rust
// Test end-to-end audio processing
#[test]
fn test_full_pipeline() {
    let backend = Backend::init();
    backend.createNotch(frequency: 4000.0);
    
    let input = load_test_audio();
    let output = backend.process(input);
    
    // Verify notch filter worked
    assert!(has_notch_filter(&output, frequency: 4000.0));
}
```

### Platform-Specific Tests

```cpp
// test_notch_windows.cpp
#ifdef _WIN32
    test_wasapi_integration();
    test_windows_audio_device();
#endif
```

---

## Documentation

### Developer Documentation

```md
# API Reference

## Backend

### AudioProcessor

```cpp
class AudioProcessor {
public:
    // Initialize
    AudioProcessor(int sampleRate = 44100);
    
    // Set notch filter
    void createNotch(NotchParams params);
    void removeNotch(uint32_t index);
    void setPitch(double freq, double q);
    
    // Process audio
    int process(const float* input, float* output, int frames);
    
    // Start/stop streaming
    void startProcessing();
    void stopProcessing();
};
```

### NotchFilter

```cpp
class NotchFilter {
public:
    // Constructor
    NotchFilter(double freq, double q, int sampleRate);
    
    // Set parameters
    void setFrequency(double freq);
    void setQuality(double q);
    
    // Process sample
    float process(float sample);
};
```

## Frontend

### Rust API

```rust
pub struct TinnotusBackend {
    // Backend state
}

impl TinnotusBackend {
    pub fn new(sample_rate: u32) -> Result<Self, String>;
    
    pub fn set_frequency(&mut self, freq: f64) -> Result<(), String>;
    pub fn add_notch(&mut self, freq: f64) -> Result<(), String>;
    
    pub fn process_audio(&self, input: &[f32], output: &mut [f32]) -> usize;
}
```
```

---

## Current status

This proposal's implementation checklists and platform examples are historical and may not match the current code. The current product version is `0.0.0.2` (Cargo version `0.0.0+2`). For the verified feature set, build instructions, known limitations, and roadmap, use [PROJECT_STATUS.md](../PROJECT_STATUS.md) and [README.md](../README.md). For the implemented threading model, use [ARCHITECTURE.md](../ARCHITECTURE.md).