cmake_minimum_required(VERSION 3.16)
project(tinnotus_shared LANGUAGES C)

# Set output directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# C standard
set(C_STANDARD 99)
set(C_STANDARD_REQUIRED ON)

# Platform detection
if(APPLE)
    set(TINNOTUS_PLATFORM_MACOS ON)
endif()
if(WIN32)
    set(TINNOTUS_PLATFORM_WINDOWS ON)
endif()
if(LINUX)
    set(TINNOTUS_PLATFORM_LINUX ON)
endif()

# ============================================================================
# Source Files
# ============================================================================

# DSP implementation
add_custom_command(
    OUTPUT tinnotus_dsp_shared
    COMMAND ${CMAKE_COMMAND} -E echo "DSP implementation from Rust (see CMAKE_SOURCE_DIR/dsp/)"
    DEPENDS ${CMAKE_SOURCE_DIR}/dsp/dsp_filter.cpp
            ${CMAKE_SOURCE_DIR}/dsp/fft.cpp
            ${CMAKE_SOURCE_DIR}/dsp/pitch_detector.cpp
    COMMENT "Building tinnotus DSP shared library"
)

# FFI header
set(TINNOTUS_FFI_HEADER "${CMAKE_BINARY_DIR}/include/tinnotus_ffi.h")
add_custom_command(
    OUTPUT ${TINNOTUS_FFI_HEADER}
    COMMAND ${CMAKE_COMMAND} -E echo "FFI header generated"
    DEPENDS ${CMAKE_SOURCE_DIR}/lib.rs
    COMMENT "Generating FFI header"
)

# ============================================================================
# Build Targets
# ============================================================================

# Build Rust shared library
add_custom_target(tinnotus_shared_all
    DEPENDS tinnotus_dsp_shared
    DEPENDS tinnotus_ffi_header
)

# Platform-specific build targets
if(TINNOTUS_PLATFORM_MACOS)
    add_custom_target(tinnotus_shared_macos
        DEPENDS tinnotus_shared_all
        COMMAND echo "macOS shared library target"
    )
endif()

if(TINNOTUS_PLATFORM_WINDOWS)
    add_custom_target(tinnotus_shared_windows
        DEPENDS tinnotus_shared_all
        COMMAND echo "Windows shared library target"
    )
endif()

if(TINNOTUS_PLATFORM_LINUX)
    add_custom_target(tinnotus_shared_linux
        DEPENDS tinnotus_shared_all
        COMMAND echo "Linux shared library target"
    )
endif()

# ============================================================================
# Installation
# ============================================================================

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Install shared library stub
install(
    FILES ${TINNOTUS_FFI_HEADER}
    DESTINATION include/tinnotus
)

# ============================================================================
# Testing
# ============================================================================

enable_testing()