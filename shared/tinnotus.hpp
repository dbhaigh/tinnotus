/**
 * @file tinnotus.hpp
 * @brief Tinnotus shared definitions for cross-platform compatibility
 * 
 * This file provides common type definitions, constants, and FFI structures
 * for the Tinnotus audio processing library. It's designed to be included by
 * both C++ backend and platform-specific code (Rust, Swift, Kotlin).
 * 
 * @version 0.1.0
 */

#ifndef TINNOTUS_HPP
#define TINNOTUS_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace tinnotus {

/**
 * @brief Version information
 */
struct Version {
    static const int MAJOR;
    static const int MINOR;
    static const int PATCH;
};

// Platform detection
#define TINNOTUS_PLATFORM_WINDOWS (defined(_WIN32) || defined(_WIN64))
#define TINNOTUS_PLATFORM_MACOS (defined(__APPLE__) && defined(__MACH__))
#define TINNOTUS_PLATFORM_LINUX (defined(__linux__))
#define TINNOTUS_PLATFORM_IOS (defined(__APPLE__) && defined(__IPHONE_OS_VERSION_MAX_ALLOWED))
#define TINNOTUS_PLATFORM_ANDROID (defined(__ANDROID__))

#ifdef TINNOTUS_PLATFORM_WINDOWS
    #define TINNOTUS_PLATFORM "windows"
#elif defined(TINNOTUS_PLATFORM_MACOS)
    #define TINNOTUS_PLATFORM "macos"
#elif defined(TINNOTUS_PLATFORM_LINUX)
    #define TINNOTUS_PLATFORM "linux"
#elif defined(TINNOTUS_PLATFORM_IOS)
    #define TINNOTUS_PLATFORM "ios"
#else
    #define TINNOTUS_PLATFORM "unknown"
#endif

// ============================================================================
// Audio Constants
// ============================================================================

/**
 * @brief Standard sample rates
 */
enum class SampleRate {
    RATE_44100 = 44100,  // CD quality
    RATE_48000 = 48000,  // Professional
    RATE_96000 = 96000,  // High res
    RATE_192000 = 192000 // Hi-res audiophile
};

/**
 * @brief Audio bit depths
 */
enum class BitDepth {
    BITS_16 = 16,    // 16-bit
    BITS_24 = 24,    // 24-bit
    BITS_32 = 32,    // 32-bit float
    BITS_FLOAT = 32  // 32-bit float
};

/**
 * @brief Audio channels
 */
enum class ChannelFormat {
    CHANNEL_MONO = 1,  // Mono
    CHANNEL_STEREO = 2 // Stereo
};

/**
 * @brief Audio format (combines bit depth and channels)
 */
struct AudioFormat {
    BitDepth depth;
    ChannelFormat channels;
    int sampleRate;
};

// ============================================================================
// Frequency ranges for tinnitus
// ============================================================================

/**
 * @brief Tinnitus frequency range in Hz
 * 
 * Most tinnitus falls within 500 Hz to 10,000 Hz range.
 * Human hearing sensitivity is highest around 1-5 kHz.
 */
constexpr double MIN_TINNITUS_FREQ = 100.0;  // Lower bound for practical detection
constexpr double MAX_TINNITUS_FREQ = 8000.0; // Upper bound (most common tinnitus range)
constexpr double DEFAULT_Q_FACTOR = 10.0;   // Quality factor for notch filter

// ============================================================================
// Math utilities (inline for header)
// ============================================================================

