#include "audio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tinnotus::audio {

SpscAudioBuffer::SpscAudioBuffer(std::size_t capacity_samples)
    : samples_(capacity_samples, 0.0f) {}

bool SpscAudioBuffer::tryWrite(const float* source, std::size_t count) noexcept {
    if (count == 0) {
        return true;
    }
    if (source == nullptr || samples_.empty()) {
        return false;
    }

    const std::size_t write_index = write_index_.load(std::memory_order_relaxed);
    const std::size_t read_index = read_index_.load(std::memory_order_acquire);
    const std::size_t used = write_index - read_index;
    if (used > samples_.size() || count > samples_.size() - used) {
        return false;
    }

    const std::size_t start = write_index % samples_.size();
    const std::size_t first_count = std::min(count, samples_.size() - start);
    std::copy_n(source, first_count, samples_.data() + start);
    std::copy_n(source + first_count, count - first_count, samples_.data());
    write_index_.store(write_index + count, std::memory_order_release);
    return true;
}

std::size_t SpscAudioBuffer::tryRead(
    float* destination,
    std::size_t max_count
) noexcept {
    if (max_count == 0 || destination == nullptr || samples_.empty()) {
        return 0;
    }

    const std::size_t read_index = read_index_.load(std::memory_order_relaxed);
    const std::size_t write_index = write_index_.load(std::memory_order_acquire);
    const std::size_t available_samples = write_index - read_index;
    if (available_samples > samples_.size()) {
        return 0;
    }

    const std::size_t count = std::min(max_count, available_samples);
    const std::size_t start = read_index % samples_.size();
    const std::size_t first_count = std::min(count, samples_.size() - start);
    std::copy_n(samples_.data() + start, first_count, destination);
    std::copy_n(samples_.data(), count - first_count, destination + first_count);
    read_index_.store(read_index + count, std::memory_order_release);
    return count;
}

std::size_t SpscAudioBuffer::available() const noexcept {
    const std::size_t write_index = write_index_.load(std::memory_order_acquire);
    const std::size_t read_index = read_index_.load(std::memory_order_acquire);
    const std::size_t count = write_index - read_index;
    return std::min(count, samples_.size());
}

AudioEngine::AudioEngine(int sample_rate, std::size_t channels) noexcept
    : sample_rate_(sample_rate),
      channels_(channels),
      valid_(sample_rate > 0 && channels > 0 && channels <= max_channels) {}

RequestResult AudioEngine::setNotch(
    std::size_t index,
    double center_frequency_hz,
    double bandwidth_hz
) noexcept {
    if (!valid_ || index >= max_notches) {
        return RequestResult::invalid_argument;
    }

    Command command;
    command.index = index;
    command.enabled = true;
    if (!calculateCoefficients(center_frequency_hz, bandwidth_hz, command.coefficients)) {
        return RequestResult::invalid_argument;
    }

    return enqueue(command) ? RequestResult::queued : RequestResult::queue_full;
}

RequestResult AudioEngine::removeNotch(std::size_t index) noexcept {
    if (!valid_ || index >= max_notches) {
        return RequestResult::invalid_argument;
    }

    Command command;
    command.index = index;
    command.enabled = false;
    return enqueue(command) ? RequestResult::queued : RequestResult::queue_full;
}

bool AudioEngine::process(
    const float* input,
    float* output,
    std::size_t frame_count
) noexcept {
    if (!valid_ ||
        frame_count > std::numeric_limits<std::size_t>::max() / channels_ ||
        (frame_count != 0 && (input == nullptr || output == nullptr))) {
        return false;
    }

    applyPendingCommands();

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            const std::size_t sample_index = frame * channels_ + channel;
            double sample = input[sample_index];

            for (std::size_t notch = 0; notch < max_notches; ++notch) {
                if (!enabled_[notch]) {
                    continue;
                }

                const Coefficients& coefficients = coefficients_[notch];
                FilterState& state = states_[notch][channel];
                const double filtered = coefficients.b0 * sample + state.z1;
                state.z1 = coefficients.b1 * sample - coefficients.a1 * filtered + state.z2;
                state.z2 = coefficients.b2 * sample - coefficients.a2 * filtered;
                sample = filtered;
            }

            output[sample_index] = static_cast<float>(sample);
        }
    }

    return true;
}

bool AudioEngine::calculateCoefficients(
    double center_frequency_hz,
    double bandwidth_hz,
    Coefficients& coefficients
) const noexcept {
    const double nyquist = static_cast<double>(sample_rate_) / 2.0;
    if (!std::isfinite(center_frequency_hz) ||
        !std::isfinite(bandwidth_hz) ||
        center_frequency_hz <= 0.0 ||
        center_frequency_hz >= nyquist ||
        bandwidth_hz <= 0.0 ||
        bandwidth_hz >= nyquist) {
        return false;
    }

    const double q = center_frequency_hz / bandwidth_hz;
    const double omega = 2.0 * std::acos(-1.0) * center_frequency_hz / sample_rate_;
    const double alpha = std::sin(omega) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    if (!std::isfinite(a0) || a0 <= 0.0) {
        return false;
    }

    const double cosine = std::cos(omega);
    coefficients.b0 = 1.0 / a0;
    coefficients.b1 = -2.0 * cosine / a0;
    coefficients.b2 = 1.0 / a0;
    coefficients.a1 = -2.0 * cosine / a0;
    coefficients.a2 = (1.0 - alpha) / a0;
    return true;
}

bool AudioEngine::enqueue(const Command& command) noexcept {
    const std::size_t write_index = command_write_index_.load(std::memory_order_relaxed);
    const std::size_t read_index = command_read_index_.load(std::memory_order_acquire);
    if (write_index - read_index >= command_queue_capacity) {
        return false;
    }

    commands_[write_index % command_queue_capacity] = command;
    command_write_index_.store(write_index + 1, std::memory_order_release);
    return true;
}

void AudioEngine::applyPendingCommands() noexcept {
    std::size_t read_index = command_read_index_.load(std::memory_order_relaxed);
    const std::size_t write_index = command_write_index_.load(std::memory_order_acquire);
    const std::size_t pending = std::min(
        write_index - read_index,
        command_queue_capacity
    );

    for (std::size_t i = 0; i < pending; ++i) {
        const Command& command = commands_[read_index % command_queue_capacity];
        coefficients_[command.index] = command.coefficients;
        enabled_[command.index] = command.enabled;
        states_[command.index].fill(FilterState{});
        ++read_index;
    }

    command_read_index_.store(read_index, std::memory_order_release);
}

} // namespace tinnotus::audio
