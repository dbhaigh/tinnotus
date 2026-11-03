#include "audio_engine.hpp"
#include "audio_engine_ffi.h"

#include <cmath>
#include <cstddef>
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
    test_notch_response();
    test_identity_and_stereo_isolation();
    test_validation_and_bounded_commands();
    test_c_api();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All Tinnotus audio-engine tests passed\n";
    return 0;
}
