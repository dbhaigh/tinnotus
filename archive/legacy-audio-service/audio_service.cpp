// Archived legacy C++ service prototype; not part of active builds.
/**
 * @file audio_service.cpp
 * @brief Audio service implementation for Tinnotus
 * 
 * Implements cross-platform audio processing service with support for:
 * - Real-time and offline audio processing
 * - Device management (Windows WASAPI, macOS CoreAudio, Linux ALSA/PulseAudio)
 * - Pitch detection and notch filtering pipeline
 * - Audio playback and recording
 * 
 * @version 0.1.0
 */

#include "audio_service.h"
#include <thread>
#include <chrono>
#include <fstream>
#include <csignal>
#include <algorithm>

// Platform-specific implementations
#if defined(TINNOTUS_PLATFORM_MACOS)
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/AudioDeviceAPI.h>
#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/AUCapabilities.h>
#include <CoreAudio/ComponentBase.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <AudioToolbox/AudioToolbox.h>
#include <sys/stat.h>
#endif

#ifdef TINNOTUS_PLATFORM_WINDOWS
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <stdio.h>
#endif

#ifdef TINNOTUS_PLATFORM_LINUX
#include <alsa/asoundlib.h>
#endif

namespace tinnotus {
namespace audio {

// ============================================================================
// Implementation Details
// ============================================================================

// Signal handler for graceful shutdown
static std::atomic<bool> g_shutdown_requested{false};

void signalHandler(int signum) {
    g_shutdown_requested = true;
}

// ============================================================================
// Audio Buffer Implementation
// ============================================================================

AudioBuffer::AudioBuffer(int sampleRate, int maxFrames, int channels)
    : sampleRate_(sampleRate), maxFrames_(maxFrames), channels_(channels),
      data_(maxFrames * channels * sizeof(float)),
      writeIndex_(0), readIndex_(0), framesWritten_(0) {
}

void AudioBuffer::push(const std::vector<float>& samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t sampleCount = samples.size();
    float* data = data_.data();
    
    // Copy samples to buffer
    for (size_t i = 0; i < sampleCount && writeIndex_ + i < data_.size(); i++) {
        data[writeIndex_ + i] = samples[i];
    }
    writeIndex_ = (writeIndex_ + sampleCount) % data_.size();
    framesWritten_ += sampleCount;
    
    // Wrap around if buffer is full
    while (framesWritten_ >= (size_t)maxFrames_ * channels_) {
        // Remove oldest frames (from readIndex_)
        size_t framesToRemove = framesWritten_ - maxFrames_ * channels_;
        if (framesToRemove <= writeIndex_ - readIndex_) {
            readIndex_ += framesToRemove;
            framesWritten_ -= framesToRemove;
        } else {
            // Wrap around
            size_t removeThisPass = writeIndex_ - readIndex_;
            readIndex_ = 0;
            framesWritten_ -= removeThisPass;
        }
    }
}

size_t AudioBuffer::framesAvailable() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t available = framesWritten_ - (writeIndex_ - readIndex_);
    if (available < 0) {
        available += data_.size() / (sizeof(float) * channels_);
    }
    
    // Convert to frames (not samples)
    return available / channels_;
}

bool AudioBuffer::isFull() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return framesWritten_ >= maxFrames_;
}

void AudioBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    framesWritten_ = 0;
    readIndex_ = 0;
    writeIndex_ = 0;
}

bool AudioBuffer::setSampleRate(int newSampleRate) {
    // Would need resampling implementation here
    // For now, return false
    return false;
}

// ============================================================================
// Signal Processing Engine Implementation
// ============================================================================

SignalProcessingEngine::SignalProcessingEngine(int sampleRate)
    : sampleRate_(sampleRate) {
    
    // Initialize with some default filters
    // (User will add notch filters via API)
}

int SignalProcessingEngine::addNotchFilter(double frequency, double bandwidth) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate frequency range
    if (frequency < 100.0 || frequency > 8000.0) {
        return -1;
    }
    
    auto filter = std::make_unique<tinnotus::dsp::NotchFilter>(
        sampleRate_, frequency, bandwidth);
    notchFilters_.push_back(std::move(filter));
    
    return (int)notchFilters_.size() - 1;
}

bool SignalProcessingEngine::removeNotchFilter(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index < 0 || index >= (int)notchFilters_.size()) {
        return false;
    }
    
    notchFilters_[index].reset();
    notchFilters_.erase(notchFilters_.begin() + index);
    return true;
}

