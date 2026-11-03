add_executable(tinnotus_audio_engine_tests audio_engine_tests.cpp)
target_link_libraries(tinnotus_audio_engine_tests PRIVATE tinnotus_backend)
target_compile_features(tinnotus_audio_engine_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(tinnotus_audio_engine_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(tinnotus_audio_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME tinnotus_audio_engine COMMAND tinnotus_audio_engine_tests)
