include_guard(GLOBAL)

option(TINNOTUS_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(TINNOTUS_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(TINNOTUS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

include(CheckCXXSourceCompiles)

function(tinnotus_apply_sanitizers)
    get_property(_sanitizers_applied GLOBAL PROPERTY TINNOTUS_SANITIZERS_APPLIED)
    if(_sanitizers_applied)
        return()
    endif()
    set_property(GLOBAL PROPERTY TINNOTUS_SANITIZERS_APPLIED TRUE)

    if(TINNOTUS_ENABLE_ASAN AND TINNOTUS_ENABLE_TSAN)
        message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled together")
    endif()

    if(NOT TINNOTUS_ENABLE_ASAN AND
       NOT TINNOTUS_ENABLE_TSAN AND
       NOT TINNOTUS_ENABLE_UBSAN)
        return()
    endif()

    if(MSVC)
        if(TINNOTUS_ENABLE_TSAN OR TINNOTUS_ENABLE_UBSAN)
            message(FATAL_ERROR "This MSVC toolchain supports TINNOTUS_ENABLE_ASAN only")
        endif()
        if(MSVC_VERSION LESS 1928)
            message(FATAL_ERROR "TINNOTUS_ENABLE_ASAN requires MSVC 19.28 or later")
        endif()
        add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
        return()
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "The enabled Tinnotus sanitizers require GCC, Clang, or MSVC")
    endif()
    if(WIN32 AND TINNOTUS_ENABLE_TSAN)
        message(FATAL_ERROR "ThreadSanitizer is not supported on Windows")
    endif()

    set(_sanitizers "")
    if(TINNOTUS_ENABLE_ASAN)
        list(APPEND _sanitizers address)
    endif()
    if(TINNOTUS_ENABLE_TSAN)
        list(APPEND _sanitizers thread)
    endif()
    if(TINNOTUS_ENABLE_UBSAN)
        list(APPEND _sanitizers undefined)
    endif()

    list(JOIN _sanitizers "," _sanitizer_list)
    string(MAKE_C_IDENTIFIER "${_sanitizer_list}" _sanitizer_key)
    set(_support_variable "TINNOTUS_SANITIZER_${_sanitizer_key}_SUPPORTED")
    set(_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    set(_saved_required_link_options "${CMAKE_REQUIRED_LINK_OPTIONS}")
    string(APPEND CMAKE_REQUIRED_FLAGS
        " -fsanitize=${_sanitizer_list} -fno-omit-frame-pointer"
    )
    list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=${_sanitizer_list}")
    check_cxx_source_compiles(
        "int main() { return 0; }"
        ${_support_variable}
    )
    set(CMAKE_REQUIRED_FLAGS "${_saved_required_flags}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${_saved_required_link_options}")
    if(NOT "${${_support_variable}}")
        message(FATAL_ERROR
            "The selected compiler/linker does not support sanitizers: ${_sanitizer_list}"
        )
    endif()

    add_compile_options("-fsanitize=${_sanitizer_list}" -fno-omit-frame-pointer)
    add_link_options("-fsanitize=${_sanitizer_list}")
endfunction()
