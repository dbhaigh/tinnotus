#include "audio_engine.hpp"
#include "audio_engine_ffi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_buffer_wraparound_and_capacity() {
    tinnotus::audio::SpscAudioBuffer buffer(5);
    const float first[] = {1.0f, 2.0f, 3.0f, 4.0f};
    const float overflow[] = {5.0f, 6.0f};
    float output[5] = {};

    check(buffer.tryWrite(first, 4), "buffer accepts samples within capacity");
    check(!buffer.tryWrite(overflow, 2), "buffer rejects writes that exceed capacity");
    check(buffer.tryRead(output, 3) == 3, "buffer reads requested available samples");
    check(output[0] == 1.0f && output[2] == 3.0f, "buffer preserves FIFO order");

    const float wrapped[] = {5.0f, 6.0f, 7.0f};
    check(buffer.tryWrite(wrapped, 3), "buffer writes across the ring boundary");
    check(buffer.tryRead(output, 5) == 4, "buffer returns remaining sample count");
    check(output[0] == 4.0f && output[1] == 5.0f &&
          output[2] == 6.0f && output[3] == 7.0f,
          "buffer preserves FIFO order across wraparound");
    check(buffer.available() == 0, "buffer reports empty after draining");
}

void test_buffer_zero_capacity_and_null_operations() {
    tinnotus::audio::SpscAudioBuffer buffer(0);
    float sample = 1.0f;

    check(buffer.capacity() == 0, "zero-capacity buffer reports zero capacity");
    check(buffer.available() == 0, "zero-capacity buffer is empty");
    check(buffer.tryWrite(nullptr, 0), "zero-length write accepts a null source");
    check(buffer.tryWrite(&sample, 0), "zero-length write does not require capacity");
    check(!buffer.tryWrite(&sample, 1), "zero-capacity buffer rejects a sample");
    check(!buffer.tryWrite(nullptr, 1), "non-empty write rejects a null source");
    check(buffer.tryRead(nullptr, 0) == 0,
          "zero-length read accepts a null destination");
    check(buffer.tryRead(nullptr, 1) == 0,
          "read with a null destination returns no samples");
    check(buffer.tryRead(&sample, 1) == 0,
          "zero-capacity buffer cannot return samples");
    check(buffer.available() == 0, "zero-capacity buffer stays empty after operations");
}

void test_buffer_single_sample_capacity() {
    tinnotus::audio::SpscAudioBuffer buffer(1);
    float output = 0.0f;

    check(buffer.tryWrite(nullptr, 0), "zero-length write accepts a null source");
    check(buffer.tryRead(nullptr, 0) == 0,
          "zero-length read accepts a null destination");
    const float initial = 42.0f;
    check(buffer.tryWrite(&initial, 1), "single-sample buffer accepts a sample");
    check(!buffer.tryWrite(nullptr, 1), "non-empty write rejects a null source");
    check(buffer.tryRead(nullptr, 1) == 0, "non-empty read rejects a null destination");
    check(buffer.available() == 1, "null read leaves the buffered sample untouched");
    check(buffer.tryRead(&output, 1) == 1 && output == initial,
          "single-sample buffer still returns data after a null read");

    check(buffer.tryRead(&output, 1) == 0, "single-sample buffer starts empty");
    for (int i = 0; i < 32; ++i) {
        const float input = static_cast<float>(i + 1);
        check(buffer.tryWrite(&input, 1), "single-sample buffer accepts one sample");
        check(buffer.available() == 1, "single-sample buffer reports full capacity");
        check(!buffer.tryWrite(&input, 1), "single-sample buffer rejects a full write");
        check(buffer.tryRead(&output, 1) == 1, "single-sample buffer returns its sample");
        check(output == input, "single-sample buffer preserves data across wraparound");
        check(buffer.available() == 0, "single-sample buffer is empty after reading");
    }
}

