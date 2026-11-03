#ifndef TINNOTUS_AUDIO_ENGINE_FFI_H
#define TINNOTUS_AUDIO_ENGINE_FFI_H

#include <stddef.h>

#if defined(TINNOTUS_AUDIO_STATIC)
#define TINNOTUS_AUDIO_API
#elif defined(_WIN32)
#if defined(TINNOTUS_AUDIO_BUILD)
#define TINNOTUS_AUDIO_API __declspec(dllexport)
#else
#define TINNOTUS_AUDIO_API __declspec(dllimport)
#endif
#else
#define TINNOTUS_AUDIO_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tinnotus_audio_engine tinnotus_audio_engine;

enum tinnotus_audio_status {
    TINNOTUS_AUDIO_OK = 0,
    TINNOTUS_AUDIO_INVALID_ARGUMENT = -1,
    TINNOTUS_AUDIO_QUEUE_FULL = -2
};

TINNOTUS_AUDIO_API tinnotus_audio_engine* tinnotus_audio_engine_create(
    int sample_rate,
    size_t channels
);
TINNOTUS_AUDIO_API void tinnotus_audio_engine_destroy(tinnotus_audio_engine* engine);
TINNOTUS_AUDIO_API int tinnotus_audio_engine_set_notch(
    tinnotus_audio_engine* engine,
    size_t index,
    double center_frequency_hz,
    double bandwidth_hz
);
TINNOTUS_AUDIO_API int tinnotus_audio_engine_remove_notch(
    tinnotus_audio_engine* engine,
    size_t index
);
TINNOTUS_AUDIO_API int tinnotus_audio_engine_process(
    tinnotus_audio_engine* engine,
    const float* input,
    float* output,
    size_t frame_count
);

#ifdef __cplusplus
}
#endif

#endif
