//! Tinnotus Shared Definitions
//!
//! This crate provides C-compatible FFI structures for cross-platform binding
//! of the Tinnotus audio processing engine.

mod constants;
mod models;

// Re-exports for convenience
pub use constants::*;
pub use models::*;

// ============================================================================
// Version Information
// ============================================================================

/// Tinnotus version information
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Version {
    /// Major version number
    pub major: u32,
    /// Minor version number
    pub minor: u32,
    /// Patch version number
    pub patch: u32,
}

impl Default for Version {
    fn default() -> Self {
        Self {
            major: 0,
            minor: 1,
            patch: 0,
        }
    }
}

impl std::fmt::Display for Version {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}.{}.{}", self.major, self.minor, self.patch)
    }
}

// ============================================================================
// Build Information
// ============================================================================

/// Platform detection macros
#[cfg(target_os = "windows")]
#[macro_export]
macro_rules! TINNOTUS_PLATFORM_WINDOWS { () => {} }

#[cfg(target_os = "macos")]
#[macro_export]
macro_rules! TINNOTUS_PLATFORM_MACOS { () => {} }

#[cfg(target_os = "linux")]
#[macro_export]
macro_rules! TINNOTUS_PLATFORM_LINUX { () => {} }

#[cfg(target_os = "ios")]
#[macro_export]
macro_rules! TINNOTUS_PLATFORM_IOS { () => {} }

#[cfg(target_os = "android")]
#[macro_export]
macro_rules! TINNOTUS_PLATFORM_ANDROID { () => {} }

// ============================================================================
// Audio Constants
// ============================================================================

/// Standard audio sample rates
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SampleRate {
    /// 44.1 kHz (CD quality)
    CD = 44100,
    /// 48 kHz (Professional audio)
    PRO = 48000,
    /// 96 kHz (High resolution)
    HD = 96000,
    /// 192 kHz (Ultra high resolution)
    ULTRA = 192000,
}

/// Standard bit depths
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BitDepth {
    /// 8-bit (Very low quality)
    U8 = 8,
    /// 16-bit (CD quality)
    S16 = 16,
    /// 24-bit (Professional)
    S24 = 24,
    /// 32-bit float
    F32 = 32,
}

/// Standard channel formats
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ChannelFormat {
    /// Mono (1 channel)
    Mono = 1,
    /// Stereo (2 channels)
    Stereo = 2,
    /// 5.1 Surround (6 channels)
    Surround51 = 6,
    /// 7.1 Surround (8 channels)
    Surround71 = 8,
}

// ============================================================================
// Tinnitus Frequency Ranges
// ============================================================================

/// Typical tinnitus frequency range
#[repr(C)]
pub const TITINNITUS_FREQ_MIN: f64 = 100.0;
#[repr(C)]
pub const TITINNITUS_FREQ_MAX: f64 = 8000.0;

/// Very low frequency tinnitus range (more rare)
#[repr(C)]
pub const TITINNITUS_FREQ_LOW_MIN: f64 = 10.0;
#[repr(C)]
pub const TITINNITUS_FREQ_LOW_MAX: f64 = 50.0;

// ============================================================================
// FFI Structures
// ============================================================================

/// Notch filter parameters
/// 
/// A notch filter attenuates a specific frequency band.
/// Used to suppress tinnitus frequency.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct NotchParams {
    /// Center frequency of the notch (Hz)
    pub center_freq: f64,
    /// Bandwidth of the notch (Hz)
    /// Controls how wide the attenuation band is
    pub bandwidth: f64,
    /// Sample rate for filter calculations
    pub sample_rate: i32,
    /// Quality factor (Q), typically 10-50 for tinnitus
    pub q_factor: f64,
}

impl Default for NotchParams {
    fn default() -> Self {
        Self {
            center_freq: 4000.0,  // Default tinnitus frequency
            bandwidth: 500.0,
            sample_rate: 44100,
            q_factor: 10.0,
        }
    }
}

/// List of notch filters
/// 
/// A notch list manages multiple notch filters for bimodal tinnitus.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct NotchList {
    /// Number of active notch filters
    pub count: usize,
    /// Array of notch filter parameters
    pub notches: [Notch; 8],  // Support up to 8 simultaneous notches
}

impl Default for NotchList {
    fn default() -> Self {
        Self {
            count: 0,
            notches: [Notch { freq: 0.0, bandwidth: 500.0 }; 8],
        }
    }
}

/// Single notch filter
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Notch {
    /// Frequency (Hz)
    pub freq: f64,
    /// Bandwidth (Hz)
    pub bandwidth: f64,
}

/// Audio buffer
/// 
/// Contains sample data for audio processing.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct AudioBuffer {
    /// Sample rate of the audio
    pub sample_rate: i32,
    /// Number of channels
    pub num_channels: i32,
    /// Number of frames (each frame contains one sample per channel)
    pub num_frames: usize,
    /// Pointer to sample data
    pub samples: *mut f32,
}