namespace math {

constexpr double PI = 3.14159265358979323846;
constexpr double SQRT2 = 1.41421356237309504880;

/**
 * @brief Convert frequency (Hz) to angular frequency (radians/sample)
 * 
 * omega = 2 * PI * frequency / sampleRate
 */
inline double hzToRadians(double frequency, int sampleRate) {
    return (2.0 * PI * frequency) / sampleRate;
}

/**
 * @brief Convert angular frequency (radians/sample) to frequency (Hz)
 */
inline double radiansToHz(double omega, int sampleRate) {
    return (omega * sampleRate) / (2.0 * PI);
}

/**
 * @brief Convert frequency to octaves
 */
inline double frequencyToOctaves(double frequency) {
    if (frequency <= 0.0) return -1.0;
    return std::log2(frequency);
}

/**
 * @brief Calculate notch filter coefficients (IIR, SOS format)
 * 
 * @param centerFreq Center frequency in Hz
 * @param bandwidth Bandwidth in Hz (controls Q factor)
 * @param sampleRate Sample rate
 * @return std::vector<std::vector<double>> Matrix of 3x2 coefficient matrices
 */
inline std::vector<std::vector<double>> calculateNotchCoefficients(
    double centerFreq, double bandwidth, int sampleRate) {

    std::vector<std::vector<double>> sos;
    double omega = hzToRadians(centerFreq, sampleRate);
    double Q = bandwidth / (2.0 * std::log(2.0) / bandwidth * sampleRate);
    
    double alpha = std::sin(omega * bandwidth / (2.0 * PI * sampleRate));
    double cos_omega = std::cos(omega);
    
    std::vector<double> stage(6);
    stage[0] = 1.0;
    stage[1] = -2.0 * cos_omega;
    stage[2] = 1.0;
    stage[3] = -2.0 * alpha * cos_omega;
    stage[4] = alpha * alpha;
    stage[5] = 0.0;  // For notch, b3, b4, b5 = 0
    
    sos.push_back({stage[0], stage[1], stage[2], stage[3], stage[4], stage[5]});
    return sos;
}

/**
 * @brief Calculate FIR notch filter coefficients
 * 
 * @param centerFreq Center frequency in Hz
 * @param bandwidth Bandwidth in Hz
 * @param sampleRate Sample rate
 * @return std::vector<double> Coefficients
 */
inline std::vector<double>> calculateFirNotchCoefficients(
    double centerFreq, double bandwidth, int sampleRate, 
    int filterOrder = 64) {

    int numTaps = filterOrder;
    double omega = hzToRadians(centerFreq, sampleRate);
    double H0 = (numTaps - 1) * omega / PI;
    
    std::vector<double> h(numTaps);
    
    for (int n = 0; n < numTaps; n++) {
        double omega_n = 2.0 * PI * n / numTaps;
        
        if (std::abs(omega_n - omega) < bandwidth / 100.0) {
            h[n] = 0.0;  // Null in notch region
        } else {
            h[n] = 1.0;  // Pass in passband
        }
    }
    
    // Apply window and shift
    double sum = 0.0;
    for (double val : h) sum += val;
    
    for (int n = 0; n < numTaps; n++) {
        h[n] = (2.0 / sum) * h[n] * std::cos(omega * (n - numTaps / 2));
    }
    
    return h;
}

/**
 * @brief Calculate Butterworth bandpass filter coefficients
 */
inline std::vector<double>> calculateButterworthCoefficients(
    double lowFreq, double highFreq, int sampleRate,
    double rollOff = 12.0) {
    
    // This is a placeholder - proper implementation uses bilinear transform
    // and Butterworth polynomial approximation
    return {};
}

} // namespace math

// ============================================================================
// FFI Structures for Rust/Swift/Kotlin binding
// ============================================================================

// FFI must use C-compatible types (no std::string, no std::vector, etc.)

/**
 * @brief Notch filter parameters
 */
struct NotchParams {
    double centerFreq;  // Center frequency in Hz
    double bandwidth;   // Bandwidth in Hz
    int sampleRate;     // Sample rate
    
    // FFI-safe defaults
    NotchParams() : centerFreq(DEFAULT_TINNITUS_FREQ), 
                   bandwidth(DEFAULT_Q_FACTOR), 
                   sampleRate(44100) {}
};

/**
 * @brief Multiple notch configuration
 */
struct NotchList {
    int count;
    struct {
        double freq;
        double bandwidth;
    } notches[8];  // Support up to 8 simultaneous notches
    
    NotchList() : count(0) {
        for (int i = 0; i < 8; i++) {
            notches[i].freq = 0.0;
            notches[i].bandwidth = DEFAULT_Q_FACTOR;
        }
    }
};

/**
 * @brief Audio buffer structure
 */
struct AudioBuffer {
    int sampleRate;
    int numChannels;
    size_t numFrames;
    float* samples;  // Flat buffer: interleaved samples
    
    AudioBuffer() : sampleRate(44100), numChannels(2), numFrames(0), samples(nullptr) {}
    ~AudioBuffer() { delete[] samples; }
};

/**
 * @brief Audio file info
 */
struct FileInfo {
    std::string path;
    int sampleRate;
    int numChannels;
    double durationSec;
    size_t numSamples;
    
    bool isValid() const {
        return !path.empty() && sampleRate > 0 && numChannels > 0 && numSamples > 0;
    }
};

/**
 * @brief Signal detection result
 */
struct SignalDetected {
    bool detected;
    double frequency;    // In Hz
    double confidence;   // 0.0 - 1.0
    double intensity;    // Amplitude estimate
    
    SignalDetected() : detected(false), frequency(0.0), 
                      confidence(0.0), intensity(0.0) {}
};

// ============================================================================
// Platform-specific includes
// ============================================================================

#ifdef TINNOTUS_PLATFORM_WINDOWS
    #include <windows.h>
    #include <audioclient.h>
    #include <mmdeviceapi.h>
#endif

#ifdef TINNOTUS_PLATFORM_MACOS
    #include <CoreAudio/CoreAudio.h>
    #include <CoreAudio/AudioDeviceAPI.h>
    #include <AudioToolbox/AudioToolbox.h>
#endif

#ifdef TINNOTUS_PLATFORM_LINUX
    #ifdef __linux__
        #include <alsa/asoundlib.h>
    #else
        #ifdef __unix__
            #include <pulse/pulseaudio.h>
        #endif
    #endif
#endif

#ifdef TINNOTUS_PLATFORM_IOS
    #import <AVFAudio/AVFAudio.h>
    #import <AVFoundation/AVFoundation.h>
#endif

#ifdef TINNOTUS_PLATFORM_ANDROID
    #include <android/log.h>
    #define __LOG_TAG__ "Tinnotus"
    #define __LOG(fmt, ...) \
        __android_log_print(ANDROID_LOG_INFO, __LOG_TAG__, fmt, ##__VA_ARGS__)
#endif

} // namespace tinnotus

#endif // TINNOTUS_HPP