void test_buffer_power_of_two_exact_wraparound() {
    tinnotus::audio::SpscAudioBuffer buffer(4);
    const float initial[] = {1.0f, 2.0f, 3.0f};
    const float wrapped[] = {4.0f, 5.0f, 6.0f};
    const float overflow = 7.0f;
    float output[4] = {};

    check(buffer.tryWrite(initial, 3), "power-of-two buffer accepts an initial block");
    check(buffer.tryRead(output, 2) == 2, "power-of-two buffer advances its read index");
    check(output[0] == 1.0f && output[1] == 2.0f,
          "power-of-two buffer preserves the initial samples");
    check(buffer.tryWrite(wrapped, 3),
          "power-of-two buffer writes exactly across the ring boundary");
    check(buffer.available() == buffer.capacity(),
          "power-of-two buffer reports full after an exact-capacity wraparound");
    check(!buffer.tryWrite(&overflow, 1), "full power-of-two buffer rejects overflow");
    check(buffer.tryRead(output, 4) == 4,
          "power-of-two buffer reads all samples after wraparound");
    check(output[0] == 3.0f && output[1] == 4.0f &&
          output[2] == 5.0f && output[3] == 6.0f,
          "power-of-two buffer preserves FIFO order across exact wraparound");
    check(buffer.available() == 0, "power-of-two buffer is empty after draining");
}

double measure_tone_ratio(double frequency_hz, bool enable_notch) {
    constexpr int sample_rate = 48000;
    constexpr std::size_t frame_count = sample_rate;
    constexpr double pi = 3.14159265358979323846;

    tinnotus::audio::AudioEngine engine(sample_rate, 1);
    if (enable_notch &&
        engine.setNotch(0, 1000.0, 100.0) !=
            tinnotus::audio::RequestResult::queued) {
        return -1.0;
    }

    std::vector<float> input(frame_count);
    std::vector<float> output(frame_count);
    for (std::size_t i = 0; i < frame_count; ++i) {
        input[i] = static_cast<float>(
            std::sin(2.0 * pi * frequency_hz * static_cast<double>(i) / sample_rate)
        );
    }
    if (!engine.process(input.data(), output.data(), frame_count)) {
        return -1.0;
    }

    constexpr std::size_t measurement_start = frame_count / 2;
    double input_energy = 0.0;
    double output_energy = 0.0;
    for (std::size_t i = measurement_start; i < frame_count; ++i) {
        input_energy += static_cast<double>(input[i]) * input[i];
        output_energy += static_cast<double>(output[i]) * output[i];
    }

    return std::sqrt(output_energy / input_energy);
}

void test_notch_response() {
    const double target_ratio = measure_tone_ratio(1000.0, true);
    const double passband_ratio = measure_tone_ratio(4000.0, true);
    check(target_ratio >= 0.0 && target_ratio < 0.1,
          "notch attenuates its center frequency by at least 20 dB");
    check(passband_ratio > 0.9 && passband_ratio < 1.1,
          "notch preserves a distant passband tone");
}

