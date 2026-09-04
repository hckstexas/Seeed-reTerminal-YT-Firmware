# reTerminal Sticky YouTube Firmware

This repository contains a native ESP-IDF firmware port for the Seeed Studio reTerminal Sticky. It renders PB&J Squad channel totals on the 800 x 480 SSD1677 E-Ink panel using the Sticky's ESP32-S3 display, GT911 touch, and button hardware.

## Standalone data flow

The firmware calls the YouTube Data API directly over HTTPS. It does not require the companion web app, a Replit workflow, or an always-running server. On first boot, the firmware starts a setup access point where you enter Wi-Fi credentials, a YouTube Data API key, and the channel ID or @handle. These values are stored in NVS on the device.

The API key is intentionally not present in this public repository or in a generic release image. Because the key is stored on the device for direct API access, it can be extracted from the device by someone with physical access. Restrict the key to the YouTube Data API, set a quota, and use a dedicated key for this device.

## Build and flash

Use ESP-IDF v5.4 or newer with the ESP32-S3 target. On the first boot, the firmware starts a setup access point named Sticky-YT with password stickysetup. Connect a phone or laptop to that network and open http://192.168.4.1. Enter the normal Wi-Fi credentials, the YouTube Data API key, and the channel ID or @handle; the values are stored in NVS and the Sticky restarts.

For developer builds, ESP-IDF v5.4 or newer can also seed Wi-Fi, the API key, and channel through idf.py menuconfig under the YouTube Sticky menu. Do not commit a personal key to sdkconfig or source control. Run idf.py set-target esp32s3, then idf.py build and idf.py -p PORT flash monitor.

The first refresh occurs at boot. A successful refresh is repeated every five minutes. A short press on the power/OK button or a touchscreen tap requests an immediate refresh. The launcher enables the Up and Down buttons on GPIO5 and GPIO6 and provides local Clock, Environment, Power, and Motion pages. Clock reads the PCF8563 RTC, Environment reads the SHT40, Power reads the BQ27220 and charger-state pins, and Motion reads the LSM6DS3TR-C. A long press on the grey button returns to Home, touch swipes move between pages, and supported navigation actions provide buzzer feedback.

The display renderer supports both the native 800 x 480 landscape canvas and a rotated 480 x 800 portrait canvas. The LSM6DS3TR-C on the sensor bus is used to detect stable device orientation: accelerometer tilt selects the orientation and gyro motion is used to wait for the device to settle before rotating the display. This orientation path is experimental until it is verified on physical hardware.

The display uses a one-bit framebuffer so it is safe for the panel's native black-and-white mode; all screens must use the display's logical width and height rather than assuming landscape dimensions.

## Safety and recovery

Flashing this project replaces the installed firmware. It has not been certified against every stock firmware build or physically validated until it is flashed to a real Sticky. Keep the official stock recovery image available before testing.

## Registry status

The Seed Studio registry contribution template is in registry/firmware.json. The registry entry should point at this repository's source directory and use a flashable release artifact after a physical hardware test.
