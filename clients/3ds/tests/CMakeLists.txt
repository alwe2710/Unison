# Standalone, host-buildable test project for prefs.cpp's
# shouldShowVideoOnBottomScreen() -- NOT part of the actual 3DS
# devkitARM build (see clients/3ds/Makefile for that), same reasoning as
# Cemu/melonDS's own tests/CMakeLists.txt: prefs.cpp itself is deliberately
# kept free of 3ds.h (see prefs.hpp's own comment), so it -- and this one
# function -- can be built and tested with a plain host compiler instead of
# needing the devkitARM toolchain or actual 3DS/citra hardware.
cmake_minimum_required(VERSION 3.16)
project(finlink_3ds_dual_screen_choice_test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED FINLINK_CORE_DIR)
    set(FINLINK_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../core")
endif()

add_executable(test_dual_screen_choice
    test_dual_screen_choice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/prefs.cpp
    ${FINLINK_CORE_DIR}/src/handshake.c
    ${FINLINK_CORE_DIR}/src/json.c
)
target_include_directories(test_dual_screen_choice PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${FINLINK_CORE_DIR}/include
)

enable_testing()
add_test(NAME dual_screen_choice COMMAND test_dual_screen_choice)
