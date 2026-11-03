/**
 * @file audio_service.h
 * @brief Audio service for real-time and offline audio processing
 * 
 * This header provides the audio processing service that:
 * - Manages audio I/O across platforms
 * - Applies notch filters to audio streams
 * - Handles pitch detection and feedback
 * - Manages audio buffers and device selection
 * 
 * @version 0.1.0
 */

#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include "tinnotus.hpp"
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>

namespace tinnotus {
namespace audio {

// ============================================================================
// Forward Declarations
// ============================================================================

class AudioBuffer;
class AudioProcessor;
class AudioDeviceManager;
class AudioAnalyzer;
class PitchDetector;

// ============================================================================
// Audio Buffer
// ============================================================================

/**
 * @brief Ring buffer for audio data
 * 
 * Provides efficient circular buffer for audio streaming
 * with configurable capacity.
 */
class AudioBuffer {
public:
    /**
     * @brief Create audio buffer
     * @param sampleRate Target sample rate
     * @param maxFrames Maximum frames to store
     * @param channels Number of channels (1 = mono, 2 = stereo)
     */
    AudioBuffer(int sampleRate = 44100, int maxFrames = 4096, int channels = 2);
    
    ~AudioBuffer() = default;
    
    /**
     * @brief Push samples into buffer (circular write)
     */
    void push(const std::vector<float>& samples);
    
    /**
     * @brief Pull samples from buffer (circular read)
     */
    std::vector<float> pull(int maxFrames = 1024);
    
    /**
     * @brief Get number of samples written
     */
    size_t framesWritten() const { return framesWritten_; }
    
    /**
     * @brief Get number of samples available to read
     */
    size_t framesAvailable() const;
    
    /**
     * @brief Check if buffer is empty
     */
    bool isEmpty() const { return framesWritten_ == 0; }
    
    /**
     * @brief Check if buffer is full
     */
    bool isFull() const;
    
    /**
     * @brief Get current sample count
     */
    size_t size() const { return framesWritten_; }
    
    /**
     * @brief Clear buffer
     */
    void clear();
    
    /**
     * @brief Get sample rate
     */
    int getSampleRate() const { return sampleRate_; }
    
    /**
     * @brief Set sample rate (may require resampling)
     */
    bool setSampleRate(int newSampleRate);
    
    /**
     * @brief Get channels
     */
    int getChannels() const { return channels_; }
    
private:
    int sampleRate_;
    int maxFrames_;
    int channels_;
    std::vector<float> data_;
    std::mutex mutex_;
    size_t writeIndex_ = 0;
    size_t readIndex_ = 0;
    size_t framesWritten_ = 0;
};

// ============================================================================
// Signal Processing Engine
// ============================================================================

/**
 * @brief Central audio processing engine
 * 
 * Manages audio filters, processing pipeline, and output generation.
 * Supports both offline and real-time modes.
 */
class SignalProcessingEngine {
public:
    /**
     * @brief Create processing engine
     * @param sampleRate Target sample rate
     */
    SignalProcessingEngine(int sampleRate = 44100);
    
    ~SignalProcessingEngine() = default;
    
    /**
     * @brief Add notch filter
     */
    int addNotchFilter(double frequency, double bandwidth = 500.0);
    
    /**
     * @brief Remove notch filter by index
     */
    bool removeNotchFilter(int index);
    
    /**
     * @brief Replace all notch filters
     */
    void replaceNotchFilters(const std::vector<std::pair<double, double>>& filters);
    
    /**
     * @brief Process audio through all filters
     */
    void process(const float* input, float* output, int numFrames);
    
    /**
     * @brief Process sample through all filters
     */
    float processSample(float sample);
    
    /**
     * @brief Get notch filter at index
     */
    std::unique_ptr<NotchFilter> getNotchFilter(int index);
    
    /**
     * @brief Get number of notch filters
     */
    int getNotchCount() const { return notchFilters_.size(); }
    
