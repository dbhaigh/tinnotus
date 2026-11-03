/**
 * @file platform.hpp
 * @brief Platform detection and audio device type definitions
 * 
 * This header provides platform-specific macros for audio device implementations.
 * Use this header to detect the current platform and select the appropriate
 * audio backend.
 */

#ifndef PLATFORM_HPP
#define PLATFORM_HPP

#include <string>

// ============================================================================
// Platform Detection Macros
// ============================================================================

/**
 * @brief Define the target platform
 * 
 * These macros are set by CMake based on the build configuration:
 * - TINNOTUS_PLATFORM_WINDOWS: Windows
 * - TINNOTUS_PLATFORM_MACOS: macOS
 * - TINNOTUS_PLATFORM_LINUX: Linux
 * - TINNOTUS_PLATFORM_IOS: iOS
 * - TINNOTUS_PLATFORM_ANDROID: Android
 */
#if defined(_WIN32) || defined(_WIN64)
    #define TINNOTUS_PLATFORM_WINDOWS
    #define TINNOTUS_PLATFORM "Windows"
#elseif defined(__APPLE__) && defined(__MACH__)
    #define TINNOTUS_PLATFORM_MACOS
    #define TINNOTUS_PLATFORM "macOS"
#elseif defined(__ANDROID__)
    #define TINNOTUS_PLATFORM_ANDROID
    #define TINNOTUS_PLATFORM "Android"
#elseif defined(__has_include) && __has_include(<TargetConditionals.h>)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define TINNOTUS_PLATFORM_IOS
        #define TINNOTUS_PLATFORM "iOS"
    #endif
#elseif defined(__linux__) || defined(__unix__)
    #define TINNOTUS_PLATFORM_LINUX
    #define TINNOTUS_PLATFORM "Linux"
#else
    #error "Unsupported platform for Tinnotus"
#endif

// ============================================================================
// Audio Device Type Definitions
// ============================================================================

/**
 * @brief Base audio device type
 * 
 * Identifies the type of audio device:
 * - Input: Microphone or line-in
 * - Output: Speakers or headphones
 */
enum class AudioDeviceType {
    Input,      // Microphone/line-in
    Output      // Speakers/headphones
};

/**
 * @brief Audio device interface
 * 
 * All audio device implementations inherit from this interface.
 */
class AudioDeviceInterface {
public:
    virtual ~AudioDeviceInterface() = default;
    
    /**
     * @brief Get device name
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Get device type
     */
    virtual AudioDeviceType getType() const = 0;
    
    /**
     * @brief Get device description
     */
    virtual std::string getDescription() const = 0;
    
    /**
     * @brief Get default sample rate for this device
     */
    virtual int getDefaultSampleRate() const = 0;
    
    /**
     * @brief Get minimum buffer size supported
     */
    virtual int getMinimumBufferSize() const = 0;
    
    /**
     * @brief Get maximum buffer size supported
     */
    virtual int getMaximumBufferSize() const = 0;
    
    /**
     * @brief Check if device is available
     */
    virtual bool isAvailable() const = 0;
    
    /**
     * @brief Initialize device
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Shutdown device
     */
    virtual void shutdown() = 0;
    
protected:
    std::string name_;
};

// ============================================================================
// Platform-Specific Device Implementations
// ============================================================================

/**
 * @brief Windows audio device (WASAPI)
 * 
 * Uses Windows WASAPI for audio I/O.
 * 
 * Note: WASAPI is complex and requires COM interfaces.
 * See audio_device_windows.cpp for implementation.
 */
#if defined(TINNOTUS_PLATFORM_WINDOWS)
class WindowsAudioDevice : public AudioDeviceInterface {
public:
    WindowsAudioDevice(int sampleRate = 44100, int bufferSize = 256);
    ~WindowsAudioDevice() override;
    
