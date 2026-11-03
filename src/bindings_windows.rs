/**
 * @file binding_windows.rs
 * @brief Windows FFI bindings for Tinnotus backend
 * 
 * This file provides safe Rust bindings to the C++ backend library
 * using WASAPI for audio I/O on Windows.
 * 
 * To build:
 *   cargo build --release
 *   cp target/release/libtinnotus_backend.dll /path/to/cpp/
 *   cp target/release/libtinnotus_backend.dll /Users/ahaigh/AppData/Local/bin/
 * 
 * Requires:
 *   - libtinnotus_backend.dll in PATH or app directory
 *   - vcpkg to build C++ with Windows support
 * 
 * To use:
 *   let audio = TinnotusAudio::new();
 *   audio.set_notch_frequency(4000.0);
 *   audio.set_notch_bandwidth(500.0);
 *   audio.process_audio(&input_buffer);
 */

use std::alloc::{alloc, dealloc, Layout};
use std::ptr;
use std::os::raw::{c_char, c_double, c_int, c_void};

// ============================================================================
// FFI Types matching tinnotus.hpp
// ============================================================================

/// Version information
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Version {
    pub major: u32,
    pub minor: u32,
    pub patch: u32,
}

/// Notch filter parameters
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct NotchParams {
    pub center_freq: c_double,
    pub bandwidth: c_double,
    pub sample_rate: c_int,
}

impl Default for NotchParams {
    fn default() -> Self {
        Self {
            center_freq: 4000.0,
            bandwidth: 500.0,
            sample_rate: 44100,
        }
    }
}

/// Multiple notch configuration
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct NotchList {
    pub count: usize,
    pub notches: [Notch; 8],
}

impl Default for NotchList {
    fn default() -> Self {
        Self {
            count: 0,
            notches: [Notch { freq: 0.0, bandwidth: 500.0 }; 8],
        }
    }
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Notch {
    pub freq: c_double,
    pub bandwidth: c_double,
}

/// Audio buffer
#[repr(C)]
pub struct AudioBuffer {
    pub sample_rate: c_int,
    pub num_channels: c_int,
    pub num_frames: usize,
    pub samples: *mut c_float,
}

impl AudioBuffer {
    /// Create an empty audio buffer
    pub fn new(sample_rate: c_int, num_channels: c_int, num_frames: usize) -> Self {
        let num_samples = num_frames * num_channels;
        let layout = Layout::from_size_align(num_samples, std::mem::size_of::<c_float>()).unwrap();
        let ptr = unsafe { alloc(layout) };
        
        let ptr = ptr as *mut c_float;
        let mut buffer = AudioBuffer {
            sample_rate,
            num_channels,
            num_frames,
            samples: ptr,
        };
        
        // Initialize to zero
        unsafe {
            std::ptr::write_bytes(ptr, 0, num_samples);
        }
        
        buffer
    }
    
    /// Create audio buffer with pre-filled samples
    pub fn with_samples(sample_rate: c_int, num_channels: c_int, num_frames: usize, samples: *const c_float) -> Self {
        let num_samples = num_frames * num_channels;
        let layout = Layout::from_size_align(num_samples, std::mem::size_of::<c_float>()).unwrap();
        let ptr = unsafe { alloc(layout) };
        let ptr = ptr as *mut c_float;
        
        // Copy samples
        unsafe {
            std::ptr::copy_nonoverlapping(samples, ptr, num_samples);
        }
        
        AudioBuffer {
            sample_rate,
            num_channels,
            num_frames,
            samples: ptr,
        }
    }
    