    /**
     * @brief Set notch filter parameters
     */
    void setNotchParameters(int index, double frequency, double bandwidth);
    
    /**
     * @brief Apply low-pass filter
     */
    void applyLowPass(int sampleRate, double cutoffFreq = 20000.0, double slope = 12.0);
    
    /**
     * @brief Apply high-pass filter
     */
    void applyHighPass(int sampleRate, double cutoffFreq = 100.0, double slope = 12.0);
    
private:
    std::vector<std::unique_ptr<NotchFilter>> notchFilters_;
    std::vector<std::unique_ptr<tinnotus::dsp::LowPassFilter>> lowPassFilters_;
    std::vector<std::unique_ptr<tinnotus::dsp::HighPassFilter>> highPassFilters_;
    int sampleRate_;
    
    mutable std::mutex mutex_;
};

// ============================================================================
// Audio Device Manager
// ============================================================================

/**
 * @brief Base class for audio device management
 */
class AudioDeviceManager {
public:
    virtual ~AudioDeviceManager() = default;
    
    /**
     * @brief Initialize device
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Shutdown device
     */
    virtual void shutdown() = 0;
    
    /**
     * @brief Get default input device info
     */
    virtual AudioDeviceInfo getDefaultInputDevice() = 0;
    
    /**
     * @brief Get default output device info
     */
    virtual AudioDeviceInfo getDefaultOutputDevice() = 0;
    
    /**
     * @brief Get list of available devices
     */
    virtual std::vector<AudioDeviceInfo> getAllDevices() = 0;
    
    /**
     * @brief Set input device
     */
    virtual bool setInputDevice(const AudioDeviceInfo& info) = 0;
    
    /**
     * @brief Set output device
     */
    virtual bool setOutputDevice(const AudioDeviceInfo& info) = 0;
    
    /**
     * @brief Get current sample rate
     */
    virtual int getSampleRate() const = 0;
    
    /**
     * @brief Set sample rate (restart needed)
     */
    virtual bool setSampleRate(int sampleRate) = 0;
    
    /**
     * @brief Check if device is running
     */
    virtual bool isRunning() const = 0;
    
    /**
     * @brief Start device
     */
    virtual void start() = 0;
    
    /**
     * @brief Stop device
     */
    virtual void stop() = 0;
    
    /**
     * @brief Set buffer size (in frames)
     */
    virtual bool setBufferSize(int frames) = 0;
    
    /**
     * @brief Get current buffer size
     */
    virtual int getBufferSize() const = 0;
    
    /**
     * @brief Check for errors
     */
    virtual bool hasErrors() const = 0;
    
    /**
     * @brief Clear device errors
     */
    virtual void clearErrors() = 0;
};

// ============================================================================
// Platform-specific Device Implementations
// ============================================================================

/**
 * @brief Windows audio device (WASAPI)
 */
#if defined(TINNOTUS_PLATFORM_WINDOWS)
class WindowsAudioDevice : public AudioDeviceManager {
public:
    WindowsAudioDevice(int sampleRate = 44100, int bufferSize = 256);
    ~WindowsAudioDevice() override;
    
    bool initialize() override;
    void shutdown() override;
    AudioDeviceInfo getDefaultInputDevice() override;
    AudioDeviceInfo getDefaultOutputDevice() override;
    std::vector<AudioDeviceInfo> getAllDevices() override;
    bool setInputDevice(const AudioDeviceInfo&) override;
    bool setOutputDevice(const AudioDeviceInfo&) override;
    int getSampleRate() const override;
    bool setSampleRate(int sampleRate) override;
    bool isRunning() const override;
    void start() override;
    void stop() override;
    bool setBufferSize(int frames) override;
    int getBufferSize() const override;
    bool hasErrors() const override;
    void clearErrors() override;
    
protected:
    bool wasapiInitialized_;
    HANDLE audioEvent_;
    int actualSampleRate_;
    int actualBufferSize_;
};
#endif

/**
 * @brief macOS audio device (CoreAudio)
 */
#if defined(TINNOTUS_PLATFORM_MACOS)
class MacOsAudioDevice : public AudioDeviceManager {
public:
    MacOsAudioDevice(int sampleRate = 44100, int bufferSize = 256);
    ~MacOsAudioDevice() override;
    
