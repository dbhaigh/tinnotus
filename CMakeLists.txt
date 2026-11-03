cmake_minimum_required(VERSION 3.16)
project(tinnotus VERSION 0.0.0.2 LANGUAGES CXX)

include(CTest)
option(TINNOTUS_BUILD_TEST "Build Tinnotus audio-core tests" ${BUILD_TESTING})
option(TINNOTUS_BUILD_BENCH "Build Tinnotus audio-engine benchmark" OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TinnotusSanitizers.cmake")
tinnotus_apply_sanitizers()

if(TINNOTUS_BUILD_TEST)
    enable_testing()
endif()

add_subdirectory(backend)
