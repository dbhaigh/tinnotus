/**
 * @file audio_device_macos.cpp
 * @brief macOS audio device implementation using CoreAudio
 */

#include "platform_macos.hpp"
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/AudioDeviceAPI.h>
#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/AUCapabilities.h>
#include <CoreAudio/ComponentBase.h>
#include <AudioToolbox/AudioToolbox.h>
#include <sys/stat.h>
#include <cmath>

namespace tinnotus {
namespace audio {
namespace {

// CoreAudio audio device class
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
    
private:
    void audioCallback(void* inUserData, AudioStreamPacketDescription** inOutDescribe,
                      const AudioBufferList* inOutData, bool inIsOutput);
    
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
    
    std::string lastError_;
    std::thread processingThread_;
};

CoreAudioDevice::CoreAudioDevice(int sampleRate, int bufferSize)
    : sampleRate_(sampleRate), bufferSize_(bufferSize),
      initialized_(false), running_(false) {
}

CoreAudioDevice::~CoreAudioDevice() {
    shutdown();
}

void CoreAudioDevice::audioCallback(void* inUserData, AudioStreamPacketDescription** inOutDescribe,
                                   const AudioBufferList* inOutData, bool inIsOutput) {
    (void)inUserData;  // TODO: Add audio processing context
    (void)inOutDescribe;
    
    // Placeholder callback
}

bool CoreAudioDevice::initialize() {
    // Initialize CoreAudio
    OSStatus status = AudioOpenDefaultDevices(
        kAudioDeviceEventStream,
        0,
        nullptr,
        false,
        &inputDeviceId_,
        &outputDeviceId_
    );
    
    if (status != noErr) {
        lastError_ = "Failed to open default audio devices";
        return false;
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
    inputFormat_.mBytesPerPacket = 2 * 4;
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
    
    return true;
}

void CoreAudioDevice::shutdown() {
    running_ = false;
    initialized_ = false;
}

AudioDeviceInfo CoreAudioDevice::getDefaultInputDevice() {
    AudioDeviceInfo info;
    info.sample_rate = sampleRate_;
    info.buffer_frames = bufferSize_;
    info.state = 0;
    return info;
}

AudioDeviceInfo CoreAudioDevice::getDefaultOutputDevice() {
    AudioDeviceInfo info;
    info.sample_rate = sampleRate_;
    info.buffer_frames = bufferSize_;
    info.state = 0;
    return info;
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
    running_ = true;
    
    AudioStartStream(inputClient_, false);
    AudioStartStream(inputClient_, true);
    AudioStartStream(outputClient_, false);
    AudioStartStream(outputClient_, true);
    
    return running_;
}

bool CoreAudioDevice::stop() {
    running_ = false;
    return running_ == false;
}

bool CoreAudioDevice::setSampleRate(int newSampleRate) {
    // Would need to reconfigure audio stream
    sampleRate_ = newSampleRate;
    return true;
}

bool CoreAudioDevice::setBufferSize(int newFrames) {
    // Would need to reconfigure audio stream
    bufferSize_ = newFrames;
    return true;
}

bool CoreAudioDevice::isRunning() const {
    return running_;
}

void CoreAudioDevice::setInputDevice(const AudioDeviceInfo& device) {
    // TODO: Implementation
}

void CoreAudioDevice::setOutputDevice(const AudioDeviceInfo& device) {
    // TODO: Implementation
}

bool CoreAudioDevice::setBufferSize(int frames) {
    bufferSize_ = frames;
    return true;
}

int CoreAudioDevice::getBufferSize() const {
    return bufferSize_;
}

bool CoreAudioDevice::hasErrors() const {
    return !lastError_.empty();
}

void CoreAudioDevice::clearErrors() {
    lastError_.clear();
}

} // anonymous namespace

// Device manager factory
std::unique_ptr<AudioDeviceManager> createDeviceManager() {
    return std::make_unique<CoreAudioDevice>();
}

// Create default audio service
std::unique_ptr<AudioService> createDefaultService(int sampleRate) {
    auto device = std::make_unique<CoreAudioDevice>(sampleRate);
    auto service = std::make_unique<AudioService>(std::move(device));
    
    if (sampleRate == 44100) {
        service->setSampleRate(44100);
    }
    
    return service;
}

} // namespace audio
} // namespace tinnotus