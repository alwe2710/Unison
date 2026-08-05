cmake_minimum_required(VERSION 3.13)

# Toolchain file must be included before project(): this is what picks up
# $DEVKITPRO/cmake/3DS.cmake and configures the arm-none-eabi compiler, same
# pattern as clients/switch/CMakeLists.txt for devkitA64/Switch.cmake.
if (NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE $ENV{DEVKITPRO}/cmake/3DS.cmake CACHE PATH "toolchain file")
endif ()

project(unison-3ds C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# unison_core (WS handshake/framing, app protocol, deflate) -- unchanged
# from the core used by every other client, see core/README.md.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../core ${CMAKE_BINARY_DIR}/unison_core)

file(GLOB_RECURSE UNISON_3DS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
add_executable(${PROJECT_NAME} ${UNISON_3DS_SRC})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_link_libraries(${PROJECT_NAME} PRIVATE unison_core citro2d citro3d ctru m)

# smdhtool requires an exact 48x48 PNG (unlike the Switch/Android icons,
# which accept the full-resolution logo directly) -- pre-resized once from
# assets/logo/unison-logo.png rather than depending on ImageMagick/PIL at
# build time.
set(APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.png)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CTR_SMDHTOOL_EXE} --create "Unison" "GBA stream client" "Unison" ${APP_ICON} ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.smdh
    COMMAND ${CTR_3DSXTOOL_EXE} $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.3dsx --smdh=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.smdh
    BYPRODUCTS ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.3dsx ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.smdh
)

# CIA (CFW-installable format), off by default since it needs makerom/
# bannertool -- neither packaged by devkitPro, see cia/fetch_tools.sh's own
# comment -- fetched into cia/tools/ (gitignored) on first build rather
# than assumed preinstalled, same reasoning as GBAStreamWebClient's
# build_wasm.sh fetching emsdk. Homebrew ("-target t") signing only: not
# suitable for retail/eShop distribution.
option(BUILD_3DS_CIA "Also build a CFW-installable .cia (needs network access to fetch makerom/bannertool once)" OFF)
if (BUILD_3DS_CIA)
    set(CIA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cia)
    set(CIA_TOOLS_DIR ${CIA_DIR}/tools)

    add_custom_command(
        OUTPUT ${CIA_TOOLS_DIR}/makerom ${CIA_TOOLS_DIR}/bannertool
        COMMAND ${CIA_DIR}/fetch_tools.sh ${CIA_TOOLS_DIR}
        COMMENT "Fetching makerom/bannertool"
    )

    # bannertool requires the image to be *exactly* 256x128 and an audio
    # track isn't actually optional despite its own --help text ("optional
    # if with a CWAV" turned out to mean "one of -a/-ca is required", not
    # "audio itself is optional") -- confirmed by trial against the real
    # tool, not just its help text. Pre-generated once from
    # assets/logo/unison-logo.png (banner.png) plus a trivial silent WAV
    # (banner_silent.wav), same reasoning as icon.png being pre-resized
    # rather than depending on ImageMagick/PIL at build time.
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bnr
        COMMAND ${CIA_TOOLS_DIR}/bannertool makebanner
            -i ${CIA_DIR}/banner.png
            -a ${CIA_DIR}/banner_silent.wav
            -o ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bnr
        DEPENDS ${CIA_TOOLS_DIR}/bannertool ${CIA_DIR}/banner.png ${CIA_DIR}/banner_silent.wav
        COMMENT "Building CIA banner"
    )

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.cia
        COMMAND ${CIA_TOOLS_DIR}/makerom -f cia -target t
            -o ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.cia
            -rsf ${CIA_DIR}/template.rsf
            -elf $<TARGET_FILE:${PROJECT_NAME}>
            -icon ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.smdh
            -banner ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bnr
        DEPENDS ${CIA_TOOLS_DIR}/makerom ${PROJECT_NAME}
            ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.smdh ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bnr
            ${CIA_DIR}/template.rsf
        COMMENT "Building ${PROJECT_NAME}.cia"
    )

    add_custom_target(${PROJECT_NAME}_cia ALL DEPENDS ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.cia)
endif ()
