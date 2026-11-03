/**
 * @file platform_macos.hpp
 * @brief macOS platform-specific header for Tinnotus
 */

#ifndef TINNOTUS_PLATFORM_MACOS_HPP
#define TINTINNOTUS_PLATFORM_MACOS_HPP

/**
 * @brief Tinnotus platform
 */
#define TINNOTUS_PLATFORM_MACOS

/**
 * @brief Include CoreAudio headers for audio I/O
 */
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/AudioDeviceAPI.h>
#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/AUCapabilities.h>
#include <CoreAudio/ComponentBase.h>
#include <AudioToolbox/AudioToolbox.h>

/**
 * @brief Audio device implementation class
 */
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

/**
 * @brief Create a default audio device for macOS
 */
inline std::unique_ptr<AudioDeviceManager> createDeviceManager() {
    return std::make_unique<MacOsAudioDevice>();
}

#endif // TINNOTUS_PLATFORM_MACOS