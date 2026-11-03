/**
 * @file platform_windows.hpp
 * @brief Windows platform definitions for Tinnotus
 * 
 * This header provides Windows-specific definitions needed by the
 * audio backend implementation.
 */

#ifndef PLATFORM_WINDOWS_HPP
#define PLATFORM_WINDOWS_HPP

// ============================================================================
// Platform Detection
// ============================================================================

// Windows platform detection
#if defined(_WIN32) || defined(_WIN64)
    #ifndef TINNOTUS_PLATFORM_WINDOWS
        #error "This header requires Windows platform to be defined"
    #endif
#endif

// ============================================================================
// Windows Headers
// ============================================================================

// Windows SDK headers for audio
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioapi.h>
#include <mmsystem.h>

// Windows audio formats
#include <mfapi.h>
#include <mferror.h>
#include <initguid.h>
#include <windowscodecs.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>

// COM library
#include <comdef.h>

// ============================================================================
// Windows Audio Constants
// ============================================================================

// Supported sample rates
enum WindowsSampleRate {
    WIN_SAMPLE_RATE_8000   = 8000,
    WIN_SAMPLE_RATE_11025  = 11025,
    WIN_SAMPLE_RATE_16000  = 16000,
    WIN_SAMPLE_RATE_22050  = 22050,
    WIN_SAMPLE_RATE_32000  = 32000,
    WIN_SAMPLE_RATE_44100  = 44100,
    WIN_SAMPLE_RATE_48000  = 48000,
    WIN_SAMPLE_RATE_88200  = 88200,
    WIN_SAMPLE_RATE_96000  = 96000,
    WIN_SAMPLE_RATE_192000 = 192000
};

// Supported audio formats
enum WindowsAudioFormat {
    WIN_AUDIO_FORMAT_PCM    = 1,
    WIN_AUDIO_FORMAT_MPEGL2 = 3,
    WIN_AUDIO_FORMAT_WMA    = 6,
    WIN_AUDIO_FORMAT_WMA_PRO = 11,
    WIN_AUDIO_FORMAT_WMAV1 = 12,
    WIN_AUDIO_FORMAT_WMAV2 = 13,
    WIN_AUDIO_FORMAT_WMAPRO = 15,
    WIN_AUDIO_FORMAT_WMASECONDARY = 17,
    WIN_AUDIO_FORMAT_WMAPRO_3 = 18,
    WIN_AUDIO_FORMAT_WMAPRO_2 = 19
};

// ============================================================================
// Windows Audio Device Types
// ============================================================================

/**
 * @brief Audio endpoint data flow
 */
enum AudioEndpointDataFlow {
    EDataFlow_NoDataFlow = 0,
    EDataFlow_Earcon,
    EDataFlow_Consumer,
    EDataFlow_Host,
    EDataFlow_Remote,
    EDataFlow_Monitor,
    EDataFlow_MonitorOut,
    EDataFlow_Protocol,
    EDataFlow_Renderer,
    EDataFlow_SharedProtected,
    EDataFlow_Alias,
    EDataFlow_Meeting,
    EDataFlow_Driver,
    EDataFlow_Input,
    EDataFlow_Output,
    EDataFlow_InputAndOutput,
    EDataFlow_0 = EDataFlow_NoDataFlow,
    EDataFlow_1 = EDataFlow_Earcon,
    EDataFlow_2 = EDataFlow_Consumer,
    EDataFlow_3 = EDataFlow_Host,
    EDataFlow_4 = EDataFlow_Remote,
    EDataFlow_5 = EDataFlow_Monitor,
    EDataFlow_6 = EDataFlow_MonitorOut,
    EDataFlow_7 = EDataFlow_Protocol,
    EDataFlow_8 = EDataFlow_Renderer,
    EDataFlow_9 = EDataFlow_SharedProtected,
    EDataFlow_10 = EDataFlow_Alias,
    EDataFlow_11 = EDataFlow_Meeting,
    EDataFlow_12 = EDataFlow_Driver,
    EDataFlow_13 = EDataFlow_Input,
    EDataFlow_14 = EDataFlow_Output,
    EDataFlow_15 = EDataFlow_InputAndOutput
};

/**
 * @brief Audio endpoint state
 */
enum AudioEndpointState {
    EDataFlow_Active = 0x0000,
    EDataFlow_Disconnected = 0x0001,
    EDataFlow_InUse = 0x0002,
    EDataFlow_Inactive = 0x0003,
    EDataFlow_Specialized = 0x0004,
    EDataFlow_NonPlugAndPlay = 0x0005
};