/// Signal detection result
/// 
/// Result of pitch detection algorithm.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct SignalDetected {
    /// Whether a signal was detected
    pub detected: i32,  // 0 = not detected, 1 = detected
    /// Detected frequency (Hz)
    pub frequency: f64,
    /// Confidence (0.0 - 1.0)
    pub confidence: f64,
    /// Signal intensity (amplitude)
    pub intensity: f64,
}

/// Service statistics
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Statistics {
    /// Number of input frames processed
    pub frames_processed: usize,
    /// Number of output frames generated
    pub frames_output: usize,
    /// Current processing latency (ms)
    pub latency_ms: f64,
    /// Average processing latency (ms)
    pub average_latency_ms: f64,
    /// Processing time per sample (us)
    pub processing_time_us: f64,
    /// Total number of notch filters
    pub total_notch_count: usize,
    /// Active tracking frequency
    pub active_frequency: f64,
    /// Confidence of active tracking
    pub active_confidence: f64,
}

/// Audio device information
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct AudioDeviceInfo {
    /// Device identifier
    pub device_id: u32,
    /// Whether this is the default input device
    pub is_default_input: i32,
    /// Whether this is the default output device
    pub is_default_output: i32,
    /// Device name (UTF-8 string)
    pub name: *const std::os::raw::c_char,
    /// Sample rate (Hz)
    pub sample_rate: i32,
    /// Buffer size in frames
    pub buffer_frames: usize,
    /// Number of input channels
    pub num_input_channels: i32,
    /// Number of output channels
    pub num_output_channels: i32,
    /// Device state
    pub state: i32,  // 0 = not running, 1 = running, 2 = error
}

// ============================================================================
// FFI Functions (Platform-specific, defined below)
// ============================================================================

/// Initialize the Tinnotus audio engine
/// 
/// Returns a handle to the audio engine.
/// On error, returns NULL.
#[inline]
pub fn tinnotus_init() -> *mut tinnotus_audio_engine {
    // Platform-specific implementation
    #[cfg(feature = "c-interface")]
    {
        tinnotus_init_impl()
    }
}

#[cfg(not(feature = "c-interface"))]
#[inline]
pub fn tinnotus_init() -> *mut tinnotus_audio_engine {
    // Fallback stub
    std::ptr::null_mut()
}

fn tinnotus_init_impl() -> *mut tinnotus_audio_engine {
    tinnotus::audio::init_engine().ok().map(|engine| {
        Box::into_raw(Box::new(engine))
    }).unwrap_or(std::ptr::null_mut())
}

/// Cleanup and shutdown the audio engine
#[inline]
pub fn tinnotus_cleanup(handle: *mut tinnotus_audio_engine) {
    if !handle.is_null() {
        let _ = unsafe { Box::from_raw(handle) };
    }
}

