#include "navigation_view.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "ui/font_5x7.h"

namespace {
void fill_rect(StickyDisplay &display, int x, int y, int width, int height, bool black)
{
    for (int row = y; row < y + height; ++row) {
        for (int column = x; column < x + width; ++column) {
            display.draw_pixel(column, row, black);
        }
    }
}

int text_width(const std::string &value, int scale)
{
    return value.empty() ? 0 : static_cast<int>(value.size()) * (6 * scale) - scale;
}

void text(StickyDisplay &display, int x, int y, const std::string &value, int scale)
{
    int cursor = x;
    for (const char character : value) {
        const uint8_t *rows = Font5x7::rows(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((rows[row] & (0x10U >> column)) != 0) {
                    fill_rect(display,
                              cursor + column * scale,
                              y + row * scale,
                              scale,
                              scale,
                              true);
                }
            }
        }
        cursor += 6 * scale;
    }
}

void centered(StickyDisplay &display, int y, const std::string &value, int scale)
{
    text(display, std::max(8, (display.width() - text_width(value, scale)) / 2), y, value, scale);
}

int screen_index(StickyScreen screen)
{
    return static_cast<int>(screen);
}
}

StickyScreen previous_screen(StickyScreen screen)
{
    const int index = screen_index(screen);
    return static_cast<StickyScreen>((index + kStickyScreenCount - 1) % kStickyScreenCount);
}

StickyScreen next_screen(StickyScreen screen)
{
    return static_cast<StickyScreen>((screen_index(screen) + 1) % kStickyScreenCount);
}

const char *screen_title(StickyScreen screen)
{
    switch (screen) {
    case StickyScreen::Home: return "HOME";
    case StickyScreen::YouTube: return "YOUTUBE";
    case StickyScreen::Clock: return "CLOCK";
    case StickyScreen::Environment: return "ENVIRONMENT";
    case StickyScreen::Power: return "POWER";
    case StickyScreen::Motion: return "MOTION";
    }
    return "STICKY";
}

void render_home_screen(StickyDisplay &display, StickyScreen selected_screen)
{
    display.clear(true);
    display.draw_rect(7, 7, display.width() - 14, display.height() - 14, true);
    const int margin = display.width() >= 700 ? 28 : 18;
    text(display, margin, 25, "STICKY HOME", 3);
    text(display, margin, 64, "SELECT A SCREEN", 2);
    fill_rect(display, margin - 3, 96, display.width() - (margin * 2) + 6, 3, true);

    const StickyScreen choices[] = {
        StickyScreen::YouTube,
        StickyScreen::Clock,
        StickyScreen::Environment,
        StickyScreen::Power,
        StickyScreen::Motion,
    };
    const int row_step = display.height() >= 700 ? 110 : 54;
    const int first_row = display.height() >= 700 ? 112 : 130;
    const int selection_width = display.width() - (margin * 2);
    for (int index = 0; index < 5; ++index) {
        const int y = first_row + index * row_step;
        const bool selected = choices[index] == selected_screen;
        if (selected) {
            display.draw_rect(margin, y - 8, selection_width, 43, true);
            text(display, margin + 20, y, ">", 3);
        }
        text(display, margin + 54, y, screen_title(choices[index]), 3);
    }

    centered(display, display.height() - 34, "UP/DOWN MOVE  GREY SELECTS", 1);
}

void render_local_screen(StickyDisplay &display,
                         StickyScreen screen,
                         const StickyLocalData &data)
{
    display.clear(true);
    display.draw_rect(7, 7, display.width() - 14, display.height() - 14, true);
    centered(display, 25, screen_title(screen), 3);
    fill_rect(display, 35, 72, display.width() - 70, 3, true);

    char line[80] = {};
    switch (screen) {
    case StickyScreen::Clock:
        if (!data.rtc_valid) {
            centered(display, 170, "RTC NOT AVAILABLE", 2);
            centered(display, 220, "CHECK SENSOR BUS", 1);
        } else {
            std::snprintf(line, sizeof(line), "%02d:%02d", data.hour, data.minute);
            centered(display, display.height() >= 700 ? 230 : 165, line, 6);
            std::snprintf(line, sizeof(line), "%04d-%02d-%02d",
                          data.year, data.month, data.day);
            centered(display, display.height() >= 700 ? 330 : 275, line, 2);
        }
        break;
    case StickyScreen::Environment:
        if (!data.environment_valid) {
            centered(display, 170, "SHT40 NOT AVAILABLE", 2);
        } else {
            std::snprintf(line, sizeof(line), "TEMP %.1f C", data.temperature_c);
            centered(display, display.height() >= 700 ? 235 : 165, line, 3);
            std::snprintf(line, sizeof(line), "HUMIDITY %.1f %%", data.humidity_percent);
            centered(display, display.height() >= 700 ? 335 : 265, line, 2);
        }
        break;
    case StickyScreen::Power:
        if (!data.power_valid) {
            centered(display, 170, "FUEL GAUGE UNAVAILABLE", 2);
        } else {
            std::snprintf(line, sizeof(line), "VOLTAGE %u MV", data.voltage_mv);
            centered(display, display.height() >= 700 ? 210 : 145, line, 2);
            std::snprintf(line, sizeof(line), "CHARGE %u %%", data.state_of_charge);
            centered(display, display.height() >= 700 ? 290 : 215, line, 3);
            std::snprintf(line, sizeof(line), "%s %s",
                          data.charging ? "CHARGING" : "ON BATTERY",
                          data.external_power ? "USB" : "");
            centered(display, display.height() >= 700 ? 385 : 285, line, 2);
        }
        break;
    case StickyScreen::Motion:
        if (!data.motion_valid) {
            centered(display, 170, "IMU NOT AVAILABLE", 2);
        } else {
            std::snprintf(line, sizeof(line), "AX %d  AY %d",
                          data.motion.accel_x, data.motion.accel_y);
            centered(display, display.height() >= 700 ? 205 : 145, line, 2);
            std::snprintf(line, sizeof(line), "AZ %d", data.motion.accel_z);
            centered(display, display.height() >= 700 ? 275 : 205, line, 2);
            std::snprintf(line, sizeof(line), "GX %d  GY %d  GZ %d",
                          data.motion.gyro_x, data.motion.gyro_y, data.motion.gyro_z);
            centered(display, display.height() >= 700 ? 355 : 275, line, 1);
        }
        break;
    default:
        centered(display, 170, "LOCAL DATA UNAVAILABLE", 2);
        break;
    }

    centered(display, display.height() - 60, "UP/DOWN CHANGE SCREEN", 1);
    centered(display, display.height() - 35, "LONG PRESS GREY FOR HOME", 1);
}