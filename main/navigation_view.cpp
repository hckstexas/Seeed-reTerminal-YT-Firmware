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

void frame(StickyDisplay &display, const char *title)
{
    display.clear(true);
    display.draw_rect(7, 7, display.width() - 14, display.height() - 14, true);
    centered(display, 25, title, display.width() < 600 ? 3 : 4);
    fill_rect(display, 25, 78, display.width() - 50, 3, true);
}

std::string signed_number(int16_t value)
{
    char buffer[12] = {};
    std::snprintf(buffer, sizeof(buffer), "%+d", static_cast<int>(value));
    return buffer;
}
}

StickyScreen previous_screen(StickyScreen screen)
{
    const int index = static_cast<int>(screen);
    return static_cast<StickyScreen>((index + kStickyScreenCount - 1) % kStickyScreenCount);
}

StickyScreen next_screen(StickyScreen screen)
{
    const int index = static_cast<int>(screen);
    return static_cast<StickyScreen>((index + 1) % kStickyScreenCount);
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
    text(display, 28, 25, "STICKY HOME", display.width() < 600 ? 2 : 3);
    text(display, 28, 64, "SELECT A SCREEN", 2);
    fill_rect(display, 25, 96, display.width() - 50, 3, true);

    const StickyScreen choices[] = {
        StickyScreen::YouTube,
        StickyScreen::Clock,
        StickyScreen::Environment,
        StickyScreen::Power,
        StickyScreen::Motion,
    };
    const int choice_width = display.width() - 50;
    for (int index = 0; index < 5; ++index) {
        const int y = 130 + index * 54;
        const bool selected = choices[index] == selected_screen;
        if (selected) {
            display.draw_rect(25, y - 8, choice_width, 43, true);
            text(display, 42, y, ">", 3);
        }
        text(display, 74, y, screen_title(choices[index]), display.width() < 600 ? 2 : 3);
    }

    centered(display, display.height() - 55, "UP/DOWN MOVE  GREY SELECTS", 1);
}

void render_clock_screen(StickyDisplay &display, const StickyClockReading &reading)
{
    frame(display, "CLOCK");
    if (!reading.valid) {
        centered(display, 170, "RTC UNAVAILABLE", 3);
        centered(display, 235, "CHECK RTC BATTERY", 2);
        centered(display, display.height() - 55, "UP/DOWN CHANGE SCREEN", 1);
        return;
    }

    char time_value[16] = {};
    char date_value[24] = {};
    std::snprintf(time_value,
                  sizeof(time_value),
                  "%02u:%02u:%02u",
                  reading.hour,
                  reading.minute,
                  reading.second);
    std::snprintf(date_value,
                  sizeof(date_value),
                  "%04u-%02u-%02u",
                  reading.year,
                  reading.month,
                  reading.day);

    const int time_scale = display.width() < 600 ? 5 : 8;
    centered(display, display.height() < 600 ? 145 : 155, time_value, time_scale);
    centered(display, display.height() < 600 ? 225 : 270, date_value, 3);
    centered(display, display.height() - 55, "PCF8563 RTC", 2);
}

void render_environment_screen(StickyDisplay &display, const StickyEnvironmentReading &reading)
{
    frame(display, "ENVIRONMENT");
    if (!reading.valid) {
        centered(display, 170, "SHT40 UNAVAILABLE", 2);
    } else {
        char temperature[32] = {};
        char humidity[32] = {};
        std::snprintf(temperature, sizeof(temperature), "TEMP %5.1f C", reading.temperature_c);
        std::snprintf(humidity, sizeof(humidity), "HUMIDITY %5.1f %%", reading.humidity_percent);
        centered(display, display.width() < 600 ? 165 : 155, temperature, display.width() < 600 ? 2 : 3);
        centered(display, display.width() < 600 ? 245 : 255, humidity, display.width() < 600 ? 2 : 3);
    }
    centered(display, display.height() - 55, "SHT40  UP/DOWN CHANGE", 1);
}

void render_power_screen(StickyDisplay &display, const StickyPowerReading &reading)
{
    frame(display, "POWER");
    const int value_scale = display.width() < 600 ? 4 : 6;
    if (reading.fuel_gauge_available) {
        char battery[16] = {};
        char voltage[24] = {};
        std::snprintf(battery, sizeof(battery), "%u%%", reading.battery_percent);
        std::snprintf(voltage, sizeof(voltage), "%u MV", reading.voltage_mv);
        centered(display, display.width() < 600 ? 135 : 125, "BATTERY", 2);
        centered(display, display.width() < 600 ? 175 : 165, battery, value_scale);
        centered(display, display.width() < 600 ? 255 : 255, voltage, 3);
        char current[24] = {};
        std::snprintf(current, sizeof(current), "CURRENT %d MA", static_cast<int>(reading.current_ma));
        centered(display, display.width() < 600 ? 315 : 325, current, 2);
    } else {
        centered(display, 175, "BATTERY GAUGE OFFLINE", 2);
    }

    const char *power_state = reading.external_power
                                  ? (reading.charging ? "USB CHARGING" : "USB POWER")
                                  : "BATTERY POWER";
    centered(display, display.height() - 100, power_state, 2);
    centered(display, display.height() - 55, "BQ27220  UP/DOWN CHANGE", 1);
}

void render_motion_screen(StickyDisplay &display, const StickyMotionReading &reading)
{
    frame(display, "MOTION");
    if (!reading.valid) {
        centered(display, 175, "LSM6DS3 UNAVAILABLE", 2);
    } else {
        char line[40] = {};
        const int scale = display.width() < 600 ? 2 : 3;
        std::snprintf(line,
                      sizeof(line),
                      "GYRO %s %s %s",
                      signed_number(reading.gyro_x).c_str(),
                      signed_number(reading.gyro_y).c_str(),
                      signed_number(reading.gyro_z).c_str());
        centered(display, display.width() < 600 ? 135 : 130, line, scale);
        std::snprintf(line,
                      sizeof(line),
                      "ACCEL %s %s %s",
                      signed_number(reading.accel_x).c_str(),
                      signed_number(reading.accel_y).c_str(),
                      signed_number(reading.accel_z).c_str());
        centered(display, display.width() < 600 ? 205 : 200, line, scale);
    }

    centered(display,
             display.width() < 600 ? 330 : 330,
             display.orientation() == DisplayOrientation::Portrait ? "ORIENTATION PORTRAIT"
                                                                   : "ORIENTATION LANDSCAPE",
             2);
    centered(display, display.height() - 55, "RAW GYRO/ACCEL  UP/DOWN", 1);
}