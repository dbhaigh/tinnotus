add_executable(tinnotus_audio_engine_bench audio_engine_bench.cpp)
target_link_libraries(tinnotus_audio_engine_bench PRIVATE tinnotus_backend)
target_compile_features(tinnotus_audio_engine_bench PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(tinnotus_audio_engine_bench PRIVATE /W4 /permissive-)
    set(_tinnotus_bench_base_flags "${CMAKE_CXX_FLAGS} /std:c++17 /W4 /permissive-")
else()
    target_compile_options(tinnotus_audio_engine_bench PRIVATE -Wall -Wextra -Wpedantic)
    set(_tinnotus_bench_base_flags "${CMAKE_CXX_FLAGS} -std=c++17 -Wall -Wextra -Wpedantic")
endif()

if(TINNOTUS_ENABLE_AVX2)
    if(MSVC)
        set(_tinnotus_bench_engine_simd "AVX2 (/arch:AVX2)")
    else()
        set(_tinnotus_bench_engine_simd "AVX2 (-mavx2)")
    endif()
else()
    set(_tinnotus_bench_engine_simd "compiler-default SIMD")
endif()

set(_tinnotus_bench_sanitizers "")
if(TINNOTUS_ENABLE_ASAN)
    list(APPEND _tinnotus_bench_sanitizers address)
endif()
if(TINNOTUS_ENABLE_TSAN)
    list(APPEND _tinnotus_bench_sanitizers thread)
endif()
if(TINNOTUS_ENABLE_UBSAN)
    list(APPEND _tinnotus_bench_sanitizers undefined)
endif()

if(_tinnotus_bench_sanitizers)
    if(MSVC)
        set(_tinnotus_bench_sanitizer_flags "/fsanitize=address")
    else()
        list(JOIN _tinnotus_bench_sanitizers "," _tinnotus_bench_sanitizer_list)
        set(_tinnotus_bench_sanitizer_flags
            "-fsanitize=${_tinnotus_bench_sanitizer_list} -fno-omit-frame-pointer"
        )
    endif()
else()
    set(_tinnotus_bench_sanitizer_flags "none")
endif()

foreach(_tinnotus_bench_flags_var
        _tinnotus_bench_base_flags
        _tinnotus_bench_engine_simd
        _tinnotus_bench_sanitizer_flags)
    string(REPLACE ";" " " ${_tinnotus_bench_flags_var}
        "${${_tinnotus_bench_flags_var}}"
    )
    string(REPLACE "\\" "\\\\" ${_tinnotus_bench_flags_var}
        "${${_tinnotus_bench_flags_var}}"
    )
    string(REPLACE "\"" "\\\"" ${_tinnotus_bench_flags_var}
        "${${_tinnotus_bench_flags_var}}"
    )
endforeach()

target_compile_definitions(tinnotus_audio_engine_bench PRIVATE
    "TINNOTUS_BENCH_BUILD_CONFIG=\"$<CONFIG>\""
    "TINNOTUS_BENCH_BASE_FLAGS=\"${_tinnotus_bench_base_flags}\""
    "TINNOTUS_BENCH_ENGINE_SIMD=\"${_tinnotus_bench_engine_simd}\""
    "TINNOTUS_BENCH_SANITIZER_FLAGS=\"${_tinnotus_bench_sanitizer_flags}\""
)

foreach(_tinnotus_bench_config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_tinnotus_bench_config}" _tinnotus_bench_config_upper)
    set(_tinnotus_bench_config_flags
        "${CMAKE_CXX_FLAGS_${_tinnotus_bench_config_upper}}"
    )
    string(REPLACE ";" " " _tinnotus_bench_config_flags
        "${_tinnotus_bench_config_flags}"
    )
    string(REPLACE "\\" "\\\\" _tinnotus_bench_config_flags
        "${_tinnotus_bench_config_flags}"
    )
    string(REPLACE "\"" "\\\"" _tinnotus_bench_config_flags
        "${_tinnotus_bench_config_flags}"
    )
    target_compile_definitions(tinnotus_audio_engine_bench PRIVATE
        "$<$<CONFIG:${_tinnotus_bench_config}>:TINNOTUS_BENCH_CONFIG_FLAGS=\"${_tinnotus_bench_config_flags}\">"
    )
endforeach()

add_custom_target(bench
    COMMAND
        "$<TARGET_FILE:tinnotus_audio_engine_bench>"
        --output-dir "${CMAKE_CURRENT_LIST_DIR}/../../docs/benchmarks"
    DEPENDS tinnotus_audio_engine_bench
    USES_TERMINAL
    VERBATIM
)
