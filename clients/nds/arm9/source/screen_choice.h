#ifndef FINLINK_NDS_SCREEN_CHOICE_H
#define FINLINK_NDS_SCREEN_CHOICE_H

#include <stdbool.h>

/* The screen-choice decision main.c's session loop consults right before
 * lcdMainOnBottom()/lcdMainOnTop() -- pulled out of the inline
 * `g_prefBottomScreen || finlink_stream_type_prefers_secondary_screen(...)`
 * expression it used to be, into its own plain-C, nds.h-free translation
 * unit (unlike main.c, which pulls in <nds.h>/<dswifi9.h>/... and can only
 * be built with devkitARM) so it has one place to unit-test on a plain host
 * compiler -- see tests/test_dual_screen_choice.c.
 *
 * prefBottomScreen is the user's own top/bottom choice (main.c's
 * g_prefBottomScreen) -- only actually consulted for a single-screen
 * stream_type; a dual-screen source's own secondary screen always forces
 * bottom regardless, see finlink_stream_type_prefers_secondary_screen()
 * (finlink/handshake.h), which this calls through to. */
bool finlink_nds_should_show_video_on_bottom(bool prefBottomScreen, const char *streamType);

#endif /* FINLINK_NDS_SCREEN_CHOICE_H */
