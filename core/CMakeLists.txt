cmake_minimum_required(VERSION 3.13)
project(unison_core C)

option(UNISON_BUILD_TESTS "Build unison_core unit tests" ON)

add_library(unison_core STATIC
    src/protocol.c
    src/inflate.c
    src/deflate.c
    src/websocket.c
    src/json.c
    src/handshake.c
    src/discovery.c
    src/video_encode.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_crc32.c
    third_party/teeny-sha1/teeny-sha1.c
)

target_include_directories(unison_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        third_party/miniz
        third_party/teeny-sha1
)

target_compile_features(unison_core PUBLIC c_std_99)

if (UNISON_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif ()
