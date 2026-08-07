cmake_minimum_required(VERSION 3.13)

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/borealis)

# Options + toolchain selection must happen before project(): this is what
# picks up $DEVKITPRO/cmake/Switch.cmake and configures the aarch64 compiler,
# following borealis's own root CMakeLists.txt pattern (see clients/switch/
# README.md for why this project vendors borealis/ as plain source rather
# than a submodule, and only the pieces needed for the Switch+glfw driver).
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(unison-switch C CXX)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_ALTER "0")
set(PROJECT_AUTHOR "Unison")
# Arbitrary but fixed placeholder: this never gets published/registered
# anywhere, it only needs to be a stable 16-hex-digit id for a sideloaded
# homebrew .nro to have a distinct save/config folder.
set(PROJECT_TITLEID "0100FIN0A0000000")
string(REGEX REPLACE "[^0-9A-Fa-f]" "0" PROJECT_TITLEID "${PROJECT_TITLEID}")
# elf2nro/hbmenu expect a 256x256 JPEG specifically, not the arbitrary PNG
# the other clients' icons accept -- pre-converted once from
# assets/logo/unison-logo.png rather than depending on ImageMagick/PIL at
# build time (same rationale as clients/3ds/CMakeLists.txt's icon.png).
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

if (NOT PLATFORM_SWITCH)
    message(FATAL_ERROR "This project only builds for the Switch: pass -DPLATFORM_SWITCH=ON.")
endif ()

add_subdirectory(${BOREALIS_LIBRARY} ${CMAKE_BINARY_DIR}/borealis)

# borealis's own toolchain.cmake only sets up i18n string loading (used for
# its built-in "hints/ok"/"hints/back"/... bottom-bar labels) for
# Desktop/iOS/Android -- for PLATFORM_SWITCH, BRLS_RESOURCES never gets a
# real value, so those lookups always miss and fall back to showing the
# raw key. Point it at our already-working romfs mount (romfsInit() in
# main.cpp, same "romfs:/" prefix switch_font.cpp already uses
# successfully for the material icon font) instead, with the actual
# strings in resources/i18n/.
target_compile_definitions(borealis PUBLIC BRLS_RESOURCES="romfs:/")

# unison_core (WS handshake/framing, app protocol, deflate) -- unchanged
# from the core used by every other client, see core/README.md.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../core ${CMAKE_BINARY_DIR}/unison_core)

find_package(Threads REQUIRED)

file(GLOB_RECURSE UNISON_SWITCH_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
program_target(${PROJECT_NAME} "${UNISON_SWITCH_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source)
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections)

# h264_decoder.cpp's h264/h265 software decode path -- switch-ffmpeg (a
# normal devkitPro pacman package: pacman -S switch-ffmpeg), found the same
# pkg-config way every other devkitPro portlib is; the toolchain's own
# Switch.cmake already points PKG_CONFIG_PATH at the portlibs prefix these
# .pc files live under.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)

target_link_libraries(${PROJECT_NAME} PRIVATE borealis unison_core Threads::Threads PkgConfig::FFMPEG)

add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
    COMMAND ${NX_NACPTOOL_EXE} --create "Unison" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp --titleid=${PROJECT_TITLEID}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
    COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
)

# NSP (CFW-installable format), off by default -- see nsp/README.md for
# why this needs SWITCH_KEYS_FILE explicitly pointed at your own keyset
# rather than anything this repo bundles or fetches. hacbrewpack itself
# (the packer, no key material) is fetched and built from source on first
# build by nsp/build_hacbrewpack.sh -- see that script's own comment for
# why source rather than a prebuilt binary, unlike this project's other
# fetched tools.
option(BUILD_SWITCH_NSP "Also build a CFW-installable .nsp (needs -DSWITCH_KEYS_FILE=/path/to/prod.keys)" OFF)
if (BUILD_SWITCH_NSP)
    if (NOT SWITCH_KEYS_FILE)
        message(FATAL_ERROR "BUILD_SWITCH_NSP needs -DSWITCH_KEYS_FILE=/path/to/your/prod.keys -- see clients/switch/nsp/README.md.")
    endif ()
    if (NOT EXISTS ${SWITCH_KEYS_FILE})
        message(FATAL_ERROR "SWITCH_KEYS_FILE (${SWITCH_KEYS_FILE}) does not exist.")
    endif ()

    set(NSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/nsp)
    set(NSP_TOOLS_DIR ${NSP_DIR}/tools)
    set(NSP_STAGE_DIR ${CMAKE_BINARY_DIR}/nsp_stage)

    configure_file(${NSP_DIR}/npdm.json.in ${CMAKE_BINARY_DIR}/${PROJECT_NAME}_npdm.json @ONLY)

    add_custom_command(
        OUTPUT ${NSP_TOOLS_DIR}/hacbrewpack
        COMMAND ${NSP_DIR}/build_hacbrewpack.sh ${NSP_TOOLS_DIR}
        COMMENT "Fetching/building hacbrewpack"
    )

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.nso
        COMMAND ${NX_ELF2NSO_EXE} ${PROJECT_NAME}.elf ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.nso
        DEPENDS ${PROJECT_NAME}
        COMMENT "Building NSO"
    )
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.npdm
        COMMAND ${NX_NPDMTOOL_EXE} ${CMAKE_BINARY_DIR}/${PROJECT_NAME}_npdm.json ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.npdm
        DEPENDS ${CMAKE_BINARY_DIR}/${PROJECT_NAME}_npdm.json
        COMMENT "Building NPDM"
    )

    # hacbrewpack's expected input layout (README.md: exefs/{main,main.npdm},
    # control/control.nacp, control/icon_AmericanEnglish.dat -- the latter
    # is just PROJECT_ICON renamed, per that same README).
    add_custom_command(
        OUTPUT ${NSP_STAGE_DIR}/.stamp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${NSP_STAGE_DIR}/exefs ${NSP_STAGE_DIR}/control
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.nso ${NSP_STAGE_DIR}/exefs/main
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.npdm ${NSP_STAGE_DIR}/exefs/main.npdm
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.nacp ${NSP_STAGE_DIR}/control/control.nacp
        COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_ICON} ${NSP_STAGE_DIR}/control/icon_AmericanEnglish.dat
        COMMAND ${CMAKE_COMMAND} -E touch ${NSP_STAGE_DIR}/.stamp
        DEPENDS ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.nso ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.npdm ${PROJECT_NAME}.nro
        COMMENT "Staging NSP input layout"
    )

    # hacbrewpack defaults to looking for a "romfs" directory in the cwd and
    # hard-fails ("Failed to open directory romfs!") if --romfsdir isn't
    # passed explicitly -- point it at the same resources/ dir the .nro
    # build already populates via copy_directory above (PROJECT_RESOURCES),
    # so the installed NSP has the same fonts/i18n/icons as the .nro.
    add_custom_target(${PROJECT_NAME}_nsp ALL
        COMMAND ${NSP_TOOLS_DIR}/hacbrewpack
            --keyset ${SWITCH_KEYS_FILE}
            --exefsdir ${NSP_STAGE_DIR}/exefs
            --controldir ${NSP_STAGE_DIR}/control
            --romfsdir ${CMAKE_BINARY_DIR}/resources
            --nologo
            --nspdir ${CMAKE_BINARY_DIR}/nsp_out
        DEPENDS ${NSP_TOOLS_DIR}/hacbrewpack ${NSP_STAGE_DIR}/.stamp
        COMMENT "Packing ${PROJECT_NAME}.nsp"
    )
endif ()
