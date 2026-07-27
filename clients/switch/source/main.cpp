// finlink for Nintendo Switch: Menu -> Settings / Player, same structure
// as clients/android/. See clients/switch/README.md for the toolchain and
// architecture notes.

#include <borealis.hpp>
#include <switch.h>

#include "menu_activity.hpp"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // Needed before any BSD socket use (GbaSession, discovery::probeLobby
    // etc.) -- libnx doesn't bring sockets up on its own.
    socketInitializeDefault();

    // Mounts the RomFS embedded into this .nro by elf2nro --romfsdir=
    // (see CMakeLists.txt) at "romfs:/" -- borealis's Switch font loader
    // reads resources/material/MaterialIcons-Regular.ttf from there during
    // Application::init(). Without this call romfs:/ paths just fail to
    // open; was missing from the initial version of this file.
    romfsInit();

    // Also needed before Application::init(): borealis's Switch font
    // loader gets the actual regular/CJK/icon glyphs from the system's
    // shared font via plGetSharedFontByType(), which -- like romfs above --
    // silently fails on every call until pl:u is initialized. Without this
    // the app doesn't crash, it just renders with every label blank (no
    // glyphs to draw), which is exactly what going straight from
    // Application::init() into pushActivity() without this call looked
    // like.
    plInitialize(PlServiceType_User);

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        plExit();
        romfsExit();
        socketExit();
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("finlink");

    // Start (Plus) is a GBA button (see PlayerActivity), not a "quit app"
    // shortcut here -- the Android client has no such gesture either, and
    // Application::pushActivity() would otherwise bind Start to
    // Application::quit() on every single activity it pushes, including
    // the player.
    brls::Application::setGlobalQuit(false);

    brls::Application::pushActivity(new MenuActivity());

    while (brls::Application::mainLoop())
        ;

    plExit();
    romfsExit();
    socketExit();
    return EXIT_SUCCESS;
}
