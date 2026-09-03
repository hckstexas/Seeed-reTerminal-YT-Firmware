#pragma once

    #include "hardware/sticky_display.h"
    #include "youtube_stats.h"

    void render_youtube_screen(StickyDisplay &display, const YoutubeStats &stats, const char *status_message);
    