    /// Free the buffer
    pub fn free(&mut self) {
        if !self.samples.is_null() {
            let layout = Layout::from_size_align(
                std::mem::size_of::<c_float>() * self.num_frames * self.num_channels,
                std::mem::size_of::<c_float>(),
            ).unwrap();
            unsafe {
                dealloc(self.samples as *mut u8, layout);
            }
            self.samples = ptr::null_mut();
            self.num_frames = 0;
        }
    }
}

/// Signal detected result
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct SignalDetected {
    pub detected: c_int,
    pub frequency: c_double,
    pub confidence: c_double,
    pub intensity: c_double,
}

/// Service statistics
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Statistics {
    pub frames_processed: usize,
    pub frames_output: usize,
    pub latency_ms: c_double,
    pub average_latency_ms: c_double,
    pub processing_time_us: c_double,
    pub total_notch_count: usize,
    pub active_frequency: c_double,
    pub active_confidence: c_double,
}

/// Audio service handle
pub struct TinnotusAudio {
    ptr: *mut c_void,
}

unsafe impl Send for TinnotusAudio {}
unsafe impl Sync for TinnotusAudio {}

// ============================================================================
// FFI Declarations for Windows
// ============================================================================

#[link(name = "tinnotus_backend")]
#[link(native = "tinnotus_backend")]
extern "C" {
    /// Initialize the Tinnotus audio engine
    fn tinnotus_init() -> *mut c_void;
    
    /// Cleanup and shutdown the audio engine
    fn tinnotus_cleanup(handle: *mut c_void);
    
    /// Process audio through the current filter configuration
    fn tinnotus_process_audio(
        handle: *mut c_void,
        input: *const c_float,
        output: *mut c_float,
        num_frames: usize,
    ) -> c_int;
    
    /// Create a new notch filter
    fn tinnotus_create_notch(
        handle: *mut c_void,
        params: *const NotchParams,
    ) -> c_int;
    
    /// Remove a notch filter by index
    fn tinnotus_remove_notch(handle: *mut c_void, index: u32) -> c_int;
    
    /// Set notch filter parameters by index
    fn tinnotus_set_notch_params(
        handle: *mut c_void,
        index: u32,
        params: *const NotchParams,
    ) -> c_int;
    
    /// Set the tinnitus frequency to track
    fn tinnotus_set_pitch(
        handle: *mut c_void,
        frequency: c_double,
        q_factor: c_double,
    ) -> c_int;
    
    /// Detect pitch from audio
    fn tinnotus_analyze_signal(
        handle: *mut c_void,
        input: *const c_float,
        num_frames: usize,
    ) -> SignalDetected;
    
    /// Load an audio file
    fn tinnotus_load_file(handle: *mut c_void, path: *const c_char) -> c_int;
    
    /// Get an audio file's info
    fn tinnotus_get_file_info(handle: *mut c_void, path: *const c_char) -> *const AudioFileInfo;
    
    /// Save processed audio to file
    fn tinnotus_save_output(
        handle: *mut c_void,
        output: *const c_float,
        num_frames: usize,
        output_path: *const c_char,
    ) -> c_int;
    
    /// Get service statistics
    fn tinnotus_get_statistics(handle: *mut c_void) -> Statistics;
    
    /// Set audio settings
    fn tinnotus_set_audio_settings(
        handle: *mut c_void,
        sample_rate: c_int,
        buffer_size: usize,
        num_channels: c_int,
    ) -> c_int;
    
    /// Start audio processing
    fn tinnotus_start(handle: *mut c_void) -> c_int;
    
    /// Stop audio processing
    fn tinnotus_stop(handle: *mut c_void) -> c_int;
    
    /// Pause audio processing
    fn tinnotus_pause(handle: *mut c_void) -> c_int;
    
    /// Resume audio processing
    fn tinnotus_resume(handle: *mut c_void) -> c_int;
    
    /// Get current processing state
    fn tinnotus_get_state(handle: *mut c_void) -> c_int;
    
    /// Get audio device info
    fn tinnotus_get_device_info(handle: *mut c_void) -> *const AudioDeviceInfo;
    
    /// Get all available devices
    fn tinnotus_get_devices(handle: *mut c_void) -> *const AudioDeviceInfo;
    fn tinnotus_get_device_count(handle: *mut c_void) -> usize;
    
    /// Set input device
    fn tinnotus_set_input_device(handle: *mut c_void, index: u32) -> c_int;
    
    /// Set output device
    fn tinnotus_set_output_device(handle: *mut c_void, index: u32) -> c_int;
}

/// Audio device info (Windows WASAPI)
#[repr(C)]
pub struct AudioDeviceInfo {
    pub device_id: u32,
    pub is_default_input: c_int,
    pub is_default_output: c_int,
    pub name: *const c_char,
    pub sample_rate: c_int,
    pub buffer_frames: usize,
    pub num_input_channels: c_int,
    pub num_output_channels: c_int,
    pub state: c_int,  // 0 = not running, 1 = running
    pub format: c_int,  // WAVEFORMATEX format ID
    pub latency_ms: c_double,
}

impl AudioDeviceInfo {
    pub fn new() -> Self {
        Self {
            device_id: 0,
            is_default_input: 0,
            is_default_output: 0,
            name: ptr::null(),
            sample_rate: 44100,
            buffer_frames: 256,
            num_input_channels: 2,
            num_output_channels: 2,
            state: 0,
            format: 0,
            latency_ms: 0.0,
        }
    }
}

// ============================================================================
// Safe Rust API
// ============================================================================

pub struct TinnotusAudio {
    handle: *mut c_void,
}

impl TinnotusAudio {
    /// Create a new Tinnotus audio instance
    pub fn new() -> Self {
        unsafe {
            let handle = tinnotus_init();
            if handle.is_null() {
                panic!("Failed to initialize Tinnotus audio engine");
            }
            Self { handle }
        }
    }
    
