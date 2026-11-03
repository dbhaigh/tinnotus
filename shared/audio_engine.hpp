#ifndef TINNOTUS_AUDIO_ENGINE_HPP
#define TINNOTUS_AUDIO_ENGINE_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

namespace tinnotus::audio {

enum class RequestResult {
    queued,
    invalid_argument,
    queue_full,
};

class SpscAudioBuffer {
public:
    explicit SpscAudioBuffer(std::size_t capacity_samples);

    bool tryWrite(const float* samples, std::size_t count) noexcept;
    std::size_t tryRead(float* destination, std::size_t max_count) noexcept;
    std::size_t available() const noexcept;
    std::size_t capacity() const noexcept { return samples_.size(); }

private:
    std::vector<float> samples_;
    alignas(64) std::atomic<std::size_t> read_index_{0};
    alignas(64) std::atomic<std::size_t> write_index_{0};
};

class AudioEngine {
public:
    static constexpr std::size_t max_channels = 8;
    static constexpr std::size_t max_notches = 8;

    AudioEngine(int sample_rate, std::size_t channels) noexcept;

    bool isValid() const noexcept { return valid_; }
    int sampleRate() const noexcept { return sample_rate_; }
    std::size_t channels() const noexcept { return channels_; }

    RequestResult setNotch(
        std::size_t index,
        double center_frequency_hz,
        double bandwidth_hz
    ) noexcept;
    RequestResult removeNotch(std::size_t index) noexcept;

    // Call configuration methods from one control thread and process from one audio thread.
    bool process(const float* input, float* output, std::size_t frame_count) noexcept;

private:
    struct Coefficients {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
    };

    struct FilterState {
        double z1 = 0.0;
        double z2 = 0.0;
    };

    struct SimdFilterState {
        std::array<double, max_channels> z1{};
        std::array<double, max_channels> z2{};
    };

    struct Command {
        std::size_t index = 0;
        bool enabled = false;
        Coefficients coefficients{};
    };

    static constexpr std::size_t command_queue_capacity = 16;

    bool calculateCoefficients(
        double center_frequency_hz,
        double bandwidth_hz,
        Coefficients& coefficients
    ) const noexcept;
    bool enqueue(const Command& command) noexcept;
    void applyPendingCommands() noexcept;

    int sample_rate_;
    std::size_t channels_;
    bool valid_;
    std::array<Command, command_queue_capacity> commands_{};
    alignas(64) std::atomic<std::size_t> command_read_index_{0};
    alignas(64) std::atomic<std::size_t> command_write_index_{0};
    std::array<Coefficients, max_notches> coefficients_{};
    std::array<bool, max_notches> enabled_{};
    std::array<std::array<FilterState, max_channels>, max_notches> scalar_states_{};
    std::array<SimdFilterState, max_notches> simd_states_{};
};

} // namespace tinnotus::audio

#endif
