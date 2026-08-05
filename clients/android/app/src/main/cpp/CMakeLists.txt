cmake_minimum_required(VERSION 3.22.1)
project(unison_android C)

# Pull in the shared protocol/codec/transport library directly from the repo
# root instead of copying/symlinking it into the Android project -- this is
# the whole point of core/ being a plain add_subdirectory()-able CMake
# target. UNISON_BUILD_TESTS is skipped automatically here since
# CMAKE_SOURCE_DIR (this project) != core's own CMAKE_CURRENT_SOURCE_DIR.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../core ${CMAKE_CURRENT_BINARY_DIR}/unison_core)

add_library(unison_android SHARED
    jni_bridge.c
)

find_library(log-lib log)
# mediandk: NDK AMediaCodec (H.264/H265 hardware decode). android:
# ANativeWindow_fromSurface(), to hand MediaCodec's Surface-mode output the
# TextureView-backed window it renders into.
find_library(mediandk-lib mediandk)
find_library(android-lib android)

target_link_libraries(unison_android
    unison_core
    ${log-lib}
    ${mediandk-lib}
    ${android-lib}
)
