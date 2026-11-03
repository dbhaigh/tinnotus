/**
 * @file platform_linux.hpp
 * @brief Linux platform definitions for Tinnotus
 * 
 * This header provides Linux-specific definitions needed by the
 * audio backend implementation.
 */

#ifndef PLATFORM_LINUX_HPP
#define PLATFORM_LINUX_HPP

// ============================================================================
// Platform Detection
// ============================================================================

// Linux platform detection
#if defined(__linux__) || defined(__unix__) || defined(__CYGWIN__)
    #ifndef TINNOTUS_PLATFORM_LINUX
        #error "This header requires Linux platform to be defined"
    #endif
#endif

// ============================================================================
// Linux Audio Library Detection
// ============================================================================

/**
 * @brief Check if ALSA is available
 */
#if defined(HAVE_ALSA) || defined(ALSA_FOUND)
    #define TINNOTUS_ALSA_AVAILABLE
#endif

/**
 * @brief Check if PulseAudio is available
 */
#if defined(HAVE_PULSEAUDIO) || defined(PA_AVAILABLE)
    #define TINNOTUS_PULSEAUDIO_AVAILABLE
#endif

// ============================================================================
// ALSA Constants
// ============================================================================

#if defined(TINNOTUS_ALSA_AVAILABLE)
    
// Audio access types
enum AlsaAccessType {
    SND_PCM_ACCESS_MMAP_INTERLEAVED,
    SND_PCM_ACCESS_MMAP_NONINTERLEAVED,
    SND_PCM_ACCESS_RW_INTERLEAVED,
    SND_PCM_ACCESS_RW_NONINTERLEAVED
};

// Audio formats
enum AlsaFormat {
    SND_PCM_FORMAT_U8,
    SND_PCM_FORMAT_S8,
    SND_PCM_FORMAT_U16_LE,
    SND_PCM_FORMAT_S16_LE,
    SND_PCM_FORMAT_U16_BE,
    SND_PCM_FORMAT_S16_BE,
    SND_PCM_FORMAT_U24_LE,
    SND_PCM_FORMAT_S24_LE,
    SND_PCM_FORMAT_U24_BE,
    SND_PCM_FORMAT_S24_BE,
    SND_PCM_FORMAT_U32_LE,
    SND_PCM_FORMAT_S32_LE,
    SND_PCM_FORMAT_U32_BE,
    SND_PCM_FORMAT_S32_BE,
    SND_PCM_FORMAT_FLOAT_LE,
    SND_PCM_FORMAT_FLOAT_PULSE,
    SND_PCM_FORMAT_FLOAT32_LE,
    SND_PCM_FORMAT_FLOAT32_BE,
    SND_PCM_FORMAT_FLOAT64_LE,
    SND_PCM_FORMAT_FLOAT64_BE,
    SND_PCM_FORMAT_S24_80_LE,
    SND_PCM_FORMAT_S24_80_BE,
    SND_PCM_FORMAT_U32_80_LE,
    SND_PCM_FORMAT_U32_80_BE,
    SND_PCM_FORMAT_S32_80_LE,
    SND_PCM_FORMAT_S32_80_BE,
    SND_PCM_FORMAT_U64_LE,
    SND_PCM_FORMAT_S64_LE,
    SND_PCM_FORMAT_U64_BE,
    SND_PCM_FORMAT_S64_BE
};

// Sample rates
constexpr int LINUX_SAMPLE_RATE_44100 = 44100;
constexpr int LINUX_SAMPLE_RATE_48000 = 48000;
constexpr int LINUX_SAMPLE_RATE_96000 = 96000;

// Default sample rate
constexpr int LINUX_DEFAULT_SAMPLE_RATE = 44100;

// Channel counts
constexpr int LINUX_MIN_CHANNELS = 1;
constexpr int LINUX_MAX_CHANNELS = 8;

// Buffer sizes
constexpr int LINUX_MIN_BUFFER_SIZE = 512;
constexpr int LINUX_MAX_BUFFER_SIZE = 10240;

// Buffer time constants
constexpr int LINUX_MIN_BUFFER_TIME_MS = 50;  // 50ms
constexpr int LINUX_MAX_BUFFER_TIME_MS = 500; // 500ms

#endif // TINNOTUS_ALSA_AVAILABLE

// ============================================================================
// PulseAudio Constants
// ============================================================================

#if defined(TINNOTUS_PULSEAUDIO_AVAILABLE)
    
// Sample specifications
struct PasaSampleSpec {
    pa_sample_format_t format;
    unsigned int rate;
    unsigned int channels;
};

// Buffer attributes
struct PasaBufferAttr {
    pa_usec_t delay;
    pa_usec_t tinfo;
    pa_uint32_t minreq;
    pa_uint32_t maxover;
    pa_uint32_t maxrequest;
    pa_uint32_t maxframedelay;
    int32_t minrequest;
    int32_t overrequest;
};

// Default sample specification
constexpr PasaSampleSpec LINUX_DEFAULT_SAMPLE_SPEC = {
    .format = PA_SAMPLE_FLOAT32LE,
    .rate = 44100,
    .channels = 2
};

