#include "youtube_view.h"

    #include <algorithm>
    #include <string>

    #include "ui/font_5x7.h"
#include "ui/olivia_bitmap.h"

    namespace {
    void fill_rect(StickyDisplay &display, int x, int y, int width, int height, bool black)
    {
      for (int row = y; row < y + height; ++row) for (int column = x; column < x + width; ++column) display.draw_pixel(column, row, black);
    }

    int text_width(const std::string &text, int scale)
    {
      return text.empty() ? 0 : static_cast<int>(text.size()) * (5 * scale + scale) - scale;
    }

    void text(StickyDisplay &display, int x, int y, const std::string &value, int scale)
    {
      int cursor = x;
      for (const char character : value) {
          const uint8_t *rows = Font5x7::rows(character);
          for (int row = 0; row < 7; ++row) for (int column = 0; column < 5; ++column) if ((rows[row] & (0x10U >> column)) != 0) fill_rect(display, cursor + column * scale, y + row * scale, scale, scale, true);
          cursor += 6 * scale;
      }
    }

    void centered(StickyDisplay &display, int y, const std::string &value, int scale)
    {
      text(display, std::max(8, (display.width() - text_width(value, scale)) / 2), y, value, scale);
    }

void centered_in_rect(StickyDisplay &display,
                      int x,
                      int width,
                      int y,
                      const std::string &value,
                      int scale)
{
  text(display, x + std::max(0, (width - text_width(value, scale)) / 2), y, value, scale);
}

void draw_olivia(StickyDisplay &display, int x, int y, int x_scale, int y_scale)
{
  for (int source_y = 0; source_y < OliviaBitmap::kHeight; ++source_y) {
    for (int source_x = 0; source_x < OliviaBitmap::kWidth; ++source_x) {
      const uint8_t source_byte =
          OliviaBitmap::kPixels[source_y * OliviaBitmap::kStride + source_x / 8];
      if ((source_byte & (0x80U >> (source_x % 8))) == 0) continue;
      fill_rect(display,
                x + source_x * x_scale,
                y + source_y * y_scale,
                x_scale,
                y_scale,
                true);
    }
  }
}

    std::string comma_number(uint64_t value)
    {
      std::string digits = std::to_string(value);
      for (int index = static_cast<int>(digits.size()) - 3; index > 0; index -= 3) digits.insert(static_cast<std::size_t>(index), ",");
      return digits;
    }
    }

    void render_youtube_screen(StickyDisplay &display, const YoutubeStats &stats, const char *status_message)
    {
      display.clear(true);
      display.draw_rect(7, 7, display.width() - 14, display.height() - 14, true);

      if (is_portrait_orientation(display.orientation())) {
          const int hero_top = 400;
          draw_olivia(display, 60, hero_top + 15, 3, 3);
          fill_rect(display, 16, hero_top, display.width() - 32, 3, true);
          centered(display, 22, "PB and J SQUAD", 2);
          centered(display,
                   52,
                   stats.valid && stats.channel_title[0] != '\0' ? stats.channel_title : "YOUTUBE",
                   1);
          fill_rect(display, 35, 76, display.width() - 70, 3, true);

          if (!stats.valid) {
              centered(display, 130, "WAITING FOR DATA", 2);
              centered(display, 185, status_message == nullptr ? "CHECK CONFIGURATION" : status_message, 1);
              centered(display, 235, "PRESS BUTTON TO RETRY", 1);
          } else {
              centered(display, 105, "SUBSCRIBERS", 2);
              centered(display, 135, comma_number(stats.subscribers), 4);
              centered(display, 215, "VIEWS", 2);
              centered(display, 245, comma_number(stats.views), 3);
              centered(display, 300, "VIDEOS", 2);
              centered(display, 330, comma_number(stats.videos), 3);
              centered(display, 370, "TAP OR PRESS TO REFRESH", 1);
          }
          return;
      }

      const int hero_width = display.width() / 2;
      const int right_x = hero_width + 16;
      const int right_width = display.width() - right_x - 16;
      draw_olivia(display, 20, 55, 3, 3);
      fill_rect(display, hero_width, 20, 3, display.height() - 40, true);
       centered_in_rect(display, right_x, right_width, 22, "PB and J SQUAD", 2);
      centered_in_rect(display,
                       right_x,
                       right_width,
                       51,
                       stats.valid && stats.channel_title[0] != '\0' ? stats.channel_title : "YOUTUBE CHANNEL",
                       1);
      fill_rect(display, right_x, 70, right_width, 3, true);

      if (!stats.valid) {
          centered_in_rect(display, right_x, right_width, 175, "WAITING FOR DATA", 2);
          centered_in_rect(display, right_x, right_width, 225, status_message == nullptr ? "CHECK CONFIGURATION" : status_message, 1);
          centered_in_rect(display, right_x, right_width, 275, "PRESS BUTTON TO RETRY", 1);
          return;
      }

      centered_in_rect(display, right_x, right_width, 88, "SUBSCRIBERS", 2);
      centered_in_rect(display, right_x, right_width, 116, comma_number(stats.subscribers), 5);
      fill_rect(display, right_x, 190, right_width, 2, true);
      centered_in_rect(display, right_x, right_width, 207, "VIEWS", 2);
      centered_in_rect(display, right_x, right_width, 237, comma_number(stats.views), 4);
      fill_rect(display, right_x, 315, right_width, 2, true);
      centered_in_rect(display, right_x, right_width, 332, "VIDEOS", 2);
      centered_in_rect(display, right_x, right_width, 362, comma_number(stats.videos), 4);
      centered_in_rect(display, right_x, right_width, 435, "AUTO REFRESH 5 MIN", 1);
    }
    