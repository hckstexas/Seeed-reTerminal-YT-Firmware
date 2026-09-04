#pragma once

#include <cstdint>

struct YoutubeConfig {
    char wifi_ssid[33] = {};
    char wifi_password[65] = {};
    char api_key[129] = {};
    char channel[97] = {};

    bool ready() const;
};

bool load_youtube_config(YoutubeConfig &config);
bool save_youtube_config(const YoutubeConfig &config);
bool start_youtube_provisioning();