// Default sample rate
constexpr int LINUX_DEFAULT_PULSE_SAMPLE_RATE = 44100;

// Buffer sizes
constexpr int LINUX_MIN_PULSE_BUFFER_SIZE = 1024;
constexpr int LINUX_MAX_PULSE_BUFFER_SIZE = 10240;

#endif // TINNOTUS_PULSEAUDIO_AVAILABLE

// ============================================================================
// Linux Audio Device Interface
// ============================================================================

/**
 * @brief Linux audio device (ALSA/PulseAudio)
 * 
 * Implements the AudioDeviceInterface base class with Linux-specific
 * ALSA/PulseAudio implementation for audio I/O.
 */
struct LinuxAudioDevice {
    // Constructor
    LinuxAudioDevice(
        int sampleRate = 44100,
        int bufferSize = 512,
        const std::string& deviceId = "",
        const std::string& deviceName = ""
    );
    
    // Destructor
    ~LinuxAudioDevice();
    
    // Initialize device (try ALSA first, then PulseAudio)
    bool initialize();
    
    // Shutdown device
    void shutdown();
    
    // Device info
    std::string getName() const { return "Linux Linux Audio Device"; }
    
    AudioDeviceType getType() const;
    
    std::string getDescription() const { return "Linux ALSA/PulseAudio Device"; }
    
    // Get sample rate
    int getSampleRate() const {
        return sampleRate_;
    }
    
    // Set sample rate (requires restart)
    bool setSampleRate(int sampleRate) {
        if (sampleRate < getMinimumBufferSize() || 
            sampleRate > getMaximumBufferSize()) {
            return false;
        }
        sampleRate_ = sampleRate;
        
        // For ALSA, requires restart
        if (alsaInitialized_) {
            return shutdown();
        }
        return true;
    }
    
    // Buffer size
    int getBufferSize() const {
        return bufferSize_;
    }
    
    // Set buffer size
    bool setBufferSize(int bufferSize) {
        if (bufferSize < getMinimumBufferSize() || 
            bufferSize > getMaximumBufferSize()) {
            return false;
        }
        bufferSize_ = bufferSize;
        
        // For ALSA, may require restart
        if (alsaInitialized_) {
            return true; // Will validate on next start
        }
        return true;
    }
    
    // Check if running
    bool isRunning() const {
        return isRunning_;
    }
    
    // Start device
    void start() {
        // Start ALSA/PulseAudio device
    }
    
    // Stop device
    void stop() {
        // Stop ALSA/PulseAudio device
    }
    
    // Check for errors
    bool hasErrors() const {
        return lastError_ != "No error";
    }
    
    // Clear errors
    void clearErrors() {
        lastError_ = "No error";
    }
    
    // Get last error
    std::string getLastError() const {
        return lastError_;
    }
    
    // Signal processing callback
    using ProcessCallback = std::function<void(const float* input, float* output, int numFrames)>;
    void setProcessCallback(ProcessCallback callback);
    
    void processAudio(const float* input, float* output, int numFrames);
    
    // Get available devices
    std::vector<AudioDeviceInfo> getAvailableDevices();
    
protected:
    // Device configuration
    int sampleRate_ = 44100;
    int bufferSize_ = 512;
    int channels_ = 2;
    
    std::string deviceId_ = "";
    std::string deviceName_ = "";
    
    // Initialization state
    bool alsaInitialized_ = false;
    bool pulseAudioInitialized_ = false;
    bool isRunning_ = false;
    
    // Error tracking
    std::string lastError_ = "No error";
    
    // Audio thread
    std::thread audioThread_;
    std::atomic<bool> audioThreadRunning_ = false;
    std::atomic<bool> audioThreadStart_ = false;
    
    // ALSA resources
    snd_pcm_t* snd_pcm_ = nullptr;
    snd_timer_t* snd_timer_ = nullptr;
    
    // PulseAudio resources
    pa_context* paContext_ = nullptr;
    pa_stream* paStream_ = nullptr;
    pa_simple* paSimple_ = nullptr;
    
    // Process callback
    ProcessCallback processCallback_;
    
    // Device selection
    AudioDeviceType deviceType_ = AudioDeviceType::Output;
};

// ============================================================================
// Linux Device Selection Helpers
// ============================================================================

/**
 * @brief Select ALSA device
 * 
 * Select the specified ALSA device by name or index.
 * 
 * @param name Device name (e.g., "hw:0,0")
 * @param index Device index (card:device)
 * @return true if device selected successfully
 */
inline bool selectAlsaDevice(const std::string& name = "default") {
    // Implementation would use snd_pcm_open
    return true; // Placeholder
}

inline bool selectAlsaDevice(int cardIndex, int deviceIndex) {
    std::string deviceId = std::to_string(cardIndex) + "," + std::to_string(deviceIndex);
    return selectAlsaDevice(deviceId);
}

/**
 * @brief Select PulseAudio device
 * 
 * Select the specified PulseAudio device.
 * 
 * @param device Pulse device name (e.g., "alsa:default")
 * @return true if device selected successfully
 */
