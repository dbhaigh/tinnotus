#include "pitch_detector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace tinnotus::audio {
namespace {

constexpr int min_sample_rate = 8000;
constexpr int max_sample_rate = 192000;
constexpr double min_frequency_hz = 50.0;
constexpr double max_frequency_hz = 2000.0;
constexpr double minimum_rms = 1.0e-6;
constexpr double yin_threshold = 0.15;
constexpr double minimum_confidence = 0.8;

} // namespace

bool PitchDetector::detect(
    const float* samples,
    std::size_t sample_count,
    int sample_rate,
    PitchEstimate& estimate
) noexcept {
    estimate = {};
    if (samples == nullptr ||
        sample_count < min_window_samples ||
        sample_count > max_window_samples ||
        sample_rate < min_sample_rate ||
        sample_rate > max_sample_rate) {
        return false;
    }

    double mean = 0.0;
    for (std::size_t i = 0; i < sample_count; ++i) {
        if (!std::isfinite(samples[i])) {
            return false;
        }
        mean += static_cast<double>(samples[i]) / static_cast<double>(sample_count);
    }

    double squared_sum = 0.0;
    for (std::size_t i = 0; i < sample_count; ++i) {
        const double centered = static_cast<double>(samples[i]) - mean;
        squared_sum += centered * centered;
    }
    const double rms = std::sqrt(squared_sum / static_cast<double>(sample_count));
    if (rms < minimum_rms) {
        return true;
    }

    const std::size_t min_lag = std::max<std::size_t>(
        2,
        static_cast<std::size_t>(static_cast<double>(sample_rate) / max_frequency_hz)
    );
    const std::size_t max_lag = std::min(
        sample_count / 2,
        static_cast<std::size_t>(
            static_cast<double>(sample_rate) / min_frequency_hz
        )
    );
    if (min_lag > max_lag) {
        return true;
    }

    std::array<double, max_window_samples + 1> difference{};
    for (std::size_t lag = 1; lag <= max_lag; ++lag) {
        double sum = 0.0;
        const std::size_t limit = sample_count - lag;
        for (std::size_t i = 0; i < limit; ++i) {
            const double delta = static_cast<double>(samples[i]) -
                                 static_cast<double>(samples[i + lag]);
            sum += delta * delta;
        }
        difference[lag] = sum;
    }

    double cumulative_difference = 0.0;
    for (std::size_t lag = 1; lag <= max_lag; ++lag) {
        cumulative_difference += difference[lag];
        difference[lag] = cumulative_difference > 0.0
            ? difference[lag] * static_cast<double>(lag) / cumulative_difference
            : 1.0;
    }

    std::size_t best_lag = 0;
    double best_difference = std::numeric_limits<double>::infinity();
    for (std::size_t lag = min_lag; lag <= max_lag; ++lag) {
        if (difference[lag] < best_difference) {
            best_difference = difference[lag];
            best_lag = lag;
        }
        if (difference[lag] < yin_threshold) {
            best_lag = lag;
            while (best_lag < max_lag &&
                   difference[best_lag + 1] < difference[best_lag]) {
                ++best_lag;
            }
            best_difference = difference[best_lag];
            break;
        }
    }

    if (best_lag == 0) {
        return true;
    }

    estimate.confidence = std::clamp(1.0 - best_difference, 0.0, 1.0);
    if (estimate.confidence < minimum_confidence) {
        return true;
    }

    double refined_lag = static_cast<double>(best_lag);
    if (best_lag > 1 && best_lag < max_lag) {
        const double previous = difference[best_lag - 1];
        const double current = difference[best_lag];
        const double next = difference[best_lag + 1];
        const double curvature = previous - 2.0 * current + next;
        if (std::abs(curvature) > std::numeric_limits<double>::epsilon()) {
            refined_lag += 0.5 * (previous - next) / curvature;
        }
    }

    if (refined_lag <= 0.0 || !std::isfinite(refined_lag)) {
        return true;
    }

    const double frequency_hz = static_cast<double>(sample_rate) / refined_lag;
    if (frequency_hz >= min_frequency_hz && frequency_hz <= max_frequency_hz) {
        estimate.frequency_hz = frequency_hz;
    }
    return true;
}

} // namespace tinnotus::audio
