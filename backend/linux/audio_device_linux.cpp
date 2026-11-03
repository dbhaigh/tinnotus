/**
 * @file audio_device_linux.cpp
 * @brief Linux audio device implementation using ALSA/PulseAudio
 * 
 * This implementation uses ALSA (Advanced Linux Sound Architecture) for audio I/O.
 * PulseAudio can be used as a backend if ALSA is not available or for system-wide access.
 * 
 * Features:
 * - ALSA for direct hardware access (lower latency)
 * - PulseAudio fallback for system-wide audio server
 * - Support for 1-8 channels
 * - Sample rates: 44100, 48000, 96000 Hz
 * - Buffer sizes: 512, 1024, 2048, 4096, 8192 frames
 */

#include "platform_linux.hpp"
#include "platform/platform.hpp"
#include <alsa/asoundlib.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <memory>
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>

// Include DSP header for actual audio processing
#include "tinnotus.hpp"

namespace tinnotus {
namespace audio {

// ============================================================================
// LinuxAudioDevice Implementation
// ============================================================================

LinuxAudioDevice::LinuxAudioDevice(
    int sampleRate, 
    int bufferSize,
    const std::string& device,
    const std::string& deviceName) 
    : sampleRate_(sampleRate),
      bufferSize_(bufferSize),
      deviceType_(AudioDeviceType::Output),
      alsaInitialized_(false),
      pulseAudioInitialized_(false),
      audioThreadRunning_(false),
      audioThreadStart_(false)
{
    if (deviceName.empty()) {
        device_ = device;
    }
    
    // Validate sample rate
    if (sampleRate < 44100 || sampleRate > 96000) {
        sampleRate_ = 44100;
    }
    
    // Validate buffer size
    if (bufferSize_ < 512) {
        bufferSize_ = 512;
    }
    if (bufferSize_ > 10240) {
        bufferSize_ = 10240;
    }
    
    lastError_ = "";
    
    // Initialize the appropriate backend
    initialize();
}

LinuxAudioDevice::~LinuxAudioDevice() {
    shutdown();
}

AudioDeviceType LinuxAudioDevice::getType() const {
    if (alsaInitialized_ && deviceName.empty()) {
        return AudioDeviceType::Output;
    }
    return AudioDeviceType::Input;
}

bool LinuxAudioDevice::initialize() {
    lastError_ = "Initializing...";
    
    // Check which backend to use
#if defined(TINNOTUS_PULSEAUDIO_AVAILABLE)
    // First, check if PulseAudio server is available
    pa_context* paContext;
    int rc = pa_context_connect(&paContext, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
    if (rc == 0) {
        // PulseAudio is available and running
        lastError_ = "PulseAudio detected, using as default";
        pulseAudioInitialized_ = true;
        alsaInitialized_ = false;
        
        // Initialize PulseAudio
        return initializePulseAudio(paContext);
    } else if (rc == PA_ERR_NOSERVER) {
        // No PulseAudio server, fall back to ALSA
        lastError_ = "PulseAudio not available, falling back to ALSA";
        paContext = nullptr;
        pulseAudioInitialized_ = false;
    }
#endif
    
    // Always try ALSA
    lastError_ = "Initializing ALSA...";
    return initializeAlsa();
}

bool LinuxAudioDevice::initializeAlsa() {
    // Check if ALSA is available
    if (!snd_lib_version) {
        lastError_ = "ALSA library not found";
        return false;
    }
    
    // Get device ID
    std::string deviceId = device_;
    if (deviceId == "default") {
        deviceId = deviceName;
    }
    
    // Open PCM device
    snd_pcm_t* handle = nullptr;
    int err = snd_pcm_open(&handle, deviceId.c_str(), SND_PCM_STREAM_OUTPUT, 0);
    
    if (err < 0) {
        lastError_ = std::string("Cannot open ALSA device: ") + snd_strerror(err);
        return false;
    }
    
    // Configure ALSA device
    snd_pcm_hw_params_t* hwParams = nullptr;
    snd_pcm_hw_params_alloca(&hwParams);
    
    snd_pcm_hw_params_any(handle, hwParams);
    snd_pcm_hw_params_set_access(handle, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, hwParams, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_rate_near(handle, hwParams, &sampleRate_, 0);
    snd_pcm_hw_params_set_channels(handle, hwParams, 2); // Default stereo
    snd_pcm_hw_params_set_buffer_size(handle, hwParams, bufferSize_);
    snd_pcm_hw_params_set_period_time_hint(handle, hwParams, 250000); // 250ms
    snd_pcm_hw_params_set_start_threshold(handle, hwParams, 512);
    
    // Apply parameters
    err = snd_pcm_hw_params(handle, hwParams);
    if (err < 0) {
        lastError_ = std::string("Cannot apply ALSA parameters: ") + snd_strerror(err);
        snd_pcm_close(handle);
        handle = nullptr;
        return false;
    }
    
    // Prepare and start
    err = snd_pcm_prepare(handle);
    if (err < 0) {
        lastError_ = std::string("Cannot prepare ALSA device: ") + snd_strerror(err);
        snd_pcm_close(handle);
        handle = nullptr;
        return false;
    }
    
    // Start streaming
    err = snd_pcm_start(handle);
    if (err < 0) {
        lastError_ = std::string("Cannot start ALSA device: ") + snd_strerror(err);
        snd_pcm_close(handle);
        handle = nullptr;
        return false;
    }
    
    snd_pcm_ = handle;
    alsaInitialized_ = true;
    lastError_ = "";
    
    return true;
}

void LinuxAudioDevice::shutdown() {
    lastError_ = "Shutting down...";
    
    // Stop ALSA
    if (alsaInitialized_ && snd_pcm_) {
        snd_pcm_drop(snd_pcm_);
        snd_pcm_close(snd_pcm_);
        snd_pcm_ = nullptr;
        alsaInitialized_ = false;
    }
    
    // Stop PulseAudio
    if (pulseAudioInitialized_) {
        if (paStream_) {
            pa_stream_stop(paStream_);
            pa_stream_close(paStream_);
            paStream_ = nullptr;
        }
        if (paContext_) {
            pa_context_disconnect(paContext_);
            pa_context_unref(paContext_);
            paContext_ = nullptr;
        }
        pulseAudioInitialized_ = false;
    }
    
    // Stop audio thread
    audioThreadRunning_ = false;
    if (audioThread_.joinable()) {
        audioThread_.join();
    }
}

void LinuxAudioDevice::setProcessCallback(ProcessCallback callback) {
    processCallback_ = std::move(callback);
}

void LinuxAudioDevice::processAudio(
    const float* input, 
    float* output, 
    int numFrames) {
    
    if (!processCallback_) {
        // No callback set, just zero out output
        memset(output, 0, numFrames * sizeof(float));
        return;
    }
    
    // Call the DSP callback
    processCallback_(input, output, numFrames);
}

void LinuxAudioDevice::startAudioThread() {
    if (!alsaInitialized_ && !pulseAudioInitialized_) {
        lastError_ = "Cannot start: device not initialized";
        return;
    }
    
    audioThreadRunning_ = true;
    audioThreadStart_.store(true);
    
    audioThread_ = std::thread([this]() {
        this->audioThreadLoop();
    });
}

void LinuxAudioDevice::audioThreadLoop() {
    while (audioThreadRunning_ && audioThreadStart_.load()) {
        // Check if we need to process
        if (alsaInitialized_) {
            writeAlsaBuffer();
        } else if (pulseAudioInitialized_) {
            writePulseBuffer();
        }
        
        // Small sleep to prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // Clean up on exit
    shutdown();
}

void LinuxAudioDevice::writeAlsaBuffer() {
    // Get buffer size in bytes
    int framesPerPeriod = 1024;
    size_t bytesPerPeriod = framesPerPeriod * 2 * 2; // frames * channels * bytes_per_sample
    
    // Write data
    snd_pcm_sframes_t framesWritten = snd_pcm_writei(snd_pcm_, (const void*)nullptr, framesPerPeriod);
    
    if (framesWritten < 0) {
        switch (framesWritten) {
            case -EPIPE:
                lastError_ = "ALSA underrun";
                break;
            case -EDOM:
                lastError_ = "ALSA overflow";
                break;
            default:
                lastError_ = "ALSA write error";
                break;
        }
    }
}

void LinuxAudioDevice::writePulseBuffer() {
    // Allocate buffer
    size_t bufferLength = 4096 * sizeof(float);
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[bufferLength]);
    memset(buffer.get(), 0, bufferLength);
    
    // Write to PulseAudio
    pa_error_t err = pa_simple_write(paSimple_.get(), buffer.get(), bufferLength, nullptr);
    if (err < 0) {
        lastError_ = "PulseAudio write error";
    }
}

std::vector<AudioDeviceInfo> LinuxAudioDevice::getAvailableDevices() {
    std::vector<AudioDeviceInfo> devices;
    
    // Get ALSA devices
#if defined(TINNOTUS_ALSA_AVAILABLE)
    snd_pcm_info_t* info = nullptr;
    
    while (snd_pcm_info_any(nullptr, &info) == 0) {
        AudioDeviceInfo device;
        device.name = snd_pcm_info_get_name(info);
        device.sampleRate = snd_pcm_info_get_rate(info);
        device.channels = snd_pcm_info_get_channels(info);
        devices.push_back(device);
        
        snd_pcm_info_free(info);
    }
#endif
    
    // Get PulseAudio devices
#if defined(TINNOTUS_PULSEAUDIO_AVAILABLE)
    pa_info_t* paInfo = nullptr;
    int rc = pa_context_get_state(paContext_);
    
    if (rc == PA_CONTEXT_READY) {
        pa_context_set_state_callback(paContext_, [](pa_context* c, const pa_context_new_state_callback::Param1&, const pa_context_new_state_callback::Param2&) {
            // State changed
        }, nullptr);
    }
    
    while (pa_context_get_state(paContext_) == PA_CONTEXT_READY) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    pa_info_t* sinkInfo = pa_context_get_sink_info_list(paContext_, &paInfo);
    if (paInfo) {
        while (paInfo) {
            AudioDeviceInfo device;
            device.name = std::string(paInfo->name);
            device.sampleRate = paInfo->sampleSpec.rate;
            device.channels = paInfo->sampleSpec.channels;
            devices.push_back(device);
            
            paInfo = pa_context_get_sink_info_list(paContext_, &paInfo);
        }
    }
#endif
    
    return devices;
}

bool LinuxAudioDevice::checkError() const {
    return !lastError_.empty();
}

} // namespace audio
} // namespace tinnotus