void SignalProcessingEngine::replaceNotchFilters(
    const std::vector<std::pair<double, double>>& filters) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    notchFilters_.clear();
    for (const auto& filter : filters) {
        notchFilters_.push_back(std::make_unique<tinnotus::dsp::NotchFilter>(
            sampleRate_, filter.first, filter.second));
    }
}

void SignalProcessingEngine::process(const float* input, float* output, int numFrames) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // First apply all notch filters
    for (auto& filter : notchFilters_) {
        filter->processBuffer(input, output, numFrames);
        input = output;
    }
    
    // Apply low-pass if configured
    for (auto& filter : lowPassFilters_) {
        filter->processBuffer(input, output, numFrames);
        input = output;
    }
    
    // Apply high-pass if configured
    for (auto& filter : highPassFilters_) {
        filter->processBuffer(input, output, numFrames);
        input = output;
    }
}

float SignalProcessingEngine::processSample(float sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    float output = sample;
    
    // Apply notch filters
    for (auto& filter : notchFilters_) {
        output = filter->processSample(output);
    }
    
    // Apply low-pass if configured
    for (auto& filter : lowPassFilters_) {
        output = filter->processSample(output);
    }
    
    // Apply high-pass if configured
    for (auto& filter : highPassFilters_) {
        output = filter->processSample(output);
    }
    
    return output;
}

std::unique_ptr<tinnotus::dsp::NotchFilter> 
SignalProcessingEngine::getNotchFilter(int index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index < 0 || index >= (int)notchFilters_.size()) {
        return nullptr;
    }
    
    return notchFilters_[index];
}

void SignalProcessingEngine::setNotchParameters(
    int index, double frequency, double bandwidth) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index < 0 || index >= (int)notchFilters_.size()) {
        return;
    }
    
    notchFilters_[index]->setFrequency(frequency);
    notchFilters_[index]->setBandwidth(bandwidth);
}

void SignalProcessingEngine::applyLowPass(int sampleRate, double cutoffFreq, double slope) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    tinnotus::dsp::LowPassFilter* lp = new tinnotus::dsp::LowPassFilter(
        sampleRate, cutoffFreq, slope);
    lowPassFilters_.push_back(std::unique_ptr<tinnotus::dsp::LowPassFilter>(lp));
}

void SignalProcessingEngine::applyHighPass(int sampleRate, double cutoffFreq, double slope) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    tinnotus::dsp::HighPassFilter* hp = new tinnotus::dsp::HighPassFilter(
        sampleRate, cutoffFreq, slope);
    highPassFilters_.push_back(std::unique_ptr<tinnotus::dsp::HighPassFilter>(hp));
}

// ============================================================================
// macOS CoreAudio Device Implementation
// ============================================================================

#ifdef TINNOTUS_PLATFORM_MACOS

// CoreAudio device class
class CoreAudioDevice : public AudioDeviceManager {
public:
    CoreAudioDevice(int sampleRate = 44100, int bufferSize = 256);
    ~CoreAudioDevice() override;
    
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
    bool initialized_;
    bool running_;
    int sampleRate_;
    int bufferSize_;
    AudioDeviceID inputDeviceId_;
    AudioDeviceID outputDeviceId_;
    
    void* inputClient_;
    void* outputClient_;
    
    AudioStreamBasicDescription inputFormat_;
    AudioStreamBasicDescription outputFormat_;
    
    AudioCallbackStruct inputCallback_;
    AudioCallbackStruct outputCallback_;
    
    std::vector<AudioDeviceDescription> deviceDescriptions_;
};

CoreAudioDevice::CoreAudioDevice(int sampleRate, int bufferSize)
    : sampleRate_(sampleRate), bufferSize_(bufferSize),
      initialized_(false), running_(false) {
}

CoreAudioDevice::~CoreAudioDevice() {
    shutdown();
}