    bool initialize() override;
    void shutdown() override;
    std::string getName() const override { return name_; }
    AudioDeviceType getType() const override;
    std::string getDescription() const override { return "Windows WASAPI Device"; }
    int getDefaultSampleRate() const override;
    int getMinimumBufferSize() const override;
    int getMaximumBufferSize() const override;
    bool isAvailable() const override;
    
    // Signal processing callback
    using ProcessCallback = std::function<void(const float* input, float* output, int numFrames)>;
    void setProcessCallback(ProcessCallback callback);
    void processAudio(const float* input, float* output, int numFrames) override;
    
protected:
    bool wasapiInitialized_;
    HANDLE audioEvent_;
    int actualSampleRate_;
    int actualBufferSize_;
    ProcessCallback processCallback_;
};
#endif

/**
 * @brief macOS audio device (CoreAudio)
 * 
 * Uses CoreAudio for audio I/O.
 * 
 * Note: CoreAudio is well-documented but verbose.
 * See audio_device_macos.cpp for implementation.
 */
#if defined(TINNOTUS_PLATFORM_MACOS)
class MacOsAudioDevice : public AudioDeviceInterface {
public:
    MacOsAudioDevice(int sampleRate = 44100, int bufferSize = 256);
    ~MacOsAudioDevice() override;
    
    bool initialize() override;
    void shutdown() override;
    std::string getName() const override { return name_; }
    AudioDeviceType getType() const override;
    std::string getDescription() const override { return "macOS CoreAudio Device"; }
    int getDefaultSampleRate() const override;
    int getMinimumBufferSize() const override;
    int getMaximumBufferSize() const override;
    bool isAvailable() const override;
    
    // Signal processing callback
    using ProcessCallback = std::function<void(const float* input, float* output, int numFrames)>;
    void setProcessCallback(ProcessCallback callback);
    void processAudio(const float* input, float* output, int numFrames) override;
    
protected:
    std::unique_ptr<CoreAudioDevice> coreAudioDevice_;
    bool coreAudioInitialized_;
    ProcessCallback processCallback_;
};

/**
 * @brief CoreAudio device helper
 * 
 * Internal class for managing CoreAudio session, stream, and callbacks.
 */
class CoreAudioDevice {
public:
    CoreAudioDevice(int sampleRate, int bufferSize);
    ~CoreAudioDevice() = default;
    
    bool initialize();
    void shutdown();
    bool isRunning() const;
    
    // Sample rate info
    int getSampleRate() const { return sampleRate_; }
    int getSampleBufferSize() const { return bufferSize_; }
    
protected:
    OSStatus audioRenderCallback(
        void* inRefCon,
        AudioUnit inRenderer,
        AudioUnitRenderActionFlags* ioActionFlags,
        UInt32 inNumberFrames,
        const AudioTimeStamp* inTimeStamp);
    
    int sampleRate_;
    int bufferSize_;
    AudioDeviceID deviceID_;
    AudioStreamBasicDescription streamDescription_;
};
#endif

/**
 * @brief Linux audio device (ALSA/PulseAudio)
 * 
 * Uses ALSA or PulseAudio for audio I/O.
 * Preference: ALSA for direct access, PulseAudio for system-wide access.
 * 
 * See audio_device_linux.cpp for implementation.
 */
#if defined(TINNOTUS_PLATFORM_LINUX)
class LinuxAudioDevice : public AudioDeviceInterface {
public:
    LinuxAudioDevice(int sampleRate = 44100, int bufferSize = 256,
                    const std::string& device = "");
    ~LinuxAudioDevice() override;
    
    bool initialize() override;
    void shutdown() override;
    std::string getName() const override { return name_; }
    AudioDeviceType getType() const override;
    std::string getDescription() const override { return "Linux ALSA/PulseAudio Device"; }
    int getDefaultSampleRate() const override;
    int getMinimumBufferSize() const override;
    int getMaximumBufferSize() const override;
    bool isAvailable() const override;
    
