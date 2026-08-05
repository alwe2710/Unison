# Standalone, host-buildable test project for prefs.cpp's
# shouldShowVideoOnBottomScreen() -- NOT part of the actual 3DS
# devkitARM build (see clients/3ds/Makefile for that), same reasoning as
# Cemu/melonDS's own tests/CMakeLists.txt: prefs.cpp itself is deliberately
# kept free of 3ds.h (see prefs.hpp's own comment), so it -- and this one
# function -- can be built and tested with a plain host compiler instead of
# needing the devkitARM toolchain or actual 3DS/citra hardware.
cmake_minimum_required(VERSION 3.16)
project(unison_3ds_dual_screen_choice_test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED UNISON_CORE_DIR)
    set(UNISON_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../core")
endif()

add_executable(test_dual_screen_choice
    test_dual_screen_choice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/prefs.cpp
    ${UNISON_CORE_DIR}/src/handshake.c
    ${UNISON_CORE_DIR}/src/json.c
)
target_include_directories(test_dual_screen_choice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${UNISON_CORE_DIR}/include
)

# audio_remix.cpp is deliberately free of <3ds.h>/NDSP types (unlike the
# rest of source/audio.cpp), so it needs no unison_core at all here.
add_executable(test_audio_remix
    test_audio_remix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/audio_remix.cpp
)
target_include_directories(test_audio_remix PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
)

# language_pref.cpp is deliberately free of <3ds.h>/cfgu (unlike the rest
# of main.cpp's resolveLanguage()); links prefs.cpp for the same reason as
# test_dual_screen_choice above (needs unison_core too, prefs.cpp itself
# calls unison_stream_type_prefers_secondary_screen()).
add_executable(test_language_and_bilinear
    test_language_and_bilinear.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/language_pref.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/prefs.cpp
    ${UNISON_CORE_DIR}/src/handshake.c
    ${UNISON_CORE_DIR}/src/json.c
)
target_include_directories(test_language_and_bilinear PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${UNISON_CORE_DIR}/include
)

enable_testing()
add_test(NAME dual_screen_choice COMMAND test_dual_screen_choice)
add_test(NAME audio_remix COMMAND test_audio_remix)
add_test(NAME language_and_bilinear COMMAND test_language_and_bilinear)