void test_notch_updates_are_smoothed() {
    constexpr int sample_rate = 48000;
    constexpr double pi = 3.14159265358979323846;
    constexpr double frequency = 1000.0;
    constexpr std::size_t warmup_frames = sample_rate;
    constexpr std::size_t transition_frames = sample_rate / 50;

    tinnotus::audio::AudioEngine engine(sample_rate, 1);
    check(engine.setNotch(0, frequency, 100.0) ==
          tinnotus::audio::RequestResult::queued,
          "transition test configures its initial notch");

    std::vector<float> warmup_input(warmup_frames);
    std::vector<float> warmup_output(warmup_frames);
    for (std::size_t frame = 0; frame < warmup_frames; ++frame) {
        warmup_input[frame] = static_cast<float>(
            std::sin(2.0 * pi * frequency * static_cast<double>(frame) / sample_rate + pi / 2.0)
        );
    }
    check(engine.process(warmup_input.data(), warmup_output.data(), warmup_frames),
          "engine settles the initial notch");
    check(std::abs(warmup_output.back()) < 0.1f,
          "initial notch attenuates the tone before an update");

    check(engine.setNotch(0, 5000.0, 100.0) ==
          tinnotus::audio::RequestResult::queued,
          "engine accepts a live notch update");
    std::vector<float> transition_input(transition_frames);
    std::vector<float> transition_output(transition_frames);
    for (std::size_t frame = 0; frame < transition_frames; ++frame) {
        const std::size_t source_frame = warmup_frames + frame;
        transition_input[frame] = static_cast<float>(
            std::sin(2.0 * pi * frequency * static_cast<double>(source_frame) / sample_rate +
                     pi / 2.0)
        );
    }
    check(engine.process(
              transition_input.data(),
              transition_output.data(),
              transition_frames
          ),
          "engine processes a smoothed notch transition");

    double maximum_step = std::abs(
        static_cast<double>(transition_output.front()) - warmup_output.back()
    );
    for (std::size_t frame = 1; frame < transition_frames; ++frame) {
        maximum_step = std::max(
            maximum_step,
            std::abs(static_cast<double>(transition_output[frame]) -
                     transition_output[frame - 1])
        );
    }
    check(maximum_step < 0.25,
          "notch update does not produce a large one-sample discontinuity");

    constexpr std::size_t measurement_frames = sample_rate;
    std::vector<float> measurement_input(measurement_frames);
    std::vector<float> measurement_output(measurement_frames);
    for (std::size_t frame = 0; frame < measurement_frames; ++frame) {
        const std::size_t source_frame = warmup_frames + transition_frames + frame;
        measurement_input[frame] = static_cast<float>(
            std::sin(2.0 * pi * frequency * static_cast<double>(source_frame) / sample_rate +
                     pi / 2.0)
        );
    }
    check(engine.process(
              measurement_input.data(),
              measurement_output.data(),
              measurement_frames
          ),
          "engine processes audio after the transition");

    double energy = 0.0;
    for (std::size_t frame = measurement_frames / 2; frame < measurement_frames; ++frame) {
        energy += static_cast<double>(measurement_output[frame]) * measurement_output[frame];
    }
    const double rms = std::sqrt(energy / (measurement_frames / 2));
    check(rms > 0.6, "updated notch passes the original tone after the transition");
}

void test_rapid_notch_updates_remain_finite_and_bounded() {
    constexpr int sample_rate = 48000;
    constexpr std::size_t block_frames = sample_rate / 200;
    constexpr double pi = 3.14159265358979323846;
    constexpr double frequency = 1000.0;

    tinnotus::audio::AudioEngine engine(sample_rate, 1);
    std::array<float, block_frames> input{};
    std::array<float, block_frames> output{};
    double previous_sample = 0.0;
    double maximum_step = 0.0;

    for (std::size_t update = 0; update < 8; ++update) {
        const double center_frequency = 500.0 + 900.0 * static_cast<double>(update);
        check(engine.setNotch(0, center_frequency, 100.0) ==
              tinnotus::audio::RequestResult::queued,
              "engine queues rapid notch updates");
        for (std::size_t frame = 0; frame < block_frames; ++frame) {
            const std::size_t source_frame = update * block_frames + frame;
            input[frame] = static_cast<float>(
                0.5 * std::sin(2.0 * pi * frequency * static_cast<double>(source_frame) /
                               sample_rate)
            );
        }
        check(engine.process(input.data(), output.data(), block_frames),
              "engine processes a rapid notch-update block");

        for (std::size_t frame = 0; frame < block_frames; ++frame) {
            check(std::isfinite(output[frame]) && std::abs(output[frame]) < 2.0f,
                  "rapid updates keep output finite and bounded");
            maximum_step = std::max(
                maximum_step,
                std::abs(static_cast<double>(output[frame]) - previous_sample)
            );
            previous_sample = output[frame];
        }
    }

    check(engine.removeNotch(0) == tinnotus::audio::RequestResult::queued,
          "engine queues a removal during a transition");
    for (std::size_t frame = 0; frame < block_frames; ++frame) {
        const std::size_t source_frame = 8 * block_frames + frame;
        input[frame] = static_cast<float>(
            0.5 * std::sin(2.0 * pi * frequency * static_cast<double>(source_frame) /
                           sample_rate)
        );
    }
    check(engine.process(input.data(), output.data(), block_frames),
          "engine processes a removal during a transition");
    for (std::size_t frame = 0; frame < block_frames; ++frame) {
        check(std::isfinite(output[frame]) && std::abs(output[frame]) < 2.0f,
              "notch removal keeps output finite and bounded");
        maximum_step = std::max(
            maximum_step,
            std::abs(static_cast<double>(output[frame]) - previous_sample)
        );
        previous_sample = output[frame];
    }

    check(maximum_step < 0.5,
          "rapid retargeting and removal do not create a large discontinuity");
}

