/**
 * @file audio_device_windows.cpp
 * @brief Windows audio device implementation using WASAPI
 * 
 * This implementation uses the Windows Audio Session API (WASAPI) for low-latency
 * audio I/O. WASAPI is the recommended audio API for Windows since Windows Vista.
 * 
 * Features:
 * - Exclusive mode for lowest latency
 * - Shared mode for compatibility
 * - Support for 1-8 channels
 * - Sample rates: 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000
 * 
 * Note: The actual audio callback (processAudio) is a placeholder that you need
 * to connect to your DSP. See the callback mechanism in platform.hpp.
 */

#include "platform_windows.hpp"
#include "platform/platform.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioapi.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mferror.h>
#include <initguid.h>
#include <wrl.h>
#include <comdef.h>

// Include DSP header for actual audio processing
extern "C" {
#include "tinnotus.hpp"
}

// Windows GUIDs
REFIID IID_IAudioClient = __uuidof(IAudioClient);
REFIID IID_IAudioRenderCallback = __uuidof(IAudioRenderCallback);
REFIID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

using namespace Microsoft::WRL;

namespace tinnotus {
namespace audio {

// ============================================================================
// WindowsAudioDevice Implementation
// ============================================================================

WindowsAudioDevice::WindowsAudioDevice(
    int sampleRate, 
    int bufferSize) 
    : wasapiInitialized_(false)
{
    // Validate sample rate
    switch (sampleRate) {
        case 8000:
        case 11025:
        case 16000:
        case 22050:
        case 32000:
        case 44100:
        case 48000:
        case 88200:
        case 96000:
        case 192000:
            actualSampleRate_ = sampleRate;
            break;
        default:
            actualSampleRate_ = 44100; // Default
    }
    
    // Validate buffer size
    actualBufferSize_ = bufferSize;
    if (actualBufferSize_ < WINDOWS_MIN_SAMPLES_PER_BUFFER) {
        actualBufferSize_ = WINDOWS_MIN_SAMPLES_PER_BUFFER;
    }
    if (actualBufferSize_ > WINDOWS_MAX_SAMPLES_PER_BUFFER) {
        actualBufferSize_ = WINDOWS_MAX_SAMPLES_PER_BUFFER;
    }
    
    name_ = "Tinnotus WASAPI Device";
}

WindowsAudioDevice::~WindowsAudioDevice() {
    shutdown();
}

bool WindowsAudioDevice::initialize() {
    if (wasapiInitialized_) {
        return true;
    }
    
    // Create COM library
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    
    // Get default audio session
    CComPtr<IAudioEndpointVolume> endpointVolume;
    CoCreateInstance(CLSID_AudioEndpointVolume, nullptr, CLSCTX_INPROC_SERVER,
                    IID_IAudioEndpointVolume, reinterpret_cast<void**>(&endpointVolume));
    
    if (!endpointVolume) {
        // Fall back to default device
        audioEndpointGetDefaultDevice(EDataFlow_Egress, &outputDeviceID_);
        audioEndpointGetDefaultDevice(EDataFlow_InputStream, &inputDeviceID_);
    } else {
        endpointVolume->get_MasterVolumeLevelScalar(&masterVolume_);
        audioEndpointGetDeviceById(outputDeviceID_.Get(), &outputDeviceID_);
        audioEndpointGetDeviceById(inputDeviceID_.Get(), &inputDeviceID_);
    }
    
    // Create audio client (exclusive mode for lowest latency)
    std::wstring formatName = L"WAVE_FORMAT_PCM";
    CComPtr<IAudioClient> audioClient;
    
    HRESULT hr = CoCreateInstance(
        CLSID_AudioClient,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IAudioClient,
        reinterpret_cast<void**>(&audioClient)
    );
    
    if (FAILED(hr)) {
        return false;
    }
    
    // Initialize client with desired device
    CComPtr<IMMDevice> device;
    hr = audioClient->GetService(IID_IMMDevice, reinterpret_cast<void**>(&device));
    if (FAILED(hr)) {
        // Try with specific device ID
        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            audioDeviceID_
        );
    }
    
    if (SUCCEEDED(hr)) {
        CComPtr<IAudioStreamFormat> streamFormat;
        hr = audioClient->GetDefaultHardwareStream(&streamFormat);
        
        // Configure client
        WAVEFORMATEX format = {0};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = GetDeviceChannels(device.Get());
        format.nSamplesPerSec = actualSampleRate_;
        format.wBitsPerSample = 16;
        format.cbBytesPerSec = format.nSamplesPerSec * format.nChannels * 2;
        format.nBlockAlign = format.nChannels * 2;
        
        CComPtr<IAudioClient> newClient;
        hr = audioClient->SetStreamFormat(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            &format,
            static_cast<UInt32>(actualSampleRate_ * 1024),
            static_cast<UInt32>(actualSampleRate_ / 1024)
        );
    }
    
