#pragma once

#include <cstdint>

#include "hardware/sticky_display.h"
#include "hardware/sticky_local_peripherals.h"

enum class StickyScreen : uint8_t {
    Home = 0,
    YouTube,
    Clock,
    Environment,
    Power,
    Motion,
};

constexpr int kStickyScreenCount = 6;

StickyScreen previous_screen(StickyScreen screen);
StickyScreen next_screen(StickyScreen screen);
const char *screen_title(StickyScreen screen);

void render_home_screen(StickyDisplay &display, StickyScreen selected_screen);
void render_local_screen(StickyDisplay &display,
                         StickyScreen screen,
                         const StickyLocalData &data);