void test_identity_and_stereo_isolation() {
    tinnotus::audio::AudioEngine identity(48000, 2);
    const float input[] = {0.25f, -0.5f, 0.75f, -1.0f};
    float output[4] = {};
    check(identity.process(input, output, 2), "unfiltered engine processes a block");
    check(output[0] == input[0] && output[1] == input[1] &&
          output[2] == input[2] && output[3] == input[3],
          "unfiltered engine is an exact passthrough");

    tinnotus::audio::AudioEngine stereo(48000, 2);
    check(stereo.setNotch(0, 1000.0, 100.0) ==
          tinnotus::audio::RequestResult::queued,
          "stereo engine accepts a notch");
    std::vector<float> stereo_input(2048 * 2);
    std::vector<float> stereo_output(stereo_input.size(), 1.0f);
    for (std::size_t frame = 0; frame < 2048; ++frame) {
        stereo_input[frame * 2] = 0.5f;
        stereo_input[frame * 2 + 1] = 0.0f;
    }
    check(stereo.process(stereo_input.data(), stereo_output.data(), 2048),
          "stereo engine processes interleaved frames");
    bool right_channel_is_silent = true;
    for (std::size_t frame = 0; frame < 2048; ++frame) {
        right_channel_is_silent &= stereo_output[frame * 2 + 1] == 0.0f;
    }
    check(right_channel_is_silent, "filter state is isolated per channel");
}

struct ReferenceCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct ReferenceState {
    double z1 = 0.0;
    double z2 = 0.0;
};

void process_scalar_reference(
    const float* input,
    float* output,
    std::size_t frame_count,
    std::size_t channels,
    const std::array<ReferenceCoefficients, tinnotus::audio::AudioEngine::max_notches>& coefficients
) {
    std::array<
        std::array<ReferenceState, tinnotus::audio::AudioEngine::max_channels>,
        tinnotus::audio::AudioEngine::max_notches
    > states{};

    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const std::size_t sample_index = frame * channels + channel;
            double sample = input[sample_index];
            for (std::size_t notch = 0; notch < coefficients.size(); ++notch) {
                ReferenceState& state = states[notch][channel];
                const ReferenceCoefficients& filter = coefficients[notch];
                const double filtered = filter.b0 * sample + state.z1;
                state.z1 = filter.b1 * sample - filter.a1 * filtered + state.z2;
                state.z2 = filter.b2 * sample - filter.a2 * filtered;
                sample = filtered;
            }
            output[sample_index] = static_cast<float>(sample);
        }
    }
}

