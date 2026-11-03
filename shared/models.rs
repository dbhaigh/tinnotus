//! Rust type definitions for Tinnotus audio processing
//! 
//! These types mirror the C++ FFI structures and provide safe Rust APIs.

use serde::{Deserialize, Serialize};

// ============================================================================
// Version Information
// ============================================================================

/// Tinnotus version information
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Version {
    pub major: u32,
    pub minor: u32,
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
// Audio Constants
// ============================================================================

/// Standard audio sample rates
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

impl Default for SampleRate {
    fn default() -> Self {
        Self::CD
    }
}

/// Standard bit depths
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
// FFF Structures
// ============================================================================

/// Notch filter parameters
/// 
/// A notch filter attenuates a specific frequency band.
/// Used to suppress tinnitus frequency.
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

impl Serialize for NotchParams {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where S: serde::Serializer {
        serde_json::json!({
            "center_freq": self.center_freq,
            "bandwidth": self.bandwidth,
            "sample_rate": self.sample_rate,
            "q_factor": self.q_factor
        }).serialize(serializer)
    }
}

impl<'de> Deserialize<'de> for NotchParams {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where D: serde::Deserializer<'de> {
        let (center, bw, sr) = serde_json::from_value(
            serde_json::Value::deserialize(deserializer)?
        )?;
        Ok(Self {
            center_freq: center as f64,
            bandwidth: bw,
            sample_rate: sr as i32,
            q_factor: 10.0,  // Default
        })
    }
}

/// List of notch filters
/// 
/// A notch list manages multiple notch filters for bimodal tinnitus.
#[derive(Debug, Clone, Default)]
pub struct NotchList {
    /// Number of active notch filters
    pub count: usize,
    /// Array of notch filter parameters
    pub notches: Vec<Notch>,
}

impl NotchList {
    /// Create a new empty notch list
    pub fn new() -> Self {
        Self {
            count: 0,
            notches: Vec::new(),
        }
    }
    
    /// Create a notch list with initial parameters
    pub fn with_notches(count: usize) -> Self {
        let mut notches = Vec::with_capacity(std::cmp::min(count, 8));
        for i in 0..count {
            let (freq, bw) = tinnotus::pitch_detector::auto_detect_tinnitus_frequency(i);
            notches.push(Notch { freq, bandwidth: bw });
        }
        Self { count, notches }
    }
    
    /// Add a notch filter
    pub fn add_notch(&mut self, freq: f64, bandwidth: f64) -> Result<(), String> {
        // Validate frequency range
        if freq < 100.0 || freq > 8000.0 {
            return Err(format!(
                "Frequency {} out of range (100-8000 Hz)",
                freq
            ));
        }
        
        // Check if already exists
        if self.notches.iter().any(|n| n.freq == freq) {
            return Err(format!("Notch at {} Hz already exists", freq));
        }
        
        // Check maximum notches (typically 8 for bimodal tinnitus)
        if self.notches.len() >= 8 {
            return Err("Maximum notch count (8) reached".to_string());
        }
        
        self.notches.push(Notch { freq, bandwidth });
        self.count = self.notches.len();
        Ok(())
    }
    
    /// Remove a notch filter by index
    pub fn remove_notch(&mut self, index: usize) -> Result<(), String> {
        if index < self.count {
            self.notches.remove(index);
            self.count = self.notches.len();
            Ok(())
        } else {
            Err(format!("Notch index {} out of range", index))
        }
    }
    
    /// Get notch by index
    pub fn get_notch(&self, index: usize) -> Option<&Notch> {
        self.notches.get(index)
    }
    
    /// Get notch by frequency
    pub fn get_notch_by_freq(&self, freq: f64) -> Option<&Notch> {
        self.notches.iter().find(|n| (n.freq - freq).abs() < 1.0)
    }
    
    /// Clear all notch filters
    pub fn clear(&mut self) {
        self.notches.clear();
        self.count = 0;
    }
    
    /// Replace all notch filters
    pub fn set_notches(&mut self, notches: Vec<(f64, f64)>) {
        self.notches = notches
            .into_iter()
            .map(|(freq, bw)| Notch { freq, bandwidth: bw })
            .collect();
        self.count = self.notches.len();
    }
}

impl Serialize for NotchList {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where S: serde::Serializer {
        let notches = self.notches
            .iter()
            .map(|n| (n.freq, n.bandwidth))
            .collect::<Vec<(f64, f64)>>();
        serde_json::json!({
            "count": self.count,
            "notches": notches
        }).serialize(serializer)
    }
}