// ============================================================================
// COM Interface Definitions
// ============================================================================

/**
 * @brief IMMDevice interface
 * 
 * Device interface for endpoint devices.
 */
struct IMMDevice {
    IUnknown* punk;
    
    IMMDevice(IUnknown* p) : punk(p) {}
    
    // QueryInterface
    IUnknown* QueryInterface(const GUID& iid) {
        if (iid == IID_IUnknown || iid == IID_IMMDevice) {
            return this;
        }
        return nullptr;
    }
    
    // Release
    unsigned long Release() {
        return --punk->_refcount;
    }
    
    // Get DeviceId
    BSTR GetDeviceId() {
        // Implementation would be here
        return nullptr;
    }
    
    // Activate interface
    template<typename T>
    T* Activate(const GUID& iid) {
        T* ptr = new T();
        ptr->_refcount = 1;
        return ptr;
    }
};

/**
 * @brief IAudioClient interface
 * 
 * Audio client interface for WASAPI.
 */
struct IAudioClient {
    IUnknown* punk;
    unsigned long lockRefcount_;
    
    IAudioClient() : punk(nullptr), lockRefcount_(1) {}
    IAudioClient(IUnknown* p) : punk(p), lockRefcount_(1) {}
    
    // QueryInterface
    IUnknown* QueryInterface(const GUID& iid) {
        if (iid == IID_IUnknown) {
            auto* ptr = new IAudioClient(punk);
            return ptr;
        }
        return punk->QueryInterface(iid);
    }
    
    // AddRef
    unsigned long AddRef() {
        return ++lockRefcount_;
    }
    
    // Initialize with device
    HRESULT Initialize(
        DWORD sharemode,
        IMMDevice* device = nullptr
    ) {
        // Implementation for exclusive/shared mode
        return S_OK;
    }
    
    // GetMixFormat
    HRESULT GetMixFormat(WAVEFORMATEX** format) {
        // Return desired format
        return S_OK;
    }
    
    // GetCurrentState
    DWORD GetCurrentState() {
        return AUDCLRT_STATE_ACTIVE;
    }
    
    // GetService
    template<typename T>
    HRESULT GetService(const GUID& iid, void** service) {
        // Implementation for getting service
        return S_OK;
    }
    
    // Stop
    HRESULT AudioClientStop() {
        return S_OK;
    }
    
    // SetStreamFormat
    HRESULT SetStreamFormat(
        DWORD sharemode,
        const WAVEFORMATEX* format,
        DWORD periodInFrames,
        DWORD defaultBufferPeriodInFrames
    ) {
        // Implementation for setting stream format
        return S_OK;
    }
};

/**
 * @brief IAudioRenderCallback interface
 * 
 * Callback interface for audio rendering.
 */
struct IAudioRenderCallback : public IUnknown {
    IAudioRenderCallback() : IUnknown(), lockRefcount_(1) {}
    IAudioRenderCallback(IUnknown* p) : IUnknown(p), lockRefcount_(1) {}
    
    IAudioRenderCallback* QueryInterface(const GUID& iid) {
        if (iid == IID_IUnknown || iid == IID_IAudioRenderCallback) {
            auto* ptr = new IAudioRenderCallback(this);
            return ptr;
        }
        return nullptr;
    }
    
    unsigned long AddRef() {
        return ++lockRefcount_;
    }
    
    unsigned long Release() {
        return --lockRefcount_;
    }
    
    // Callback function
    STDMETHODIMP OnRender(long long reserved);
    
protected:
    IUnknown* punk;
    unsigned long lockRefcount_;
};

/**
 * @brief IAudioCaptureClient interface
 * 
 * Callback interface for audio capture.
 */
struct IAudioCaptureClient : public IUnknown {
    IAudioCaptureClient() : IUnknown(), lockRefcount_(1) {}
    IAudioCaptureClient(IUnknown* p) : IUnknown(p), lockRefcount_(1) {}
    
    IAudioCaptureClient* QueryInterface(const GUID& iid) {
        if (iid == IID_IUnknown || iid == IID_IAudioCaptureClient) {
            auto* ptr = new IAudioCaptureClient(this);
            return ptr;
        }
        return nullptr;
    }
    
    unsigned long AddRef() {
        return ++lockRefcount_;
    }
    
    unsigned long Release() {
        return --lockRefcount_;
    }
    
    STDMETHODIMP GetCurrentState(DWORD64* state) {
        return S_OK;
    }
    
