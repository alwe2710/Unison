#include "screen_choice.h"

#include "finlink/handshake.h"

bool finlink_nds_should_show_video_on_bottom(bool prefBottomScreen, const char *streamType) {
    return prefBottomScreen || finlink_stream_type_prefers_secondary_screen(streamType);
}
