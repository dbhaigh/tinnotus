cmake_minimum_required(VERSION 3.16)
project(tinnotus_backend LANGUAGES CXX)

include(CTest)
option(TINNOTUS_BUILD_TEST "Build Tinnotus audio-core tests" ${BUILD_TESTING})

add_library(tinnotus_backend STATIC
    ../shared/audio_engine.cpp
    ../shared/audio_engine_ffi.cpp
)
target_include_directories(tinnotus_backend
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../shared>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(tinnotus_backend PUBLIC cxx_std_17)
target_compile_definitions(tinnotus_backend PUBLIC TINNOTUS_AUDIO_STATIC)

if(MSVC)
    target_compile_options(tinnotus_backend PRIVATE /W4 /permissive-)
else()
    target_compile_options(tinnotus_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS tinnotus_backend ARCHIVE DESTINATION lib)
install(FILES
    ../shared/audio_engine.hpp
    ../shared/audio_engine_ffi.h
    DESTINATION include/tinnotus
)

if(TINNOTUS_BUILD_TEST)
    add_subdirectory(test)
endif()