bool CoreAudioDevice::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Initialize CoreAudio
    OSStatus status = AudioOpenDefaultDevices(
        kAudioDeviceEventStream,
        0,  // Use default stream
        nullptr,
        false,  // No default
        &inputDeviceId_,
        &outputDeviceId_
    );
    
    if (status != noErr) {
        lastError_ = "Failed to open default audio devices";
        return false;
    }
    
    // Get device descriptions
    UInt32 propertySize = 0;
    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal;
    AudioObjectPropertyScope deviceScope = kAudioObjectPropertyScopeOutput;
    
    OSStatus result = AudioObjectGetPropertyData(
        outputDeviceId_,
        &kAudioObjectPropertyComponent,
        0,
        nullptr,
        &propertySize,
        nullptr
    );
    
    if (result != noErr) {
        lastError_ = "Failed to get device properties";
        return false;
    }
    
    deviceDescriptions_.clear();
    for (UInt32 i = 0; i < propertySize / sizeof(AudioDeviceDescription); i++) {
        AudioDeviceDescription desc;
        AudioObjectGetPropertyData(
            outputDeviceId_,
            &kAudioObjectPropertyDeviceDescription,
            0,
            nullptr,
            &propertySize,
            &desc
        );
        deviceDescriptions_.push_back(desc);
    }
    
    // Configure stream format
    inputFormat_.mSampleRate = sampleRate_;
    outputFormat_.mSampleRate = sampleRate_;
    inputFormat_.mFormatID = kAudioFormatKAudioFormatLinearPCM;
    outputFormat_.mFormatID = kAudioFormatKAudioFormatLinearPCM;
    inputFormat_.mChannelsPerFrame = 2;
    outputFormat_.mChannelsPerFrame = 2;
    inputFormat_.mBitsPerChannel = 32;
    outputFormat_.mBitsPerChannel = 32;
    inputFormat_.mBytesPerPacket = 2 * 4;  // 2 channels * 4 bytes per sample
    outputFormat_.mBytesPerPacket = 2 * 4;
    inputFormat_.mBytesPerFrame = inputFormat_.mBytesPerPacket;
    outputFormat_.mBytesPerFrame = outputFormat_.mBytesPerPacket;
    inputFormat_.mFramesPerPacket = 1;
    outputFormat_.mFramesPerPacket = 1;
    
    inputFormat_.mFormatFlags = kAudioFormatFlagIsSignedInteger | 
                                kAudioFormatFlagIsPacked | 
                                kAudioFormatFlagIsBigEndian;
    outputFormat_.mFormatFlags = kAudioFormatFlagIsSignedInteger | 
                                kAudioFormatFlagIsPacked | 
                                kAudioFormatFlagIsBigEndian;
    
    // Create callback streams
    outputClient_ = AudioOutputGetCurrentNode(outputDeviceId_, &outputNodeID_);
    status = AudioCreateStreamWithNode(
        &outputClient_,
        outputNodeID_,
        &outputFormat_,
        sizeof(AudioStreamPacketDescription),
        &outputPacketDesc_,
        (void**)&outputClient_,
        audioCallback
    );
    
    inputClient_ = AudioInputGetCurrentNode(inputDeviceId_, &inputNodeID_);
    status = AudioCreateStreamWithNode(
        &inputClient_,
        inputNodeID_,
        &inputFormat_,
        sizeof(AudioStreamPacketDescription),
        &inputPacketDesc_,
        (void**)&inputClient_,
        audioCallback
    );
    
    if (status != noErr) {
        lastError_ = "Failed to create audio streams";
        return false;
    }
    
    initialized_ = true;
    return true;
}

void CoreAudioDevice::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    
    if (inputClient_) {
        AudioStopStream(inputClient_, false);
        AudioStopStream(inputClient_, true);
        inputClient_ = nullptr;
    }
    
    if (outputClient_) {
        AudioStopStream(outputClient_, false);
        AudioStopStream(outputClient_, true);
        outputClient_ = nullptr;
    }
    
    AudioObjectPropertyScope outputScope = kAudioObjectPropertyScopeGlobal;
    AudioObjectPropertyScope deviceScope = kAudioObjectPropertyScopeOutput;
    
    AudioObjectRelease(inputDeviceId_);
    AudioObjectRelease(outputDeviceId_);
    
    initialized_ = false;
    running_ = false;
}

AudioDeviceInfo CoreAudioDevice::getDefaultInputDevice() {
    // Get info from CoreAudio
    return AudioDeviceInfo();
}

AudioDeviceInfo CoreAudioDevice::getDefaultOutputDevice() {
    // Get info from CoreAudio
    return AudioDeviceInfo();
}

