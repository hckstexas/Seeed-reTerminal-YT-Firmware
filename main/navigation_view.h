#pragma once

#include <cstdint>

#include "hardware/sticky_display.h"
#include "hardware/sticky_orientation.h"
#include "hardware/sticky_sensors.h"

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
void render_clock_screen(StickyDisplay &display, const StickyClockReading &reading);
void render_environment_screen(StickyDisplay &display, const StickyEnvironmentReading &reading);
void render_power_screen(StickyDisplay &display, const StickyPowerReading &reading);
void render_motion_screen(StickyDisplay &display, const StickyMotionReading &reading);