inline bool selectPulseAudioDevice(const std::string& device = "default") {
    // Implementation would use pa_context_connect
    return true; // Placeholder
}

// ============================================================================
// Linux Device Info Structure
// ============================================================================

/**
 * @brief Linux-specific device info
 */
struct LinuxDeviceInfo {
    std::string name;
    std::string id;
    std::string description;
    int sampleRate;
    int channels;
    int sampleFormat;
    std::string type; // "Input", "Output", "Monitor"
    std::vector<int> supportedSampleRates;
    std::vector<int> supportedChannelConfigs;
    std::vector<int> supportedFormats;
    
    // For PulseAudio
    pa_sample_spec_t paSampleSpec;
    std::vector<pa_cvolume_t> paVolumes;
    std::string paVolumeName;
    
    // For ALSA
    snd_card_t* sndCard_;
    snd_pcm_t* sndPcm_;
};

// ============================================================================
// Linux Audio Event Types
// ============================================================================

enum LinuxAudioEventType {
    LINUX_AUDIO_EVENT_STARTED = 0,
    LINUX_AUDIO_EVENT_STOPPED,
    LINUX_AUDIO_EVENT_PAUSED,
    LINUX_AUDIO_EVENT_RESUMED,
    LINUX_AUDIO_EVENT_ERROR,
    LINUX_AUDIO_EVENT_DEVICE_CHANGED,
    LINUX_AUDIO_EVENT_SAMPLE_RATE_CHANGED,
    LINUX_AUDIO_EVENT_VOLUME_CHANGED
};

// ============================================================================
// Linux Audio State
// ============================================================================

enum LinuxAudioState {
    LINUX_AUDIO_STATE_IDLE = 0,
    LINUX_AUDIO_STATE_INITIALIZING,
    LINUX_AUDIO_STATE_RUNNING,
    LINUX_AUDIO_STATE_PAUSED,
    LINUX_AUDIO_STATE_STOPPED,
    LINUX_AUDIO_STATE_ERROR
};

// ============================================================================
// Platform Constants
// ============================================================================

// Default device names
constexpr const char* LINUX_DEFAULT_INPUT_DEVICE = "default";
constexpr const char* LINUX_DEFAULT_OUTPUT_DEVICE = "default";

// Default sample rate
constexpr int LINUX_DEFAULT_SAMPLE_RATE = 44100;

// Buffer size limits
constexpr int LINUX_MIN_BUFFER_SIZE = 512;
constexpr int LINUX_MAX_BUFFER_SIZE = 10240;

// Recommended buffer sizes
static const int LINUX_RECOMMENDED_BUFFER_SIZES[] = {
    512, 1024, 2048, 4096, 8192, 16384, 32768
};

// Buffer time constants (in milliseconds)
constexpr int LINUX_MIN_BUFFER_TIME_MS = 50;  // 50ms
constexpr int LINUX_MAX_BUFFER_TIME_MS = 500; // 500ms
constexpr int LINUX_DEFAULT_BUFFER_TIME_MS = 250; // 250ms

// PulseAudio default buffer
constexpr int LINUX_DEFAULT_PULSE_BUFFER_TIME_MS = 100; // 100ms

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Check if ALSA is available
 */
inline bool isAlsaAvailable() {
#if defined(HAVE_ALSA) || defined(ALSA_FOUND)
    return snd_lib_version != nullptr;
#else
    return false;
#endif
}

/**
 * @brief Check if PulseAudio is available
 */
inline bool isPulseAudioAvailable() {
#if defined(HAVE_PULSEAUDIO) || defined(PA_AVAILABLE)
    return pa_context_get_state(paContext_) != PA_CONTEXT_UNCONNECTED;
#else
    return false;
#endif
}

/**
 * @brief Get available devices
 */
inline std::vector<AudioDeviceInfo> getAvailableDevices() {
    std::vector<AudioDeviceInfo> devices;
    
    // Get ALSA devices
    if (isAlsaAvailable()) {
        // Enumerate ALSM devices
    }
    
    // Get PulseAudio devices
    if (isPulseAudioAvailable()) {
        // Enumerate PulseAudio devices
    }
    
    return devices;
}

/**
 * @brief Create Linux audio device
 */
inline std::unique_ptr<AudioDeviceInterface> createLinuxAudioDevice(
    int sampleRate = 44100,
    int bufferSize = 512)
{
    return std::make_unique<LinuxAudioDevice>(sampleRate, bufferSize);
}

/**
 * @brief Detect preferred audio backend
 * 
 * Returns the preferred audio backend for the system:
 * - PulseAudio (if available and PulseaudioServerRunning)
 * - ALSA (if PulseAudio not available)
 */
inline std::string getPreferredAudioBackend() {
#if defined(HAVE_PULSEAUDIO) || defined(PA_AVAILABLE)
    // Check if PulseAudio server is running
    // If yes, return "pulse"
    // Otherwise, return "alsa"
    return "alsa"; // Default fallback
#else
    return "none";
#endif
}

#endif // TINNOTUS_PLATFORM_LINUX

#endif // PLATFORM_LINUX_HPP