void test_channel_processing_matches_scalar_reference() {
    constexpr int sample_rate = 48000;
    constexpr std::size_t frame_count = 257;
    constexpr std::size_t first_block_frames = 37;
    constexpr double pi = 3.14159265358979323846;
    constexpr std::array<std::size_t, 7> channel_counts{1, 2, 4, 5, 6, 7, 8};

    for (const std::size_t channels : channel_counts) {
        tinnotus::audio::AudioEngine engine(sample_rate, channels);
        std::array<ReferenceCoefficients, tinnotus::audio::AudioEngine::max_notches>
            coefficients{};
        bool configured = true;
        for (std::size_t notch = 0; notch < coefficients.size(); ++notch) {
            const double center_frequency = 500.0 + 900.0 * static_cast<double>(notch);
            const double bandwidth = 90.0 + 11.0 * static_cast<double>(notch);
            const double omega = 2.0 * pi * center_frequency / sample_rate;
            const double q = center_frequency / bandwidth;
            const double alpha = std::sin(omega) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            const double cosine = std::cos(omega);

            coefficients[notch].b0 = 1.0 / a0;
            coefficients[notch].b1 = -2.0 * cosine / a0;
            coefficients[notch].b2 = 1.0 / a0;
            coefficients[notch].a1 = -2.0 * cosine / a0;
            coefficients[notch].a2 = (1.0 - alpha) / a0;
            configured &= engine.setNotch(notch, center_frequency, bandwidth) ==
                tinnotus::audio::RequestResult::queued;
        }
        check(configured, "scalar-reference test configures every notch");
        if (!configured) {
            continue;
        }

        std::vector<float> input(frame_count * channels);
        std::vector<float> output(input.size());
        std::vector<float> expected(input.size());
        std::uint32_t random_state = 0x12345678U + static_cast<std::uint32_t>(channels);
        for (float& sample : input) {
            random_state = random_state * 1664525U + 1013904223U;
            sample = static_cast<float>((random_state >> 8) & 0xFFFFU) / 32767.5f - 1.0f;
        }

        process_scalar_reference(
            input.data(),
            expected.data(),
            frame_count,
            channels,
            coefficients
        );
        const bool processed_first_block = engine.process(
            input.data(),
            output.data(),
            first_block_frames
        );
        const bool processed_second_block = engine.process(
            input.data() + first_block_frames * channels,
            output.data() + first_block_frames * channels,
            frame_count - first_block_frames
        );
        check(processed_first_block && processed_second_block,
              "engine processes split blocks");

        bool matches_reference = true;
        for (std::size_t sample = 0; sample < output.size(); ++sample) {
            if (std::abs(static_cast<double>(output[sample]) - expected[sample]) > 1.0e-6) {
                matches_reference = false;
                break;
            }
        }
        check(matches_reference,
              "output matches the scalar reference within 1e-6");
    }
}

void test_validation_and_bounded_commands() {
    tinnotus::audio::AudioEngine invalid_engine(0, 1);
    check(!invalid_engine.isValid(), "engine rejects an invalid sample rate");

    tinnotus::audio::AudioEngine engine(48000, 1);
    check(engine.setNotch(0, 24000.0, 100.0) ==
          tinnotus::audio::RequestResult::invalid_argument,
          "engine rejects a notch at Nyquist");
    check(engine.setNotch(0, 1000.0, 0.0) ==
          tinnotus::audio::RequestResult::invalid_argument,
          "engine rejects zero bandwidth");
    check(engine.setNotch(8, 1000.0, 100.0) ==
          tinnotus::audio::RequestResult::invalid_argument,
          "engine rejects an out-of-range notch index");

    for (std::size_t i = 0; i < 16; ++i) {
        check(engine.setNotch(0, 1000.0 + static_cast<double>(i), 100.0) ==
              tinnotus::audio::RequestResult::queued,
              "command queue accepts commands up to its fixed capacity");
    }
    check(engine.removeNotch(0) == tinnotus::audio::RequestResult::queue_full,
          "command queue reports overflow instead of dropping silently");
    check(engine.process(nullptr, nullptr, 0),
          "zero-frame processing drains pending control commands");
    check(engine.removeNotch(0) == tinnotus::audio::RequestResult::queued,
          "command queue accepts commands after the audio thread drains it");
}