impl<'de> Deserialize<'de> for NotchList {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where D: serde::Deserializer<'de> {
        let value = serde_json::from_value(serde_json::Value::deserialize(deserializer)?)?;
        let count = value["count"].as_usize().unwrap_or(0);
        let notches_data: Vec<(f64, f64)> = serde_json::from_value(value["notches"].clone())?;
        
        let mut notches = Vec::with_capacity(std::cmp::min(count, notches_data.len()));
        for (freq, bw) in notches_data.iter().take(count) {
            notches.push((freq, bw));
        }
        
        Ok(Self { count, notches })
    }
}

/// Single notch filter
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
#[derive(Debug, Clone)]
pub struct AudioBuffer {
    /// Sample rate of the audio
    pub sample_rate: i32,
    /// Number of channels
    pub num_channels: i32,
    /// Number of frames (each frame contains one sample per channel)
    pub num_frames: usize,
    /// Pointer to sample data
    pub samples: Vec<f32>,
}

impl AudioBuffer {
    /// Create an empty audio buffer
    pub fn new(sample_rate: i32, num_channels: i32, num_frames: usize) -> Self {
        let num_samples = num_frames * num_channels;
        Self {
            sample_rate,
            num_channels,
            num_frames,
            samples: vec![0.0; num_samples],
        }
    }
    
    /// Create audio buffer with pre-filled samples
    pub fn with_samples(
        sample_rate: i32, 
        num_channels: i32, 
        num_frames: usize, 
        samples: Vec<f32>
    ) -> Self {
        let num_samples = num_frames * num_channels;
        Self {
            sample_rate,
            num_channels,
            num_frames,
            samples,
        }
    }
    
    /// Create audio buffer from WAV file
    pub fn from_wav(path: &str) -> Result<Self, String> {
        use hound::{WavReader, WavHeader};
        
        let header = WavReader::open(path)
            .map_err(|e| format!("Failed to open WAV file: {}", e))?;
        
        let sample_rate = header.sample_rate() as i32;
        let num_channels = header.channels() as i32;
        let bits_per_sample = header.bits_per_sample();
        
        // Convert to frames
        let num_frames = if bits_per_sample == 32 {
            // 32-bit WAV is in native endianness
            header.samples() as usize
        } else {
            // Downsample from WAV sample count
            header.samples() as usize
        };
        
        // Read samples
        let samples: Vec<f32> = header.read_samples()
            .map_err(|e| format!("Failed to read WAV samples: {}", e))?
            .map(|s| {
                match bits_per_sample {
                    16 => s,
                    24 => s,
                    32 => s,
                    8 => (s * 127.0) as f32,
                    bits => {
                        // Unsupported bit depth
                        return 0.0;
                    }
                }
            })
            .collect();
        
        Ok(AudioBuffer {
            sample_rate,
            num_channels,
            num_frames,
            samples,
        })
    }
    
    /// Save to WAV file
    pub fn save_wav(&self, path: &str) -> Result<(), String> {
        use hound::WavWriter;
        
        let sample_rate = self.sample_rate as f32;
        let num_channels = self.num_channels as f32;
        let sample_width = match self.sample_rate {
            8 => 1,
            16 => 2,
            24 => 3,
            32 => 4,
            _ => 4,  // Default to 32-bit float
        };
        
        let writer = WavWriter::create(path, sample_rate, num_channels, sample_width)
            .map_err(|e| format!("Failed to create WAV writer: {}", e))?;
        
        for sample in &self.samples {
            let buffer = [sample];
            writer.write_samplef(&buffer)
                .map_err(|e| format!("Failed to write WAV sample: {}", e))?;
        }
        
        writer.finalize()
            .map_err(|e| format!("Failed to finalize WAV writer: {}", e))?;
        
        Ok(())
    }
}

/// Signal detection result
/// 
/// Result of pitch detection algorithm.
#[derive(Debug, Clone, Copy)]
pub struct SignalDetected {
    /// Whether a signal was detected
    pub detected: bool,
    /// Detected frequency (Hz)
    pub frequency: f64,
    /// Confidence (0.0 - 1.0)
    pub confidence: f64,
    /// Signal intensity (amplitude)
    pub intensity: f64,
}

impl SignalDetected {
    /// Check if signal is within typical tinnitus range
    pub fn is_tinnitus(&self) -> bool {
        self.frequency >= 100.0 && self.frequency <= 8000.0
    }
    
    /// Check if signal is within very low frequency range
    pub fn is_low_freq(&self) -> bool {
        self.frequency >= 10.0 && self.frequency <= 50.0
    }
}

/// Service statistics
#[derive(Debug, Clone)]
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

