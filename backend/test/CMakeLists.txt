add_executable(tinnotus_audio_engine_tests audio_engine_tests.cpp)
target_link_libraries(tinnotus_audio_engine_tests PRIVATE tinnotus_backend)
target_compile_features(tinnotus_audio_engine_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(tinnotus_audio_engine_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(tinnotus_audio_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME tinnotus_audio_engine COMMAND tinnotus_audio_engine_tests)

find_package(Threads REQUIRED)

add_executable(tinnotus_audio_engine_stress_tests audio_engine_stress_tests.cpp)
target_link_libraries(tinnotus_audio_engine_stress_tests
    PRIVATE
        tinnotus_backend
        Threads::Threads
)
target_compile_features(tinnotus_audio_engine_stress_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(tinnotus_audio_engine_stress_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(tinnotus_audio_engine_stress_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(
    NAME tinnotus_audio_engine_stress
    COMMAND tinnotus_audio_engine_stress_tests
)
set_tests_properties(tinnotus_audio_engine_stress PROPERTIES LABELS "stress" TIMEOUT 300)

if(MSVC AND TINNOTUS_ENABLE_ASAN)
    get_filename_component(_tinnotus_asan_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set_tests_properties(
        tinnotus_audio_engine
        tinnotus_audio_engine_stress
        PROPERTIES ENVIRONMENT "PATH=${_tinnotus_asan_runtime_dir}\;$ENV{PATH}"
    )
endif()
