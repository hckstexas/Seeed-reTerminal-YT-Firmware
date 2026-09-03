#include "youtube_view.h"

    #include <algorithm>
    #include <string>

    #include "ui/font_5x7.h"

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
      text(display, std::max(8, (StickyDisplay::kWidth - text_width(value, scale)) / 2), y, value, scale);
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
      display.draw_rect(7, 7, 786, 466, true);
      text(display, 25, 23, "PB J SQUAD", 3);
      text(display, 28, 61, stats.valid && stats.channel_title[0] != '\0' ? stats.channel_title : "YOUTUBE CHANNEL", 2);
      fill_rect(display, 25, 86, 750, 3, true);

      if (!stats.valid) {
          centered(display, 150, "WAITING FOR DATA", 3);
          centered(display, 215, status_message == nullptr ? "CHECK CONFIGURATION" : status_message, 2);
          centered(display, 280, "PRESS BUTTON TO RETRY", 2);
          return;
      }

      centered(display, 112, "SUBSCRIBERS", 2);
      centered(display, 146, comma_number(stats.subscribers), 5);
      fill_rect(display, 42, 235, 716, 2, true);
      text(display, 67, 270, "VIEWS", 2);
      text(display, 67, 310, comma_number(stats.views), 3);
      text(display, 450, 270, "VIDEOS", 2);
      text(display, 450, 310, comma_number(stats.videos), 3);
      fill_rect(display, 42, 367, 716, 2, true);
      centered(display, 405, "AUTO REFRESH 5 MIN", 2);
      centered(display, 440, "TAP OR PRESS BUTTON TO REFRESH", 1);
    }
    