void test_pitch_detection() {
    constexpr int sample_rate = 48000;
    constexpr std::size_t sample_count = 4096;
    constexpr double pi = 3.14159265358979323846;
    constexpr std::array<double, 6> frequencies{55.0, 110.0, 220.0, 440.0, 880.0, 1760.0};
    std::vector<float> samples(sample_count);
    double detected_frequency = 0.0;
    double confidence = 0.0;

    for (const double frequency : frequencies) {
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = static_cast<float>(
                std::sin(2.0 * pi * frequency * static_cast<double>(i) / sample_rate)
            );
        }
        check(tinnotus_pitch_detect(
                  samples.data(),
                  samples.size(),
                  sample_rate,
                  &detected_frequency,
                  &confidence
              ) == TINNOTUS_AUDIO_OK,
              "pitch API accepts a valid analysis window");
        check(detected_frequency > 0.0 &&
                  std::abs(detected_frequency - frequency) <= std::max(1.0, frequency * 0.01),
              "pitch estimate is within one percent for a synthetic sine");
        check(confidence >= 0.8, "synthetic sine produces high pitch confidence");
    }

    std::fill(samples.begin(), samples.end(), 0.0f);
    check(tinnotus_pitch_detect(
              samples.data(),
              samples.size(),
              sample_rate,
              &detected_frequency,
              &confidence
          ) == TINNOTUS_AUDIO_OK,
          "pitch API accepts a silent window");
    check(detected_frequency == 0.0 && confidence < 0.1,
          "silence returns no pitch and low confidence");

    std::uint32_t random_state = 0xC0FFEEU;
    for (float& sample : samples) {
        random_state = random_state * 1664525U + 1013904223U;
        sample = static_cast<float>((random_state >> 8) & 0xFFFFU) / 32767.5f - 1.0f;
    }
    check(tinnotus_pitch_detect(
              samples.data(),
              samples.size(),
              sample_rate,
              &detected_frequency,
              &confidence
          ) == TINNOTUS_AUDIO_OK,
          "pitch API accepts a noise window");
    check(detected_frequency == 0.0 && confidence < 0.8,
          "noise returns no pitch and low confidence");

    check(tinnotus_pitch_detect(
              nullptr,
              sample_count,
              sample_rate,
              &detected_frequency,
              &confidence
          ) == TINNOTUS_AUDIO_INVALID_ARGUMENT,
          "pitch API rejects a null sample window");
    check(tinnotus_pitch_detect(
              samples.data(),
              sample_count,
              sample_rate,
              nullptr,
              &confidence
          ) == TINNOTUS_AUDIO_INVALID_ARGUMENT,
          "pitch API rejects a null result pointer");
}

void test_c_api() {
    tinnotus_audio_engine* engine = tinnotus_audio_engine_create(48000, 1);
    check(engine != nullptr, "C API creates a valid engine");
    if (engine == nullptr) {
        return;
    }

    float input[] = {0.5f, -0.25f};
    float output[2] = {};
    check(tinnotus_audio_engine_set_notch(engine, 0, 1000.0, 100.0) ==
          TINNOTUS_AUDIO_OK,
          "C API queues notch configuration");
    check(tinnotus_audio_engine_process(engine, input, output, 2) ==
          TINNOTUS_AUDIO_OK,
          "C API processes audio");
    check(tinnotus_audio_engine_process(engine, nullptr, output, 2) ==
          TINNOTUS_AUDIO_INVALID_ARGUMENT,
          "C API reports invalid buffers");
    check(tinnotus_audio_engine_set_notch(nullptr, 0, 1000.0, 100.0) ==
          TINNOTUS_AUDIO_INVALID_ARGUMENT,
          "C API reports a null engine");
    tinnotus_audio_engine_destroy(engine);
    tinnotus_audio_engine_destroy(nullptr);
}

} // namespace

int main() {
    test_buffer_wraparound_and_capacity();
    test_buffer_zero_capacity_and_null_operations();
    test_buffer_single_sample_capacity();
    test_buffer_power_of_two_exact_wraparound();
    test_notch_response();
    test_notch_updates_are_smoothed();
    test_rapid_notch_updates_remain_finite_and_bounded();
    test_identity_and_stereo_isolation();
    test_channel_processing_matches_scalar_reference();
    test_validation_and_bounded_commands();
    test_pitch_detection();
    test_c_api();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All Tinnotus audio-engine tests passed\n";
    return 0;
}
