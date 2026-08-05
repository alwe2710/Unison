# Standalone, host-buildable test project for screen_choice.c's
# unison_nds_should_show_video_on_bottom() -- NOT part of the actual NDS
# devkitARM build (see clients/nds/arm9/Makefile for that, which picks up
# screen_choice.c automatically via its CFILES wildcard). screen_choice.c
# is deliberately kept free of <nds.h> (see screen_choice.h's own comment),
# unlike the rest of this client's source/ (main.c pulls in <nds.h>/
# <dswifi9.h>/...), so it -- and this one function -- can be built and
# tested with a plain host compiler instead of needing the devkitARM
# toolchain or actual NDS/melonDS hardware.
cmake_minimum_required(VERSION 3.16)
project(unison_nds_dual_screen_choice_test C)

if(NOT DEFINED UNISON_CORE_DIR)
    set(UNISON_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../core")
endif()

add_executable(test_dual_screen_choice
    test_dual_screen_choice.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/screen_choice.c
    ${UNISON_CORE_DIR}/src/handshake.c
    ${UNISON_CORE_DIR}/src/json.c
)
target_include_directories(test_dual_screen_choice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${UNISON_CORE_DIR}/include
)

# audio_ring.c only needs unison_read_s16le/unison_write_u16le
# (unison/endian.h, header-only) -- no other unison_core .c file required.
add_executable(test_audio_ring
    test_audio_ring.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/audio_ring.c
)
target_include_directories(test_audio_ring PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${UNISON_CORE_DIR}/include
)

# language_pref.c only needs strings_generated.h's StrLang enum (header-only,
# no nds.h) -- no unison_core needed here.
add_executable(test_language_pref
    test_language_pref.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/language_pref.c
)
target_include_directories(test_language_pref PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
)

enable_testing()
add_test(NAME dual_screen_choice COMMAND test_dual_screen_choice)
add_test(NAME audio_ring COMMAND test_audio_ring)
add_test(NAME language_pref COMMAND test_language_pref)
