cmake_minimum_required(VERSION 3.16)
project(tinnotus VERSION 0.0.0.1 LANGUAGES CXX)

include(CTest)
option(TINNOTUS_BUILD_TEST "Build Tinnotus audio-core tests" ${BUILD_TESTING})

if(TINNOTUS_BUILD_TEST)
    enable_testing()
endif()

add_subdirectory(backend)
