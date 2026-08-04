# Standalone, host-buildable test project for
# finlink_switch_remix_and_resample_to_stereo() (audio_remix.hpp/.cpp) --
# NOT part of the actual Switch devkitA64/borealis build (see
# clients/switch/CMakeLists.txt for that). audio_remix.cpp is deliberately
# free of any borealis/libnx types, so it can be built and tested with a
# plain host compiler instead of needing the devkitA64 toolchain or actual
# Switch/yuzu hardware -- same reasoning as clients/3ds/tests and
# clients/nds/arm9/tests.
cmake_minimum_required(VERSION 3.16)
project(finlink_switch_audio_remix_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_audio_remix
    test_audio_remix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/audio_remix.cpp
)
target_include_directories(test_audio_remix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
)

# settings_pref.cpp is deliberately free of <switch.h> (unlike prefs.cpp,
# which can't be linked into a host test at all -- see settings_pref.hpp's
# own comment), but does need prefs.hpp for Prefs::LanguagePref.
add_executable(test_language_and_bilinear
    test_language_and_bilinear.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/settings_pref.cpp
)
target_include_directories(test_language_and_bilinear PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
)

enable_testing()
add_test(NAME audio_remix COMMAND test_audio_remix)
add_test(NAME language_and_bilinear COMMAND test_language_and_bilinear)
