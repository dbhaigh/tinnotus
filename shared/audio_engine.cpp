#include "audio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(TINNOTUS_AUDIO_ENABLE_AVX2) || defined(_M_AVX2) || defined(__AVX2__)
#include <immintrin.h>
#define TINNOTUS_AUDIO_USE_AVX2
#elif defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || \
    defined(__SSE2__)
#include <emmintrin.h>
#define TINNOTUS_AUDIO_USE_SSE2
#elif defined(_M_ARM64) || defined(__aarch64__)
#include <arm_neon.h>
#define TINNOTUS_AUDIO_USE_NEON
#endif

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

#if defined(TINNOTUS_AUDIO_USE_AVX2) || \
    defined(TINNOTUS_AUDIO_USE_SSE2) || \
    defined(TINNOTUS_AUDIO_USE_NEON)
    if (channels_ < 6) {
#else
    {
#endif
        for (std::size_t frame = 0; frame < frame_count; ++frame) {
            for (std::size_t channel = 0; channel < channels_; ++channel) {
                const std::size_t sample_index = frame * channels_ + channel;
                double sample = input[sample_index];

                for (std::size_t notch = 0; notch < max_notches; ++notch) {
                    if (!enabled_[notch]) {
                        continue;
                    }

                    const Coefficients& coefficients = coefficients_[notch];
                    FilterState& state = scalar_states_[notch][channel];
                    const double filtered = coefficients.b0 * sample + state.z1;
                    state.z1 = coefficients.b1 * sample - coefficients.a1 * filtered +
                               state.z2;
                    state.z2 = coefficients.b2 * sample - coefficients.a2 * filtered;
                    sample = filtered;
                }

                output[sample_index] = static_cast<float>(sample);
            }
        }
        return true;
    }

#if defined(TINNOTUS_AUDIO_USE_AVX2) || \
    defined(TINNOTUS_AUDIO_USE_SSE2) || \
    defined(TINNOTUS_AUDIO_USE_NEON)
    std::array<double, max_channels> frame_samples;
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        const std::size_t frame_offset = frame * channels_;
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            frame_samples[channel] = input[frame_offset + channel];
        }

        for (std::size_t notch = 0; notch < max_notches; ++notch) {
            if (!enabled_[notch]) {
                continue;
            }

            const Coefficients& coefficients = coefficients_[notch];
            SimdFilterState& state = simd_states_[notch];
            std::size_t channel = 0;

#if defined(TINNOTUS_AUDIO_USE_AVX2)
            const __m256d b0_avx = _mm256_set1_pd(coefficients.b0);
            const __m256d b1_avx = _mm256_set1_pd(coefficients.b1);
            const __m256d b2_avx = _mm256_set1_pd(coefficients.b2);
            const __m256d a1_avx = _mm256_set1_pd(coefficients.a1);
            const __m256d a2_avx = _mm256_set1_pd(coefficients.a2);
            for (; channel + 4 <= channels_; channel += 4) {
                const __m256d sample = _mm256_loadu_pd(frame_samples.data() + channel);
                const __m256d z1 = _mm256_loadu_pd(state.z1.data() + channel);
                const __m256d z2 = _mm256_loadu_pd(state.z2.data() + channel);
                const __m256d filtered = _mm256_add_pd(
                    _mm256_mul_pd(b0_avx, sample),
                    z1
                );
                const __m256d next_z1 = _mm256_add_pd(
                    _mm256_sub_pd(
                        _mm256_mul_pd(b1_avx, sample),
                        _mm256_mul_pd(a1_avx, filtered)
                    ),
                    z2
                );
                const __m256d next_z2 = _mm256_sub_pd(
                    _mm256_mul_pd(b2_avx, sample),
                    _mm256_mul_pd(a2_avx, filtered)
                );
                _mm256_storeu_pd(frame_samples.data() + channel, filtered);
                _mm256_storeu_pd(state.z1.data() + channel, next_z1);
                _mm256_storeu_pd(state.z2.data() + channel, next_z2);
            }

            if (channel + 2 <= channels_) {
                const __m128d b0_sse = _mm_set1_pd(coefficients.b0);
                const __m128d b1_sse = _mm_set1_pd(coefficients.b1);
                const __m128d b2_sse = _mm_set1_pd(coefficients.b2);
                const __m128d a1_sse = _mm_set1_pd(coefficients.a1);
                const __m128d a2_sse = _mm_set1_pd(coefficients.a2);
                for (; channel + 2 <= channels_; channel += 2) {
                    const __m128d sample = _mm_loadu_pd(frame_samples.data() + channel);
                    const __m128d z1 = _mm_loadu_pd(state.z1.data() + channel);
                    const __m128d z2 = _mm_loadu_pd(state.z2.data() + channel);
                    const __m128d filtered = _mm_add_pd(_mm_mul_pd(b0_sse, sample), z1);
                    const __m128d next_z1 = _mm_add_pd(
                        _mm_sub_pd(
                            _mm_mul_pd(b1_sse, sample),
                            _mm_mul_pd(a1_sse, filtered)
                        ),
                        z2
                    );
                    const __m128d next_z2 = _mm_sub_pd(
                        _mm_mul_pd(b2_sse, sample),
                        _mm_mul_pd(a2_sse, filtered)
                    );
                    _mm_storeu_pd(frame_samples.data() + channel, filtered);
                    _mm_storeu_pd(state.z1.data() + channel, next_z1);
                    _mm_storeu_pd(state.z2.data() + channel, next_z2);
                }
            }
#elif defined(TINNOTUS_AUDIO_USE_SSE2)
            const __m128d b0 = _mm_set1_pd(coefficients.b0);
            const __m128d b1 = _mm_set1_pd(coefficients.b1);
            const __m128d b2 = _mm_set1_pd(coefficients.b2);
            const __m128d a1 = _mm_set1_pd(coefficients.a1);
            const __m128d a2 = _mm_set1_pd(coefficients.a2);
            for (; channel + 2 <= channels_; channel += 2) {
                const __m128d sample = _mm_loadu_pd(frame_samples.data() + channel);
                const __m128d z1 = _mm_loadu_pd(state.z1.data() + channel);
                const __m128d z2 = _mm_loadu_pd(state.z2.data() + channel);
                const __m128d filtered = _mm_add_pd(_mm_mul_pd(b0, sample), z1);
                const __m128d next_z1 = _mm_add_pd(
                    _mm_sub_pd(_mm_mul_pd(b1, sample), _mm_mul_pd(a1, filtered)),
                    z2
                );
                const __m128d next_z2 = _mm_sub_pd(
                    _mm_mul_pd(b2, sample),
                    _mm_mul_pd(a2, filtered)
                );
                _mm_storeu_pd(frame_samples.data() + channel, filtered);
                _mm_storeu_pd(state.z1.data() + channel, next_z1);
                _mm_storeu_pd(state.z2.data() + channel, next_z2);
            }
#elif defined(TINNOTUS_AUDIO_USE_NEON)
            const float64x2_t b0 = vdupq_n_f64(coefficients.b0);
            const float64x2_t b1 = vdupq_n_f64(coefficients.b1);
            const float64x2_t b2 = vdupq_n_f64(coefficients.b2);
            const float64x2_t a1 = vdupq_n_f64(coefficients.a1);
            const float64x2_t a2 = vdupq_n_f64(coefficients.a2);
            for (; channel + 2 <= channels_; channel += 2) {
                const float64x2_t sample = vld1q_f64(frame_samples.data() + channel);
                const float64x2_t z1 = vld1q_f64(state.z1.data() + channel);
                const float64x2_t z2 = vld1q_f64(state.z2.data() + channel);
                const float64x2_t filtered = vaddq_f64(vmulq_f64(b0, sample), z1);
                const float64x2_t next_z1 = vaddq_f64(
                    vsubq_f64(vmulq_f64(b1, sample), vmulq_f64(a1, filtered)),
                    z2
                );
                const float64x2_t next_z2 = vsubq_f64(
                    vmulq_f64(b2, sample),
                    vmulq_f64(a2, filtered)
                );
                vst1q_f64(frame_samples.data() + channel, filtered);
                vst1q_f64(state.z1.data() + channel, next_z1);
                vst1q_f64(state.z2.data() + channel, next_z2);
            }
#endif

            for (; channel < channels_; ++channel) {
                const double sample = frame_samples[channel];
                    const double filtered =
                        coefficients.b0 * sample + state.z1[channel];
                    state.z1[channel] =
                        coefficients.b1 * sample - coefficients.a1 * filtered +
                        state.z2[channel];
                    state.z2[channel] =
                        coefficients.b2 * sample - coefficients.a2 * filtered;
                    frame_samples[channel] = filtered;
                }
        }

        for (std::size_t channel = 0; channel < channels_; ++channel) {
            output[frame_offset + channel] = static_cast<float>(frame_samples[channel]);
        }
    }
    return true;
#endif
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
        scalar_states_[command.index].fill(FilterState{});
        simd_states_[command.index] = SimdFilterState{};
        ++read_index;
    }

    command_read_index_.store(read_index, std::memory_order_release);
}

} // namespace tinnotus::audio