    bool initialize() override;
    void shutdown() override;
    AudioDeviceInfo getDefaultInputDevice() override;
    AudioDeviceInfo getDefaultOutputDevice() override;
    std::vector<AudioDeviceInfo> getAllDevices() override;
    bool setInputDevice(const AudioDeviceInfo&) override;
    bool setOutputDevice(const AudioDeviceInfo&) override;
    int getSampleRate() const override;
    bool setSampleRate(int sampleRate) override;
    bool isRunning() const override;
    void start() override;
    void stop() override;
    bool setBufferSize(int frames) override;
    int getBufferSize() const override;
    bool hasErrors() const override;
    void clearErrors() override;
    
protected:
    std::unique_ptr<CoreAudioDevice> coreAudioDevice_;
    bool coreAudioInitialized_;
};
#endif

/**
 * @brief Linux audio device (ALSA/PulseAudio)
 */
#if defined(TINNOTUS_PLATFORM_LINUX)
class LinuxAudioDevice : public AudioDeviceManager {
public:
    LinuxAudioDevice(int sampleRate = 44100, int bufferSize = 256);
    ~LinuxAudioDevice() override;
    
    bool initialize() override;
    void shutdown() override;
    AudioDeviceInfo getDefaultInputDevice() override;
    AudioDeviceInfo getDefaultOutputDevice() override;
    std::vector<AudioDeviceInfo> getAllDevices() override;
    bool setInputDevice(const AudioDeviceInfo&) override;
    bool setOutputDevice(const AudioDeviceInfo&) override;
    int getSampleRate() const override;
    bool setSampleRate(int sampleRate) override;
    bool isRunning() const override;
    void start() override;
    void stop() override;
    bool setBufferSize(int frames) override;
    int getBufferSize() const override;
    bool hasErrors() const override;
    void clearErrors() override;
    
protected:
    bool alsaInitialized_;
    bool pulseAudioInitialized_;
};
#endif

// ============================================================================
// Audio Service (Main Controller)
// ============================================================================

/**
 * @brief Main audio service class
 * 
 * Orchestrates all audio-related operations:
 * - Device management
 * - Signal processing
 * - Pitch detection
 * - Notch filtering
 * - Playback/recording
 */
class AudioService {
public:
    enum class Mode {
        Record,
        Playback,
        RecordPlayback,  // Full-duplex
    };
    
    enum class State {
        Idle,
        Initializing,
        Running,
        Paused,
        Stopped,
        Error
    };
    
    /**
     * @brief Create audio service
     */
    static std::unique_ptr<AudioService> create(
        int sampleRate = 44100,
        Audio::Mode mode = Audio::Mode::Playback);
    
    ~AudioService();
    
    /**
     * @brief Initialize service
     */
    bool initialize();
    
    /**
     * @brief Shutdown service
     */
    void shutdown();
    
    /**
     * @brief Set audio mode
     */
    void setMode(Mode mode);
    
    /**
     * @brief Get current state
     */
    State getState() const { return state_; }
    
    /**
     * @brief Start processing
     */
    bool start();
    
    /**
     * @brief Stop processing
     */
    void stop();
    
    /**
     * @brief Pause/resume processing
     */
    bool pause();
    bool resume();
    
    /**
     * @brief Check if processing is active
     */
    bool isRunning() const { 
        std::lock_guard<std::mutex> lock(stateMutex_);
        return state_ == State::Running; 
    }
    
    /**
     * @brief Set notch filter
     */
    bool setNotchFilter(double frequency, double bandwidth = 500.0);
    
