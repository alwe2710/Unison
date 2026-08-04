# Standalone, host-buildable test project for screen_choice.c's
# finlink_nds_should_show_video_on_bottom() -- NOT part of the actual NDS
# devkitARM build (see clients/nds/arm9/Makefile for that, which picks up
# screen_choice.c automatically via its CFILES wildcard). screen_choice.c
# is deliberately kept free of <nds.h> (see screen_choice.h's own comment),
# unlike the rest of this client's source/ (main.c pulls in <nds.h>/
# <dswifi9.h>/...), so it -- and this one function -- can be built and
# tested with a plain host compiler instead of needing the devkitARM
# toolchain or actual NDS/melonDS hardware.
cmake_minimum_required(VERSION 3.16)
project(finlink_nds_dual_screen_choice_test C)

if(NOT DEFINED FINLINK_CORE_DIR)
    set(FINLINK_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../core")
endif()

add_executable(test_dual_screen_choice
    test_dual_screen_choice.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/screen_choice.c
    ${FINLINK_CORE_DIR}/src/handshake.c
    ${FINLINK_CORE_DIR}/src/json.c
)
target_include_directories(test_dual_screen_choice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${FINLINK_CORE_DIR}/include
)

enable_testing()
add_test(NAME dual_screen_choice COMMAND test_dual_screen_choice)
