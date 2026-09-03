# reTerminal Sticky YouTube Firmware

Native ESP32-S3 firmware for the Seeed Studio reTerminal Sticky. It renders PB&J Squad YouTube channel totals on the 800x480 E-Ink display and is designed to be submitted as a community firmware option to the Seed Studio Sticky Playground registry.

The firmware never contains a YouTube API key. It calls a deployed HTTPS proxy endpoint, which keeps the key server-side.

This project is experimental until it has been built with ESP-IDF v5.4 and tested on physical reTerminal Sticky hardware. Flashing a community firmware replaces the currently installed firmware; use the official Sticky recovery flow to restore stock firmware.
