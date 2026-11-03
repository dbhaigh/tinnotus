//! Shared constants for Tinnotus audio processing

/// Tinnitus frequency ranges (Hz)
pub const TITINNITUS_FREQ_MIN: f64 = 100.0;
pub const TITINNITUS_FREQ_MAX: f64 = 8000.0;

/// Very low frequency tinnitus range (10-50 Hz, rare)
pub const TITINNITUS_FREQ_LOW_MIN: f64 = 10.0;
pub const TITINNITUS_FREQ_LOW_MAX: f64 = 50.0;

/// Standard sample rates
pub const SAMPLE_RATE_44100: i32 = 44100;
pub const SAMPLE_RATE_48000: i32 = 48000;
pub const SAMPLE_RATE_96000: i32 = 96000;