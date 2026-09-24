| Supported Targets | ESP32 | ESP32-C3 | ESP32-C6 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- |

# Heaptop basic example

An interactive serial console (based on ESP-IDF's `system/console/advanced` example) with heaptop's `ht` commands, plus `stress` commands that create leaks, fragmentation, CPU load and allocation failures, so every heaptop view has something to show.

## Hardware

Written for an ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM) with the console on the UART port. `sdkconfig.defaults.esp32s3` holds the board settings; other targets build with 4 MB flash and no PSRAM.

## Build and flash

```bash
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Type `help` for every command, `ht` for heaptop's.

## Try it

| Do | Then look at |
|---|---|
| `stress leak 256 100` | `ht top` (the `stress_leak` heap climbs, then `LEAK?` and the `leak` alert appear); `ht leaks start` … `ht leaks stop` groups the allocations by call stack |
| `stress frag 200` | `ht heap` (fragmentation rises) and `ht frag` (free blocks shift to small sizes) |
| `stress cpu 50` | `ht top` (`stress_cpu` near 50%, one core busier) |
| `stress stack 3328` | `ht tasks stack` (`stress_stack` near the bottom) and the `stack` alert |
| `stress fail 100000000` | `ht allocs` (the failure, with its size and task) and the `alloc_fail` alert |
| `stress burst 5000` | the allocs/s line in `ht top` |
| `ht mark`, then any of the above, then `ht diff` | what changed, region by region and task by task |
| `ht stream` | JSON Lines; `q` stops |

`stress stop` ends every workload and frees what it kept.

## Configuration

`sdkconfig.defaults` enables the IDF features heaptop builds on: FreeRTOS run-time stats (64-bit counters), heap task tracking, heap hooks, standalone heap tracing and light heap poisoning. These are debugging features, and task tracking in particular slows every allocation, so do not ship them in production firmware.

`sdkconfig.ci.minimal` turns all of them off (heaptop degrades to what is left), and `sdkconfig.ci.stream_at_boot` streams JSON Lines from boot. To build with one:

```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci.minimal" build
```

## Tests

With the board attached: `pytest pytest_heaptop_basic.py --target esp32s3 --port PORT` (pytest-embedded).
