#pragma once

    #include <cstdint>

    #include "driver/spi_master.h"
    #include "epaper_panel.h"

    enum class DisplayOrientation {
      Landscape,
      Portrait,
  LandscapeInverted,
  PortraitInverted,
    };

constexpr bool is_portrait_orientation(DisplayOrientation orientation)
{
  return orientation == DisplayOrientation::Portrait ||
         orientation == DisplayOrientation::PortraitInverted;
}

    class StickyDisplay {
    public:
      static constexpr int kWidth = 800;
      static constexpr int kHeight = 480;

      bool init();
      void set_orientation(DisplayOrientation orientation);
      DisplayOrientation orientation() const { return orientation_; }
      int width() const { return is_portrait_orientation(orientation_) ? kHeight : kWidth; }
      int height() const { return is_portrait_orientation(orientation_) ? kWidth : kHeight; }
      void clear(bool white = true);
      void draw_pixel(int x, int y, bool black = true);
      void draw_rect(int x, int y, int width, int height, bool black = true);
      bool refresh_full();
      bool refresh_partial();

    private:
      bool refresh(seeed_epaper_refresh_mode_t mode);

      static constexpr int kStrideBytes = kWidth / 8;
      static constexpr int kBufferSize = kStrideBytes * kHeight;

      spi_device_handle_t spi_device_ = nullptr;
      seeed_epaper_panel_handle_t panel_ = nullptr;
      uint8_t *framebuffer_ = nullptr;
      DisplayOrientation orientation_ = DisplayOrientation::Landscape;
    };
    