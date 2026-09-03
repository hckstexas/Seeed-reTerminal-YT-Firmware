# reTerminal Sticky YouTube Firmware

    This repository contains a native ESP-IDF firmware port for the Seeed Studio reTerminal Sticky. It renders PB J Squad channel totals on the 800 x 480 SSD1677 E-Ink panel using the Sticky's ESP32-S3 display, GT911 touch, and button hardware.

    ## Data flow

    The firmware calls an HTTPS endpoint configured as CONFIG_YOUTUBE_API_BASE_URL. That endpoint should be the deployed API server route /api/youtube/stats from the companion YouTube Sticky Display app. The YouTube API key remains server-side and is never compiled into this firmware.

    ## Build and flash

    Use ESP-IDF v5.4 or newer with the ESP32-S3 target. Run idf.py set-target esp32s3, idf.py menuconfig, and configure the YouTube Sticky menu with Wi-Fi SSID, Wi-Fi password, the deployed HTTPS stats endpoint, and the channel ID or @handle. Then run idf.py build and idf.py -p PORT flash monitor.

    The first refresh occurs at boot. A successful refresh is repeated every five minutes. A short press on the power/OK button or a touchscreen tap requests an immediate refresh. The display uses a one-bit framebuffer so it is safe for the panel's native black-and-white mode; the visual layout is constrained to 800 x 480.

    ## Safety and recovery

    Flashing this project replaces the installed firmware. It has not been certified against every stock firmware build or physically validated until it is flashed to a real Sticky. Keep the official stock recovery image available before testing.

    ## Registry status

    The Seed Studio registry contribution template is in registry/firmware.json. The registry entry should point at this repository's source directory and use a flashable release artifact after a physical hardware test.
    