/// Process audio through the current filter configuration
#[inline]
pub fn tinnotus_process_audio(
    handle: *mut tinnotus_audio_engine,
    input: *const f32,
    output: *mut f32,
    num_frames: usize,
) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Create a new notch filter
/// 
/// # Arguments
/// * `params` - Notch filter parameters
/// 
/// # Returns
/// Index of the newly created notch filter, or -1 on error
#[inline]
pub fn tinnotus_create_notch(
    handle: *mut tinnotus_audio_engine,
    params: *const NotchParams,
) -> i32 {
    // Implementation
    if handle.is_null() || params.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Remove a notch filter by index
/// 
/// # Arguments
/// * `index` - Index of the notch filter to remove
/// 
/// # Returns
/// 0 on success, -1 on error
#[inline]
pub fn tinnotus_remove_notch(
    handle: *mut tinnotus_audio_engine,
    index: u32,
) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Set notch filter parameters by index
#[inline]
pub fn tinnotus_set_notch_params(
    handle: *mut tinnotus_audio_engine,
    index: u32,
    params: *const NotchParams,
) -> i32 {
    // Implementation
    if handle.is_null() || params.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Set the tinnitus frequency to track
#[inline]
pub fn tinnotus_set_pitch(
    handle: *mut tinnotus_audio_engine,
    frequency: f64,
    q_factor: f64,
) -> i32 {
    // Implementation
    0  // Success
}

/// Detect pitch from audio
#[inline]
pub fn tinnotus_analyze_signal(
    handle: *mut tinnotus_audio_engine,
    input: *const f32,
    num_frames: usize,
) -> SignalDetected {
    // Implementation
    SignalDetected {
        detected: 0,
        frequency: 0.0,
        confidence: 0.0,
        intensity: 0.0,
    }
}

/// Load an audio file
#[inline]
pub fn tinnotus_load_file(
    handle: *mut tinnotus_audio_engine,
    path: *const std::os::raw::c_char,
) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Get an audio file's info
#[inline]
pub fn tinnotus_get_file_info(
    handle: *mut tinnotus_audio_engine,
    path: *const std::os::raw::c_char,
) -> *const AudioFileInfo {
    // Implementation
    std::ptr::null()
}

/// Save processed audio to file
#[inline]
pub fn tinnotus_save_output(
    handle: *mut tinnotus_audio_engine,
    output: *const f32,
    num_frames: usize,
    output_path: *const std::os::raw::c_char,
) -> i32 {
    // Implementation
    0  // Success
}

/// Get service statistics
#[inline]
pub fn tinnotus_get_statistics(
    handle: *mut tinnotus_audio_engine,
) -> Statistics {
    // Implementation
    Statistics {
        frames_processed: 0,
        frames_output: 0,
        latency_ms: 0.0,
        average_latency_ms: 0.0,
        processing_time_us: 0.0,
        total_notch_count: 0,
        active_frequency: 0.0,
        active_confidence: 0.0,
    }
}

/// Set audio settings
#[inline]
pub fn tinnotus_set_audio_settings(
    handle: *mut tinnotus_audio_engine,
    sample_rate: i32,
    buffer_size: usize,
    num_channels: i32,
) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Start audio processing
#[inline]
pub fn tinnotus_start(handle: *mut tinnotus_audio_engine) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Stop audio processing
#[inline]
pub fn tinnotus_stop(handle: *mut tinnotus_audio_engine) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Pause audio processing
#[inline]
pub fn tinnotus_pause(handle: *mut tinnotus_audio_engine) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Resume audio processing
#[inline]
pub fn tinnotus_resume(handle: *mut tinnotus_audio_engine) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Get current processing state
#[inline]
pub fn tinnotus_get_state(handle: *mut tinnotus_audio_engine) -> i32 {
    // Implementation
    0  // Idle
}

/// Get audio device info
#[inline]
pub fn tinnotus_get_device_info(
    handle: *mut tinnotus_audio_engine,
) -> *const AudioDeviceInfo {
    // Implementation
    std::ptr::null()
}

/// Get all available devices
#[inline]
pub fn tinnotus_get_devices(
    handle: *mut tinnotus_audio_engine,
) -> *const AudioDeviceInfo {
    // Implementation
    std::ptr::null()
}

/// Get device count
#[inline]
pub fn tinnotus_get_device_count(
    handle: *mut tinnotus_audio_engine,
) -> usize {
    // Implementation
    0
}

/// Set input device by index
#[inline]
pub fn tinnotus_set_input_device(
    handle: *mut tinnotus_audio_engine,
    index: u32,
) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

/// Set output device by index
#[inline]
pub fn tinnotus_set_output_device(
    handle: *mut tinnotus_audio_engine,
    index: u32,
) -> i32 {
    // Implementation
    if handle.is_null() {
        return -1;  // Error
    }
    0  // Success
}

// ============================================================================
// Audio Engine Type
// ============================================================================

/// Audio engine handle
#[repr(C)]
struct tinnotus_audio_engine {
    // Internal implementation details
    _private: [u8; 256],
}

// ============================================================================
// Helper Functions
// ============================================================================

/// Convert frequency to radians
#[inline]
pub fn hz_to_radians(frequency: f64, sample_rate: i32) -> f64 {
    2.0 * std::f64::consts::PI * frequency as f64 / sample_rate as f64
}

/// Convert radians to frequency
#[inline]
pub fn radians_to_hz(radians: f64, sample_rate: i32) -> f64 {
    radians / (2.0 * std::f64::consts::PI) * sample_rate as f64
}

/// Convert frequency to MIDI note number
#[inline]
pub fn freq_to_midi(freq: f64) -> i32 {
    let midi = 69.0 + 12.0 * (freq / 440.0 - 1.0).ln() / std::f64::consts::LN_2;
    midi as i32
}

/// Convert MIDI note number to frequency
#[inline]
pub fn midi_to_freq(midi: i32) -> f64 {
    440.0 * (2.0_f64.powf((midi as f64 - 69.0) / 12.0))
}

// ============================================================================
// Test Helper Functions
// ============================================================================

/// Generate a sine wave
#[inline]
pub fn generate_sine_wave(
    frequency: f64,
    sample_rate: i32,
    duration_secs: f64,
) -> Vec<f32> {
    let num_samples = (duration_secs * sample_rate as f64) as usize;
    (0..num_samples)
        .map(|i| {
            let t = i as f64 / sample_rate as f64;
            let phase = 2.0 * std::f64::consts::PI * frequency * t;
            (phase.sin()) as f32
        })
        .collect()
}

/// Generate frequency sweep
#[inline]
pub fn generate_frequency_sweep(
    sample_rate: i32,
    start_freq: f64,
    end_freq: f64,
    duration_secs: f64,
) -> Vec<f32> {
    let num_samples = (duration_secs * sample_rate as f64) as usize;
    (0..num_samples)
        .map(|i| {
            let t = i as f64 / sample_rate as f64;
            let freq = start_freq + (end_freq - start_freq) * t / duration_secs;
            let phase = 2.0 * std::f64::consts::PI * freq * t;
            (phase.sin()) as f32
        })
        .collect()
}