    STDMETHODIMP GetStreamInformation(DWORD64* streamInfo) {
        return S_OK;
    }
    
protected:
    IUnknown* punk;
    unsigned long lockRefcount_;
};

// ============================================================================
// Windows Audio Device Interface
// ============================================================================

/**
 * @brief Windows audio device using WASAPI
 * 
 * Implements the AudioDeviceManager base class with Windows-specific
 * WASAPI implementation for audio I/O.
 */
struct WindowsAudioDevice {
    // Constructor
    WindowsAudioDevice(
        int sampleRate = 44100,
        int bufferSize = 256,
        const std::string& deviceId = ""
    );
    
    // Destructor
    ~WindowsAudioDevice();
    
    // Initialize device
    bool initialize() {
        // Initialize WASAPI
        // Return success/failure
        return false; // Placeholder
    }
    
    // Shutdown device
    void shutdown() {
        // Clean up WASAPI resources
    }
    
    // Device info
    std::string getName() const { return "Windows WASAPI Device"; }
    
    AudioEndpointDataFlow getDataFlow() const {
        return EDataFlow_14; // EDataFlow_Output
    }
    
    AudioEndpointState getState() const {
        return EDataFlow_Active; // Placeholder
    }
    
    int getDefaultSampleRate() const {
        return 44100;
    }
    
    int getMinimumBufferSize() const {
        return 256; // Minimum for WASAPI
    }
    
    int getMaximumBufferSize() const {
        return 10240; // Maximum for WASAPI
    }
    
    bool isRunning() const {
        return false; // Placeholder
    }
    
    // Start/stop
    void start() {
        // Start WASAPI stream
    }
    
    void stop() {
        // Stop WASAPI stream
    }
    
    // Set buffer size
    bool setBufferSize(int frames) {
        if (frames < getMinimumBufferSize() || frames > getMaximumBufferSize()) {
            return false;
        }
        return true; // Will validate on initialization
    }
    
    // Get current buffer size
    int getBufferSize() const {
        return bufferSize_;
    }
    
    // Check for errors
    bool hasErrors() const {
        return false; // Placeholder
    }
    
    // Clear errors
    void clearErrors() {}
    
    // Signal processing callback
    using ProcessCallback = std::function<void(const float* input, float* output, int numFrames)>;
    void setProcessCallback(ProcessCallback callback);
    
    void processAudio(const float* input, float* output, int numFrames);
    
protected:
    // WASAPI resources
    IMMDevice device_;
    IAudioClient* audioClient_ = nullptr;
    IAudioRenderCallback* renderCallback_ = nullptr;
    
    int sampleRate_ = 44100;
    int bufferSize_ = 256;
    float masterVolume_ = 1.0f;
    
    std::atomic<bool> isRunning_ = false;
    std::atomic<bool> isInitialized_ = false;
    
    // Audio event
    HANDLE audioEvent_ = nullptr;
    
    // Process callback
    ProcessCallback processCallback_;
};

// ============================================================================
// Platform Constants
// ============================================================================

// Default device names
constexpr const char* WINDOWS_DEFAULT_INPUT_DEVICE = "Default Audio Input";
constexpr const char* WINDOWS_DEFAULT_OUTPUT_DEVICE = "Default Audio Output";

// Default sample rate
constexpr int WINDOWS_DEFAULT_SAMPLE_RATE = 44100;

// Buffer size limits
constexpr int WINDOWS_MIN_BUFFER_SIZE = 256;
constexpr int WINDOWS_MAX_BUFFER_SIZE = 10240;

// Recommended buffer sizes
static const int WINDOWS_RECOMMENDED_BUFFER_SIZES[] = {
    256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Get default audio device
 */
inline bool getDefaultAudioDevice() {
    // Get Windows default audio device
    // Implementation would use EnumAudioSessions or IMMDeviceEnumerator
    return true; // Placeholder
}

/**
 * @brief Check if Windows audio is available
 */
inline bool isWindowsAudioAvailable() {
    // Check for Windows API availability
    return true; // Placeholder
}

/**
 * @brief Create Windows audio device
 */
inline std::unique_ptr<AudioDeviceInterface> createWindowsAudioDevice(
    int sampleRate = 44100,
    int bufferSize = 256)
{
    return std::make_unique<WindowsAudioDevice>(sampleRate, bufferSize);
}

#endif // TINNOTUS_PLATFORM_WINDOWS

#endif // PLATFORM_WINDOWS_HPP