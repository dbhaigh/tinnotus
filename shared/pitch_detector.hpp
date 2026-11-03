#ifndef TINNOTUS_PITCH_DETECTOR_HPP
#define TINNOTUS_PITCH_DETECTOR_HPP

#include <cstddef>

namespace tinnotus::audio {

struct PitchEstimate {
    double frequency_hz = 0.0;
    double confidence = 0.0;
};

class PitchDetector {
public:
    static constexpr std::size_t min_window_samples = 64;
    static constexpr std::size_t max_window_samples = 8192;

    static bool detect(
        const float* samples,
        std::size_t sample_count,
        int sample_rate,
        PitchEstimate& estimate
    ) noexcept;
};

} // namespace tinnotus::audio

#endif