std::vector<AudioDeviceInfo> CoreAudioDevice::getAllDevices() {
    std::vector<AudioDeviceInfo> devices;
    
    UInt32 propertySize = 0;
    AudioObjectGetPropertyDataSize(
        kAudioObjectSystem,
        &kAudioObjectPropertySubDevice,
        0,
        nullptr,
        &propertySize
    );
    
    UInt32 numDevices = propertySize / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> deviceIds(numDevices);
    
    AudioObjectGetPropertyData(
        kAudioObjectSystem,
        &kAudioObjectPropertySubDevice,
        0,
        nullptr,
        &propertySize,
        deviceIds.data()
    );
    
    for (AudioDeviceID id : deviceIds) {
        AudioDeviceInfo info;
        devices.push_back(info);
    }
    
    return devices;
}

bool CoreAudioDevice::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    running_ = true;
    
    AudioStartStream(inputClient_, false);
    AudioStartStream(inputClient_, true);
    AudioStartStream(outputClient_, false);
    AudioStartStream(outputClient_, true);
    
    // Start processing thread
    inputProcessingThread_ = std::make_unique<std::thread>(&AudioService::inputProcessingLoop, this);
    outputProcessingThread_ = std::make_unique<std::thread>(&AudioService::outputProcessingLoop, this);
    
    return running_;
}

bool CoreAudioDevice::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    
    if (inputProcessingThread_) {
        inputProcessingThread_->join();
        inputProcessingThread_.reset();
    }
    
    if (outputProcessingThread_) {
        outputProcessingThread_->join();
        outputProcessingThread_.reset();
    }
    
    return running_ == false;
}

bool CoreAudioDevice::setBufferSize(int frames) {
    // Would need to configure stream with new buffer size
    bufferSize_ = frames;
    return true;
}

int CoreAudioDevice::getBufferSize() const {
    return bufferSize_;
}

bool CoreAudioDevice::hasErrors() const {
    return lastError_.empty() == false;
}

void CoreAudioDevice::clearErrors() {
    lastError_.clear();
}

#endif // TINNOTUS_PLATFORM_MACOS

// ============================================================================
// Audio Service Implementation
// ============================================================================

std::unique_ptr<AudioService> AudioService::create(
    int sampleRate, Audio::Mode mode) {
    
    auto service = std::make_unique<AudioService>();
    service->sampleRate_ = sampleRate;
    service->mode_ = mode;
    
    return service;
}

AudioService::~AudioService() {
    shutdown();
    if (device_) {
        device_->shutdown();
    }
}

bool AudioService::initialize() {
    state_ = State::Initializing;
    
    // Initialize device
    if (!device_) {
        switch (mode_) {
            case Mode::Playback:
                device_ = std::make_unique<MacOsAudioDevice>(sampleRate_);
                break;
            case Mode::Record:
                device_ = std::make_unique<MacOsAudioDevice>(sampleRate_, 512);
                break;
            case Mode::RecordPlayback:
                device_ = std::make_unique<MacOsAudioDevice>(sampleRate_, 512);
                break;
        }
    }
    
    if (!device_->initialize()) {
        log("Failed to initialize audio device: " + device_->lastError());
        return false;
    }
    
    // Create signal processing engine
    engine_ = std::make_unique<SignalProcessingEngine>(sampleRate_);
    
    // Create input/output buffers
    inputBuffer_ = std::make_unique<AudioBuffer>(sampleRate_, 4096, device_->getChannels());
    outputBuffer_ = std::make_unique<AudioBuffer>(sampleRate_, 4096, device_->getChannels());
    
    state_ = State::Idle;
    return true;
}

void AudioService::shutdown() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Stopped) {
        state_ = State::Stopped;
    }
}

void AudioService::setMode(Mode newMode) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ == State::Running) {
        stop();
    }
    
    mode_ = newMode;
    
    if (state_ == State::Stopped) {
        // Reinitialize with new mode
        if (sampleRate_ == 44100) {
            device_ = std::make_unique<MacOsAudioDevice>(sampleRate_, 512);
        } else {
            device_ = std::make_unique<MacOsAudioDevice>(sampleRate_);
        }
        initialize();
    }
}

bool AudioService::start() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Initialized) {
        log("Cannot start: not initialized");
        return false;
    }
    
    state_ = State::Running;
    
    // Start audio device
    device_->start();
    
    // Start processing threads
    processingThread_ = std::make_unique<std::thread>(&AudioService::processingLoop, this);
    
    state_ = State::Running;
    return true;
}

bool AudioService::pause() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Running) {
        return false;
    }
    
    state_ = State::Paused;
    
    // Stop input processing
    if (inputProcessingThread_) {
        inputProcessingThread_->join();
        inputProcessingThread_.reset();
    }
    inputProcessingThread_ = nullptr;
    
    return true;
}