    // Signal processing callback
    using ProcessCallback = std::function<void(const float* input, float* output, int numFrames)>;
    void setProcessCallback(ProcessCallback callback);
    void processAudio(const float* input, float* output, int numFrames) override;
    
protected:
    bool alsaInitialized_;
    bool pulseAudioInitialized_;
    ProcessCallback processCallback_;
    snd_pcm_t* snd_pcm_;
};
#endif

/**
 * @brief iOS audio device (AVFoundation)
 * 
 * Uses AVFoundation for audio I/O.
 * 
 * Note: Requires Obj-C++ implementation for AVAudioEngine integration.
 * See audio_device_ios.mm for implementation (stub).
 */
#if defined(TINNOTUS_PLATFORM_IOS)
class IosAudioDevice : public AudioDeviceInterface {
public:
    IosAudioDevice(int sampleRate = 44100, int bufferSize = 256);
    ~IosAudioDevice() override;
    
    bool initialize() override;
    void shutdown() override;
    std::string getName() const override { return name_; }
    AudioDeviceType getType() const override;
    std::string getDescription() const override { return "iOS AVFoundation Device"; }
    int getDefaultSampleRate() const override;
    int getMinimumBufferSize() const override;
    int getMaximumBufferSize() const override;
    bool isAvailable() const override;
    
    // Signal processing callback
    using ProcessCallback = std::function<void(const float* input, float* output, int numFrames)>;
    void setProcessCallback(ProcessCallback callback);
    void processAudio(const float* input, float* output, int numFrames) override;
    
protected:
    bool avFoundationInitialized_;
    ProcessCallback processCallback_;
    // AVAudioEngine references would go here in Obj-C++ implementation
};

/**
 * @brief CoreAudio device for iOS (for testing)
 * 
 * iOS can use CoreAudio for testing on Mac.
 */
class CoreAudioDevice; // Forward declaration
#endif

/**
 * @brief Android audio device (OpenSL ES)
 * 
 * Uses OpenSL ES for audio I/O.
 * 
 * Note: AAudio is a newer, simpler API that may be preferred for modern devices.
 * See audio_device_android.cpp for implementation (stub).
 */
#if defined(TINNOTUS_PLATFORM_ANDROID)
class AndroidAudioDevice : public AudioDeviceInterface {
public:
    AndroidAudioDevice(int sampleRate = 44100, int bufferSize = 256);
    ~AndroidAudioDevice() override;
    
    bool initialize() override;
    void shutdown() override;
    std::string getName() const override { return name_; }
    AudioDeviceType getType() const override;
    std::string getDescription() const override { return "Android OpenSL ES Device"; }
    int getDefaultSampleRate() const override;
    int getMinimumBufferSize() const override;
    int getMaximumBufferSize() const override;
    bool isAvailable() const override;
    
    // Signal processing callback
    using ProcessCallback = std::function<void(const float* input, float* output, int numFrames)>;
    void setProcessCallback(ProcessCallback callback);
    void processAudio(const float* input, float* output, int numFrames) override;
    
protected:
    sl_engineID_t engine_;
    sl_playerID_t player_;
    sl_recorderID_t recorder_;
    ProcessCallback processCallback_;
};
#endif

// ============================================================================
// Platform Device Factory
// ============================================================================

/**
 * @brief Create the appropriate audio device for the current platform
 * 
 * @param sampleRate Desired sample rate
 * @param bufferSize Desired buffer size
 * @return std::unique_ptr<AudioDeviceInterface> Platform-specific device
 */
