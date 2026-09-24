| Supported Targets | ESP32 | ESP32-C3 | ESP32-C6 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- |

# Heaptop basic example

Interactive serial console (based on ESP-IDF's `system/console/advanced` example) with heaptop's `ht` commands registered, plus `stress` commands that create leaks, fragmentation, CPU load and allocation failures so every heaptop view has something to show.

## Hardware

Tested on an ESP32-S3 N16R8 (16MB flash, 8MB octal PSRAM) with the console on the UART port. `sdkconfig.defaults.esp32s3` holds the board settings; other targets build with 4MB flash and no PSRAM.

## Build and flash

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Type `help` for all commands, `ht` for the heaptop ones.

## Configuration

`sdkconfig.defaults` enables the IDF features heaptop builds on: FreeRTOS run-time stats (64-bit counters), heap task tracking, heap hooks, standalone heap tracing and light heap poisoning. These are debugging features: task tracking in particular slows every allocation, so do not ship them in production firmware.
