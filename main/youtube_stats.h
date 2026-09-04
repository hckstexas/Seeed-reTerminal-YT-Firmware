#pragma once

#include <cstdint>

struct YoutubeConfig;

struct YoutubeStats {
    bool valid = false;
    uint64_t subscribers = 0;
    uint64_t views = 0;
    uint64_t videos = 0;
    char channel_title[96] = {};
};

bool fetch_youtube_stats(const YoutubeConfig &config, YoutubeStats &stats, char *error_message, uint32_t error_message_size);