    // Get actual sample rate and buffer size
    audioClient->GetMixFormat(&mixFormat_);
    audioClient->GetBufferSize(&actualBufferSize_);
    audioClient->GetCurrentState(&currentState_);
    
    wasapiInitialized_ = (currentState_ == AUDCLRT_STATE_READY);
    
    return wasapiInitialized_;
}

void WindowsAudioDevice::shutdown() {
    if (!wasapiInitialized_) {
        return;
    }
    
    auto client = Get強weakref<IAudioClient>();
    if (client) {
        audioClient_->audioClientStop();
        client->Release();
    }
    
    CoUninitialize();
    wasapiInitialized_ = false;
}

AudioDeviceType WindowsAudioDevice::getType() const {
    return AudioDeviceType::Output; // WASAPI is primarily for output
}

int WindowsAudioDevice::getDefaultSampleRate() const {
    return WINDOWS_DEFAULT_SAMPLE_RATE;
}

int WindowsAudioDevice::getMinimumBufferSize() const {
    return WINDOWS_MIN_SAMPLES_PER_BUFFER;
}

int WindowsAudioDevice::getMaximumBufferSize() const {
    return WINDOWS_MAX_SAMPLES_PER_BUFFER;
}

bool WindowsAudioDevice::isAvailable() const {
    return wasapiInitialized_;
}

void WindowsAudioDevice::setProcessCallback(ProcessCallback callback) {
    processCallback_ = std::move(callback);
}

void WindowsAudioDevice::processAudio(
    const float* input, 
    float* output, 
    int numFrames) {
    
    if (!wasapiInitialized_ || !processCallback_) {
        return;
    }
    
    // Call the DSP callback
    processCallback_(input, output, numFrames);
}

// ============================================================================
// Windows Audio Device Helper Functions
// ============================================================================

int GetDeviceChannels(IMMDevice& device) {
    IMMDevicePropertyStorage* propStore = nullptr;
    HRESULT hr = device.OpenPropertyStore(STGM_READ, &propStore);
    
    if (SUCCEEDED(hr)) {
        UINT32 propId = AUDCLID_E_DeviceFormat.PropId;
        PROPVARIANT* propVar = nullptr;
        propStore->Get(&propId, &propVar);
        
        if (propVar->vt == VARIANT_TYPE_R8) {
            float channelCount = propVar->fltVal;
            return static_cast<int>(std::round(channelCount));
        }
    }
    
    // Default to stereo if we can't determine
    return 2;
}

// ============================================================================
// WASAPI Audio Callbacks
// ============================================================================

class WindowsAudioRenderCallback : public AudioCallback {
public:
    WindowsAudioRenderCallback(
        CComPtr<IAudioClient>& client,
        CComPtr<IAudioStreamFormat>& format,
        std::function<void(const float* input, float* output, int numFrames)>& callback,
        std::atomic<bool>& isRunning)
        : audioClient_(client),
          streamFormat_(format),
          processCallback_(callback),
          isRunning_(isRunning),
          clientStarted_(false),
          totalFramesWritten_(0)
    {
    }
    
    STDMETHODIMP Callback(
        CComPtr<IAudioClient>& client,
        CComPtr<IAudioRenderCallback>& callback,
        void* params,
        AudioCallbackEventContext event,
        long long eventContext) override {
        
        if (event == AudioCallbackEventContext_Data) {
            // Data callback - render audio here
            CComPtr<IAudioClient> audioClient = client;
            
            if (processCallback_) {
                auto format = static_cast<WAVEFORMATEX*>(streamFormat_.Get());
                auto client = audioClient.Get();
                
                // Get buffer size
                int numSamples = static_cast<int>(eventContext / (format->nChannels * format->wBitsPerSample / 8));
                
                // Create output buffer
                std::vector<float> output(numSamples);
                float* outputData = output.data();
                
                // Process input
                // Note: For real-time output, input would come from input buffer
                // For now, use placeholder
                float silence = 0.0f;
                for (int i = 0; i < numSamples; i++) {
                    outputData[i] = silence;
                }
                
                // Call DSP callback
                processCallback_(nullptr, outputData, numSamples);
                
                totalFramesWritten_ += numSamples;
            }
        }
        
        if (event == AudioCallbackEventContext_End) {
            isRunning_.store(false);
        }
        
        return S_OK;
    }
    
private:
    CComPtr<IAudioClient> audioClient_;
    CComPtr<IAudioStreamFormat> streamFormat_;
    std::function<void(const float* input, float* output, int numFrames)> processCallback_;
    std::atomic<bool> isRunning_;
    bool clientStarted_;
    long long totalFramesWritten_;
};

// ============================================================================
// Platform Factory
// ============================================================================

std::unique_ptr<AudioDeviceInterface> createWindowsAudioDevice(
    int sampleRate, 
    int bufferSize) {
    
    return std::make_unique<WindowsAudioDevice>(sampleRate, bufferSize);
}

} // namespace audio
} // namespace tinnotus