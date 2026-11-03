#include "audio_engine_ffi.h"

#include "audio_engine.hpp"

#include <new>

struct tinnotus_audio_engine {
    explicit tinnotus_audio_engine(int sample_rate, std::size_t channels) noexcept
        : implementation(sample_rate, channels) {}

    tinnotus::audio::AudioEngine implementation;
};

extern "C" tinnotus_audio_engine* tinnotus_audio_engine_create(
    int sample_rate,
    std::size_t channels
) {
    auto* engine = new (std::nothrow) tinnotus_audio_engine(sample_rate, channels);
    if (engine == nullptr || !engine->implementation.isValid()) {
        delete engine;
        return nullptr;
    }
    return engine;
}

extern "C" void tinnotus_audio_engine_destroy(tinnotus_audio_engine* engine) {
    delete engine;
}

extern "C" int tinnotus_audio_engine_set_notch(
    tinnotus_audio_engine* engine,
    std::size_t index,
    double center_frequency_hz,
    double bandwidth_hz
) {
    if (engine == nullptr) {
        return TINNOTUS_AUDIO_INVALID_ARGUMENT;
    }

    switch (engine->implementation.setNotch(
        index,
        center_frequency_hz,
        bandwidth_hz
    )) {
    case tinnotus::audio::RequestResult::queued:
        return TINNOTUS_AUDIO_OK;
    case tinnotus::audio::RequestResult::queue_full:
        return TINNOTUS_AUDIO_QUEUE_FULL;
    case tinnotus::audio::RequestResult::invalid_argument:
        return TINNOTUS_AUDIO_INVALID_ARGUMENT;
    }

    return TINNOTUS_AUDIO_INVALID_ARGUMENT;
}

extern "C" int tinnotus_audio_engine_remove_notch(
    tinnotus_audio_engine* engine,
    std::size_t index
) {
    if (engine == nullptr) {
        return TINNOTUS_AUDIO_INVALID_ARGUMENT;
    }

    switch (engine->implementation.removeNotch(index)) {
    case tinnotus::audio::RequestResult::queued:
        return TINNOTUS_AUDIO_OK;
    case tinnotus::audio::RequestResult::queue_full:
        return TINNOTUS_AUDIO_QUEUE_FULL;
    case tinnotus::audio::RequestResult::invalid_argument:
        return TINNOTUS_AUDIO_INVALID_ARGUMENT;
    }

    return TINNOTUS_AUDIO_INVALID_ARGUMENT;
}

extern "C" int tinnotus_audio_engine_process(
    tinnotus_audio_engine* engine,
    const float* input,
    float* output,
    std::size_t frame_count
) {
    if (engine == nullptr ||
        !engine->implementation.process(input, output, frame_count)) {
        return TINNOTUS_AUDIO_INVALID_ARGUMENT;
    }
    return TINNOTUS_AUDIO_OK;
}