    /**
     * @brief Add notch filter
     */
    bool addNotchFilter(double frequency, double bandwidth = 500.0);
    
    /**
     * @brief Remove notch by index
     */
    bool removeNotchFilter(int index);
    
    /**
     * @brief Get notch filter at index
     */
    std::unique_ptr<NotchFilter> getNotchFilter(int index);
    
    /**
     * @brief Get all notch frequencies
     */
    std::vector<double> getNotchFrequencies() const;
    
    /**
     * @brief Set pitch detection
     */
    bool setPitchDetection(double frequency, double bandwidth = 500.0);
    
    /**
     * @brief Detect pitch from audio buffer
     */
    double detectPitch(const float* audioBuffer, int numSamples);
    
    /**
     * @brief Process audio input
     */
    void setInput(const float* input, int numFrames);
    
    /**
     * @brief Get audio output
     */
    bool getOutput(float* output, int maxFrames);
    
    /**
     * @brief Load audio file for processing
     */
    bool loadFile(const std::string& filepath);
    
    /**
     * @brief Process loaded file
     */
    bool processFile();
    
    /**
     * @brief Analyze current audio for tinnitus
     */
    TinnitusResult analyzeTinnitus();
    
    /**
     * @brief Get service statistics
     */
    ServiceStatistics getStatistics() const;
    
    /**
     * @brief Set callback for pitch changes
     */
    void onPitchChanged(std::function<void(double, double)> callback) {
        pitchChangeCallback_ = std::move(callback);
    }
    
    /**
     * @brief Get log messages
     */
    const std::vector<std::string>& getLogs() const { return logs_; }
    
private:
    std::unique_ptr<AudioDeviceManager> device_;
    std::unique_ptr<SignalProcessingEngine> engine_;
    std::unique_ptr<AudioBuffer> inputBuffer_;
    std::unique_ptr<AudioBuffer> outputBuffer_;
    std::unique_ptr<PitchDetector> pitchDetector_;
    std::unique_ptr<TinnitusDetector> tinnitusDetector_;
    
    Mode mode_ = Mode::Playback;
    State state_ = State::Idle;
    
    std::vector<std::unique_ptr<NotchFilter>> notchFilters_;
    std::vector<std::pair<double, double>> notchParams_;
    std::vector<double> notchFrequencies_;
    
    std::function<void(double, double)> pitchChangeCallback_;
    
    int sampleRate_ = 44100;
    int bufferSize_ = 256;
    
    mutable std::mutex stateMutex_;
    mutable std::condition_variable stateCV_;
    
    std::vector<std::string> logs_;
    
    void log(const std::string& message);
};

// ============================================================================
// Tinnitus Analysis Result
// ============================================================================

/**
 * @brief Tinnitus detection and analysis result
 */
struct TinnitusResult {
    bool detected;
    double frequency;  // Hz
    double confidence;  // 0.0 - 1.0
    double intensity;  // Amplitude estimate
    std::string characteristics;  // "continuous", "fluctuating", etc.
    
    TinnitusResult() : detected(false), frequency(0.0), 
                      confidence(0.0), intensity(0.0) {}
};

/**
 * @brief Audio service statistics
 */
struct ServiceStatistics {
    size_t framesProcessed;
    size_t framesOutput;
    double latencyMs;
    double averageLatencyMs;
    double processingTimeUs;
    size_t totalNotchCount;
    double activeFrequency;
    double activeConfidence;
    std::vector<double> peakFrequencies;
    
    ServiceStatistics() : framesProcessed(0), framesOutput(0), 
                         latencyMs(0.0), averageLatencyMs(0.0),
                         processingTimeUs(0.0), totalNotchCount(0),
                         activeFrequency(0.0), activeConfidence(0.0) {}
};

} // namespace audio
} // namespace tinnotus

#endif // AUDIO_SERVICE_H