    /// Cleanup resources
    pub fn close(self) {
        unsafe {
            tinnotus_cleanup(self.handle);
        }
    }
    
    /// Set notch filter frequency and bandwidth
    pub fn set_notch(&self, frequency: f64, bandwidth: f64) -> Result<(), TinnotusError> {
        let params = NotchParams {
            center_freq: frequency,
            bandwidth: bandwidth,
            sample_rate: 44100,
        };
        
        unsafe {
            let result = tinnotus_create_notch(self.handle, &params);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Add notch filter
    pub fn add_notch(&self, frequency: f64, bandwidth: f64) -> Result<u32, TinnotusError> {
        // For now, just delegate to set_notch
        self.set_notch(frequency, bandwidth)
    }
    
    /// Remove notch filter by index
    pub fn remove_notch(&self, index: u32) -> Result<(), TinnotusError> {
        unsafe {
            let result = tinnotus_remove_notch(self.handle, index);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Set notch parameters by index
    pub fn set_notch_params(&self, index: u32, frequency: f64, bandwidth: f64) -> Result<(), TinnotusError> {
        let params = NotchParams {
            center_freq: frequency,
            bandwidth: bandwidth,
            sample_rate: 44100,
        };
        
        unsafe {
            let result = tinnotus_set_notch_params(self.handle, index, &params);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Set the frequency to track
    pub fn set_pitch(&self, frequency: f64, q_factor: f64) -> Result<(), TinnotusError> {
        unsafe {
            let result = tinnotus_set_pitch(self.handle, frequency, q_factor);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Detect pitch from audio buffer
    pub fn detect_pitch(&self, samples: &[f32]) -> Result<SignalDetected, TinnotusError> {
        let input_ptr = samples.as_ptr();
        let num_samples = samples.len();
        
        unsafe {
            let result = tinnotus_analyze_signal(self.handle, input_ptr, num_samples);
            Ok(result)
        }
    }
    
    /// Process audio buffer through current filter configuration
    pub fn process_buffer(&self, input: &[f32], output: &mut [f32]) -> Result<usize, TinnotusError> {
        if input.len() != output.len() {
            return Err(TinnotusError::InvalidArgs);
        }
        
        let num_frames = input.len();
        let output_ptr = output.as_mut_ptr();
        let input_ptr = input.as_ptr();
        
        unsafe {
            let result = tinnotus_process_audio(self.handle, input_ptr, output_ptr, num_frames);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
            Ok(num_frames)
        }
    }
    
    /// Load audio file
    pub fn load_file(&self, path: &str) -> Result<(), TinnotusError> {
        let path_wide = path.as_bytes();
        // Windows needs wide string
        // For now, just use narrow string
        let path_c = std::ffi::CString::new(path).unwrap();
        unsafe {
            let result = tinnotus_load_file(self.handle, path_c.as_ptr());
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Save processed audio to file
    pub fn save_output(&self, samples: &[f32], path: &str) -> Result<(), TinnotusError> {
        let output_ptr = samples.as_ptr();
        let num_frames = samples.len();
        let path_c = std::ffi::CString::new(path).unwrap();
        
        unsafe {
            let result = tinnotus_save_output(self.handle, output_ptr, num_frames, path_c.as_ptr());
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Get service statistics
    pub fn get_statistics(&self) -> Statistics {
        unsafe {
            tinnotus_get_statistics(self.handle)
        }
    }
    
    /// Set audio settings
    pub fn set_sample_rate(&self, sample_rate: u32) -> Result<(), TinnotusError> {
        // For now, requires restart
        if sample_rate != 44100 {
            return Err(TinnotusError::NotImplemented);
        }
        Ok(())
    }
    
    /// Set buffer size
    pub fn set_buffer_size(&self, size: usize) -> Result<(), TinnotusError> {
        // For now, requires restart
        Ok(())
    }
    
    /// Start audio processing
    pub fn start(&self) -> Result<(), TinnotusError> {
        unsafe {
            let result = tinnotus_start(self.handle);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Stop audio processing
    pub fn stop(&self) -> Result<(), TinnotusError> {
        unsafe {
            let result = tinnotus_stop(self.handle);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Pause audio processing
    pub fn pause(&self) -> Result<(), TinnotusError> {
        unsafe {
            let result = tinnotus_pause(self.handle);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Resume audio processing
    pub fn resume(&self) -> Result<(), TinnotusError> {
        unsafe {
            let result = tinnotus_resume(self.handle);
            if result != 0 {
                return Err(TinnotusError::Failed);
            }
        }
        Ok(())
    }
    
    /// Get current processing state
    pub fn get_state(&self) -> AudioState {
        unsafe {
            let state = tinnotus_get_state(self.handle);
            match state {
                0 => AudioState::Idle,
                1 => AudioState::Initializing,
                2 => AudioState::Running,
                3 => AudioState::Paused,
                4 => AudioState::Stopped,
                5 => AudioState::Error,
                _ => AudioState::Unknown,
            }
        }
    }
}

// ============================================================================
// Error Type
// ============================================================================

#[derive(Debug, PartialEq)]
pub enum TinnotusError {
    Failed,
    InvalidArgs,
    NotImplemented,
    OutOfRange,
    InvalidAudio,
    DeviceUnavailable,
    Internal,
}

// ============================================================================
// Audio State
// ============================================================================

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum AudioState {
    Idle,
    Initializing,
    Running,
    Paused,
    Stopped,
    Error,
    Unknown,
}

// ============================================================================
// Test Helpers
// ============================================================================

/// Generate a sine wave
pub fn generate_sine_wave(frequency: f64, sample_rate: u32, duration_secs: f64) -> Vec<f32> {
    let num_samples = (duration_secs * sample_rate as f64) as usize;
    let samples = vec![0.0; num_samples];
    
    let (start, end) = generate_sine_wave_to_samples(frequency, sample_rate, duration_secs);
    
    for (i, sample) in samples.iter_mut().enumerate() {
        if i >= start && i < end {
            *sample = *start.get(i - start).unwrap();
        }
    }
    
    samples
}

/// Generate sine wave samples
fn generate_sine_wave_to_samples(frequency: f64, sample_rate: u32, duration_secs: f64) -> (Vec<f32>, usize) {
    let num_samples = (duration_secs * sample_rate as f64) as usize;
    let samples = (0..num_samples)
        .map(|i| {
            let phase = 2.0 * std::f64::consts::PI * frequency * i as f64 / sample_rate as f64;
            (phase.sin()) as f32
        })
        .collect();
    
    (samples, 0)
}

/// Generate sweep (frequency ramp)
pub fn generate_frequency_sweep(
    sample_rate: u32,
    start_freq: f64,
    end_freq: f64,
    duration_secs: f64,
) -> Vec<f32> {
    let num_samples = (duration_secs * sample_rate as f64) as usize;
    let samples = (0..num_samples)
        .map(|i| {
            let t = i as f64 / sample_rate as f64;
            let freq = start_freq + (end_freq - start_freq) * t / duration_secs;
            let phase = 2.0 * std::f64::consts::PI * freq * i as f64 / sample_rate as f64;
            (phase.sin()) as f32
        })
        .collect();
    
    (samples, 0)
}

// ============================================================================
// Convenience Functions
// ============================================================================

/// Create a basic Tinnotus audio instance
pub fn create_audio_engine() -> TinnotusAudio {
    TinnotusAudio::new()
}

/// Process audio through notch filter
pub fn apply_notch_filter(
    input: &[f32],
    frequency: f64,
    bandwidth: f64,
) -> Result<Vec<f32>, TinnotusError> {
    let engine = TinnotusAudio::new();
    engine.set_notch(frequency, bandwidth)?;
    let output = vec![0.0; input.len()];
    engine.process_buffer(input, &mut output)?;
    
    Ok(output)
}

/// Detect tinnitus frequency from audio
pub fn detect_tinnitus_frequency(samples: &[f32]) -> Result<SignalDetected, TinnotusError> {
    let engine = TinnotusAudio::new();
    engine.set_pitch(4000.0, 10.0)?;  // Default tinnitus frequency
    engine.detect_pitch(samples)
}