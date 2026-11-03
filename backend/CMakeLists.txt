cmake_minimum_required(VERSION 3.16)
project(tinnotus_backend LANGUAGES CXX)

include(CTest)
option(TINNOTUS_BUILD_TEST "Build Tinnotus audio-core tests" ${BUILD_TESTING})
option(TINNOTUS_BUILD_BENCH "Build Tinnotus audio-engine benchmark" OFF)
option(TINNOTUS_ENABLE_AVX2
    "Compile the audio engine with AVX2 instructions (requires an AVX2 CPU)"
    OFF
)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/TinnotusSanitizers.cmake")
tinnotus_apply_sanitizers()

add_library(tinnotus_backend STATIC
    ../shared/audio_engine.cpp
    ../shared/audio_engine_ffi.cpp
    ../shared/pitch_detector.cpp
)
target_include_directories(tinnotus_backend
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../shared>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tinnotus_backend PUBLIC cxx_std_17)
target_compile_definitions(tinnotus_backend PUBLIC TINNOTUS_AUDIO_STATIC)

if(TINNOTUS_ENABLE_AVX2)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR} ${CMAKE_GENERATOR_PLATFORM}"
        _tinnotus_target_arch
    )
    if(NOT _tinnotus_target_arch MATCHES "x86_64|amd64|x64|i[3-6]86|win32")
        message(FATAL_ERROR "TINNOTUS_ENABLE_AVX2 requires an x86 target")
    endif()

    if(MSVC)
        target_compile_options(tinnotus_backend PRIVATE /arch:AVX2)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(tinnotus_backend PRIVATE -mavx2)
    else()
        message(FATAL_ERROR
            "TINNOTUS_ENABLE_AVX2 is unsupported by this compiler"
        )
    endif()
    target_compile_definitions(tinnotus_backend PRIVATE
        TINNOTUS_AUDIO_ENABLE_AVX2=1
    )
endif()

if(MSVC)
    target_compile_options(tinnotus_backend PRIVATE /W4 /permissive-)
else()
    target_compile_options(tinnotus_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS tinnotus_backend ARCHIVE DESTINATION lib)
install(FILES
    ../shared/audio_engine.hpp
    ../shared/audio_engine_ffi.h
    ../shared/pitch_detector.hpp
    DESTINATION include/tinnotus
)

if(TINNOTUS_BUILD_TEST)
    add_subdirectory(test)
endif()

if(TINNOTUS_BUILD_BENCH)
    add_subdirectory(bench)
endif()