impl Default for Statistics {
    fn default() -> Self {
        Self {
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
}

// ============================================================================
// Audio Device Information
// ============================================================================

/// Audio device information
#[derive(Debug, Clone)]
pub struct AudioDeviceInfo {
    /// Device identifier
    pub device_id: u32,
    /// Device name
    pub name: String,
    /// Whether this is the default input device
    pub is_default_input: bool,
    /// Whether this is the default output device
    pub is_default_output: bool,
    /// Sample rate (Hz)
    pub sample_rate: i32,
    /// Buffer size in frames
    pub buffer_frames: usize,
    /// Number of input channels
    pub num_input_channels: i32,
    /// Number of output channels
    pub num_output_channels: i32,
    /// Device state
    pub state: DeviceState,
    /// Driver name
    pub driver: String,
}

impl AudioDeviceInfo {
    /// Create a default device info
    pub fn new() -> Self {
        Self {
            device_id: 0,
            name: "Default".to_string(),
            is_default_input: true,
            is_default_output: true,
            sample_rate: 44100,
            buffer_frames: 256,
            num_input_channels: 2,
            num_output_channels: 2,
            state: DeviceState::Unknown,
            driver: String::new(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum DeviceState {
    Unknown,
    Running,
    Paused,
    Stopped,
    Error,
}

impl std::fmt::Display for DeviceState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            DeviceState::Unknown => write!(f, "Unknown"),
            DeviceState::Running => write!(f, "Running"),
            DeviceState::Paused => write!(f, "Paused"),
            DeviceState::Stopped => write!(f, "Stopped"),
            DeviceState::Error => write!(f, "Error"),
        }
    }
}

// ============================================================================
// DSP Types
// ============================================================================

/// Filter types
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum FilterType {
    Notch,
    LowPass,
    HighPass,
    BandPass,
    AllPass,
    FIRNotch,
    Biquad,
}

/// Pitch detection algorithm
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PitchAlgorithm {
    /// YIN - Autocorrelation-based (default)
    YIN,
    /// Goertzel - Optimized for known frequency bins
    Goertzel,
    /// FFT - Spectral peak detection
    FFT,
    /// Auto - Select best algorithm automatically
    Auto,
}

/// Pitch detection result
#[derive(Debug, Clone)]
pub struct PitchResult {
    /// Detected frequency (Hz)
    pub frequency: f64,
    /// Confidence (0.0 - 1.0)
    pub confidence: f64,
    /// Algorithm used
    pub algorithm: PitchAlgorithm,
    /// Processing time (ms)
    pub processing_time_ms: f64,
}

/// Frequency range for pitch detection
#[derive(Debug, Clone)]
pub struct FrequencyRange {
    /// Minimum frequency (Hz)
    pub min: f64,
    /// Maximum frequency (Hz)
    pub max: f64,
    /// Default pitch (MIDI note number)
    pub default_pitch: i32,
}

impl Default for FrequencyRange {
    fn default() -> Self {
        Self {
            min: 100.0,
            max: 8000.0,
            default_pitch: 69,  // A4 = 440 Hz
        }
    }
}

// ============================================================================
// Audio Processing Engine
// ============================================================================

/// Audio processing engine state
#[derive(Debug, Clone, PartialEq)]
pub enum EngineState {
    /// Engine is initialized but not running
    Idle,
    /// Engine is initializing
    Initializing,
    /// Engine is processing audio
    Running,
    /// Engine is paused
    Paused,
    /// Engine is stopped
    Stopped,
    /// Engine has an error
    Error,
}

/// Audio processing engine
#[derive(Debug)]
pub struct AudioEngine {
    /// Current state
    pub state: EngineState,
    /// Sample rate
    pub sample_rate: i32,
    /// Number of channels
    pub num_channels: i32,
    /// Buffer size
    pub buffer_size: usize,
    /// Active notch filters
    pub notch_filters: Vec<NotchParams>,
    /// Active low-pass filters
    pub low_pass_filters: Vec<(f64, f64)>,  // (cutoff, slope)
    /// Active high-pass filters
    pub high_pass_filters: Vec<(f64, f64)>,  // (cutoff, slope)
    /// Detected signals
    pub detected_signals: Vec<SignalDetected>,
    /// Output buffer
    pub output_buffer: AudioBuffer,
}

impl Default for AudioEngine {
    fn default() -> Self {
        Self {
            state: EngineState::Idle,
            sample_rate: 44100,
            num_channels: 2,
            buffer_size: 256,
            notch_filters: Vec::new(),
            low_pass_filters: Vec::new(),
            high_pass_filters: Vec::new(),
            detected_signals: Vec::new(),
            output_buffer: AudioBuffer::new(44100, 2, 0),
        }
    }
}