bool AudioService::resume() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Paused) {
        return false;
    }
    
    state_ = State::Running;
    
    // Restart input processing
    inputProcessingThread_ = std::make_unique<std::thread>(&AudioService::inputProcessingLoop, this);
    
    return true;
}

bool AudioService::setNotchFilter(double frequency, double bandwidth) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    // Validate frequency range
    if (frequency < 100.0 || frequency > 8000.0) {
        log("Frequency out of range: " + std::to_string(frequency));
        return false;
    }
    
    auto filter = std::make_unique<tinnotus::dsp::NotchFilter>(
        sampleRate_, frequency, bandwidth);
    
    notchFilters_.push_back(std::move(filter));
    notchParams_.push_back({frequency, bandwidth});
    notchFrequencies_.push_back(frequency);
    
    // Update engine
    engine_->addNotchFilter(frequency, bandwidth);
    
    log("Notch filter added at " + std::to_string(frequency) + " Hz");
    return true;
}

bool AudioService::addNotchFilter(double frequency, double bandwidth) {
    // Delegate to setNotchFilter for now
    return setNotchFilter(frequency, bandwidth);
}

bool AudioService::removeNotchFilter(int index) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (index < 0 || index >= (int)notchFilters_.size()) {
        return false;
    }
    
    // Remove from engine
    int engineIndex = index;
    if (engineIndex >= (int)engine_->getNotchCount()) {
        engineIndex = (int)engine_->getNotchCount() - 1;
    }
    
    engine_->removeNotchFilter(engineIndex);
    
    // Remove from vector
    notchFilters_[index].reset();
    notchFilters_.erase(notchFilters_.begin() + index);
    notchParams_.erase(notchParams_.begin() + index);
    notchFrequencies_.erase(notchFrequencies_.begin() + index);
    
    return true;
}

std::vector<double> AudioService::getNotchFrequencies() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return notchFrequencies_;
}

double AudioService::detectPitch(const float* audioBuffer, int numSamples) {
    if (!pitchDetector_) {
        pitchDetector_ = std::make_unique<CombinedPitchDetector>(
            sampleRate_, {PitchDetectionAlgorithm::YIN});
    }
    
    return pitchDetector_->detectPitch(audioBuffer, numSamples);
}

void AudioService::setInput(const float* input, int numFrames) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (state_ != State::Running) {
        return;
    }
    
    inputBuffer_->push({input, numFrames * device_->getChannels()});
}

bool AudioService::getOutput(float* output, int maxFrames) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    auto available = inputBuffer_->framesAvailable();
    int framesToCopy = std::min(maxFrames, (int)available);
    
    if (framesToCopy == 0) {
        return false;
    }
    
    // Copy buffered output to provided buffer
    outputBuffer_->push({output, framesToCopy * device_->getChannels()});
    
    return true;
}

bool AudioService::loadFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    // Load WAV file
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        log("Failed to open file: " + filepath);
        return false;
    }
    
    // Check for WAV header
    char header[44];
    file.read(header, 44);
    if (header[0] != 'R' || header[36] != 'W' || header[25] != 'a' || header[28] != 'V') {
        log("Not a WAV file");
        return false;
    }
    
    // Extract file info
    Audio::FileInfo fileInfo;
    fileInfo.sampleRate = sampleRate_;  // Will need to parse from WAV
    
    log("Loaded file: " + filepath);
    return true;
}

bool AudioService::processFile() {
    // Placeholder - would need file loading implementation
    return false;
}

TinnitusResult AudioService::analyzeTinnitus() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    // Get audio buffer
    auto buffer = inputBuffer_->pull();
    
    // Detect pitch
    double freq = detectPitch(buffer.data(), buffer.size());
    
    if (freq == 0.0) {
        // Try guided test if no automatic detection
        guidedTest();
        freq = detectPitch(buffer.data(), buffer.size());
    }
    
    return {
        freq > 0.0,
        freq,
        0.5,  // Would calculate actual confidence
        0.0,  // Would calculate from amplitude
        "unknown"
    };
}

ServiceStatistics AudioService::getStatistics() const {
    ServiceStatistics stats;
    stats.framesProcessed = inputBuffer_->framesWritten();
    stats.framesOutput = outputBuffer_->size();
    
    return stats;
}

void AudioService::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(logsMutex_);
    logs_.push_back(std::move(message));
}

} // namespace audio
} // namespace tinnotus