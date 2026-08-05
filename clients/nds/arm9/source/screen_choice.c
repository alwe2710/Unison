#include "screen_choice.h"

#include "unison/handshake.h"

bool unison_nds_should_show_video_on_bottom(bool prefBottomScreen, const char *streamType) {
    return prefBottomScreen || unison_stream_type_prefers_secondary_screen(streamType);
}