inline std::unique_ptr<AudioDeviceInterface> createAudioDevice(
    int sampleRate = 44100,
    int bufferSize = 256)
{
#if defined(TINNOTUS_PLATFORM_WINDOWS)
    return std::make_unique<WindowsAudioDevice>(sampleRate, bufferSize);
#elif defined(TINNOTUS_PLATFORM_MACOS)
    return std::make_unique<MacOsAudioDevice>(sampleRate, bufferSize);
#elif defined(TINNOTUS_PLATFORM_LINUX)
    return std::make_unique<LinuxAudioDevice>(sampleRate, bufferSize);
#elif defined(TINNOTUS_PLATFORM_IOS)
    return std::make_unique<IosAudioDevice>(sampleRate, bufferSize);
#elif defined(TINNOTUS_PLATFORM_ANDROID)
    return std::make_unique<AndroidAudioDevice>(sampleRate, bufferSize);
#else
    #error "No audio device implementation for this platform"
#endif
}

// ============================================================================
// Error Codes
// ============================================================================

/**
 * @brief Audio device error codes
 */
enum class AudioDeviceError {
    Success = 0,
    InitializationFailed = 1,
    ShutdownFailed = 2,
    SampleRateMismatch = 3,
    BufferSizeOutOfBounds = 4,
    DeviceUnavailable = 5,
    AudioCallbackError = 6,
    StartFailed = 7,
    StopFailed = 8,
    BufferUnderrun = 9,
    BufferOverflow = 10
};

// ============================================================================
// Platform Constants
// ============================================================================

#if defined(TINNOTUS_PLATFORM_WINDOWS)
    constexpr int WINDOWS_MIN_SAMPLES_PER_BUFFER = 256;
    constexpr int WINDOWS_MAX_SAMPLES_PER_BUFFER = 10240;
    constexpr int WINDOWS_DEFAULT_SAMPLE_RATE = 44100;
    
    constexpr const char* WINDOWS_DEFAULT_INPUT_DEVICE = "Default Audio Input";
    constexpr const char* WINDOWS_DEFAULT_OUTPUT_DEVICE = "Default Audio Output";
#elif defined(TINNOTUS_PLATFORM_MACOS)
    constexpr int MACOS_MIN_SAMPLES_PER_BUFFER = 512;
    constexpr int MACOS_MAX_SAMPLES_PER_BUFFER = 512; // Fixed buffer on macOS
    constexpr int MACOS_DEFAULT_SAMPLE_RATE = 44100;
    
    constexpr const char* MACOS_DEFAULT_INPUT_DEVICE = "Built-in Input";
    constexpr const char* MACOS_DEFAULT_OUTPUT_DEVICE = "Built-in Output";
#elif defined(TINNOTUS_PLATFORM_LINUX)
    constexpr int LINUX_MIN_SAMPLES_PER_BUFFER = 512;
    constexpr int LINUX_MAX_SAMPLES_PER_BUFFER = 10240;
    constexpr int LINUX_DEFAULT_SAMPLE_RATE = 44100;
    
    constexpr const char* LINUX_DEFAULT_INPUT_DEVICE = "default";
    constexpr const char* LINUX_DEFAULT_OUTPUT_DEVICE = "default";
#elif defined(TINNOTUS_PLATFORM_IOS)
    constexpr int IOS_MIN_SAMPLES_PER_BUFFER = 1024; // Minimum required by iOS
    constexpr int IOS_MAX_SAMPLES_PER_BUFFER = 1024; // iOS has fixed buffers
    constexpr int IOS_DEFAULT_SAMPLE_RATE = 44100;
    
    constexpr const char* IOS_DEFAULT_INPUT_DEVICE = "Built-in Microphone";
    constexpr const char* IOS_DEFAULT_OUTPUT_DEVICE = "Built-in Earphones";
#elif defined(TINNOTUS_PLATFORM_ANDROID)
    constexpr int ANDROID_MIN_SAMPLES_PER_BUFFER = 512;
    constexpr int ANDROID_MAX_SAMPLES_PER_BUFFER = 10240;
    constexpr int ANDROID_DEFAULT_SAMPLE_RATE = 44100;
    
    constexpr const char* ANDROID_DEFAULT_OUTPUT_DEVICE = "Default";
#endif

#endif // PLATFORM_HPP