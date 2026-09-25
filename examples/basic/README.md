| Supported Targets | ESP32 | ESP32-C3 | ESP32-C6 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- |

# Heaptop basic example

An interactive serial console (based on ESP-IDF's `system/console/advanced` example) with heaptop's `ht` commands.

## Hardware

Written for an ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) with the console on the UART port. `sdkconfig.defaults.esp32s3` holds the board settings; other targets build with 4 MB flash and no PSRAM.

## Build and flash

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Type `help` for every command, `ht` for heaptop's.

## Try it

The [guided examples](../../README.md#guided-examples) in the main README walk through these step by step. In short:

| Do | Then look at |
|---|---|
| `ht heap`, `ht tasks heap` | what is used and what is free, per region and per task |
| `ht health` | every check with its value and limit, and the last failed allocations |
| `ht clear` | min free, peaks and failures start over from now |
| `ht stress cpu 60 30` | `ht top`: both cores near 60% for 30 seconds |
| `ht stream` | JSON Lines; `q` stops |

## Configuration

`sdkconfig.defaults` enables the IDF features heaptop reads: FreeRTOS run-time stats (64-bit counters) for CPU %, and heap task tracking for heap per task. Light heap poisoning is on too. These are debugging features, and task tracking in particular slows every allocation, so do not ship them in production firmware.

`sdkconfig.ci.minimal` turns all of them off (heaptop degrades to what is left), and `sdkconfig.ci.stream_at_boot` streams JSON Lines from boot. To build with one:

```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci.minimal" build
```

## Tests

With the board attached, after `idf.py build` (pytest-embedded flashes the build):

```bash
pytest pytest_heaptop_basic.py --embedded-services esp,idf --target esp32s3 --port PORT --build-dir build
```
