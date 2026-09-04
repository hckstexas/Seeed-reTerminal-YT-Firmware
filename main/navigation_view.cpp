#include "navigation_view.h"

#include <algorithm>
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
    text(display, 28, 25, "STICKY HOME", 3);
    text(display, 28, 64, "SELECT A SCREEN", 2);
    fill_rect(display, 25, 96, display.width() - 50, 3, true);

    const StickyScreen choices[] = {
        StickyScreen::YouTube,
        StickyScreen::Clock,
        StickyScreen::Environment,
        StickyScreen::Power,
        StickyScreen::Motion,
    };
    for (int index = 0; index < 5; ++index) {
        const int y = 130 + index * 54;
        const bool selected = choices[index] == selected_screen;
        if (selected) {
            display.draw_rect(28, y - 8, 744, 43, true);
            text(display, 50, y, ">", 3);
        }
        text(display, 82, y, screen_title(choices[index]), 3);
    }

    centered(display, 425, "UP/DOWN MOVE  GREY SELECTS", 1);
}

void render_placeholder_screen(StickyDisplay &display, StickyScreen screen)
{
    display.clear(true);
    display.draw_rect(7, 7, display.width() - 14, display.height() - 14, true);
    centered(display, 35, screen_title(screen), 4);
    fill_rect(display, 50, 95, display.width() - 100, 3, true);
    centered(display, 155, "COMING NEXT", 4);
    centered(display, 235, "THIS SCREEN IS RESERVED", 2);
    centered(display, 275, "FOR THE ORIGINAL STICKY FEATURES", 2);
    centered(display, 365, "UP/DOWN CHANGE SCREEN", 2);
    centered(display, 405, "LONG PRESS GREY FOR HOME", 2);
}