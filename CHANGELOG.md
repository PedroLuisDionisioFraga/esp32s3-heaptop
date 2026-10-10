# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0] - 2026-10-10

### Added

- `heaptop_json_snapshot()` (`include/heaptop_json.h`): writes a snapshot as one JSON document through a chunk callback, for a web UI or an HTTP/MQTT client. It uses the field names of the stream protocol, allocates nothing, and takes about 600 bytes of stack. Flags choose whether the tasks and the trends are included, and an optional `limits` argument reports the alert limits. The document is covered by a host test.

### Changed

- The JSON Lines stream and the JSON document share their string escaping and name tables (`src/heaptop_jsonutil.h`). The stream output is unchanged.

### Fixed

- README: the install snippet pointed at `^0.2.0`.

## [0.3.1] - 2026-10-07

### Changed

- The GitHub repository is now `esp32s3_heaptop`: the links in the component manifest and the README badges point to it.

## [0.3.0] - 2026-10-07

### Added

- `ht health` / health view: MIN and MAX columns, the lowest and highest value of every check since boot or `ht clear`. Snapshot fields `check_seen`, `check_min` and `check_max`.

## [0.2.0] - 2026-09-24

Heaptop is now a simple memory monitor: what is used, what is free, health, a fresh window on demand, and CPU stress.

### Added

- `ht health` / health view: a verdict line, every check with its current value and limit, and the last failed allocations.
- `ht clear` / `heaptop_clear()`: reset minimum free, task peaks, the failure log, trends and leak-suspicion history. Stack high-water marks keep their since-boot minimum.
- `ht stress cpu <pct> [seconds]` / `heaptop_stress_cpu()` / `heaptop_stress_stop()`: load every core, for benchmarks and soak tests.
- Snapshot field `since_us` and stream field `since_ms`: when the current stats window started.

### Changed

- Data sources are detected from the IDF configuration. Menuconfig keeps only the sample period, the boot stream and the alert limits.
- Buffers go to PSRAM whenever the chip has it. The sampler task's stack, priority and core are fixed (4096, 1, no affinity), and `heaptop_config_t` holds only the period and the limits.
- The failed-allocation callback is always registered. An application that needs the slot registers its own after `heaptop_init()`.
- Alert hysteresis is fixed at 10%.
- Stream protocol version 2: the sample line lost `allocs_s`, `frees_s` and `bytes_s`, and `failures` counts from the last clear.
- The `basic` example has no `stress` command any more. CPU load comes from `ht stress cpu`.

### Removed

- Leak capture (`ht leaks`, `heaptop_leaks_start/stop/report`); live leak suspicion per task stays.
- `ht mark` / `ht diff` / `heaptop_mark()` / `heaptop_diff()`.
- `ht frag` (free-block histogram); fragmentation % stays in `ht heap`, `ht top` and `ht health`.
- `ht allocs`, heap hooks and allocation rates; failed allocations moved to `ht health`.
- `ht alerts`, `heaptop_get_thresholds()` and `heaptop_set_thresholds()`.
- Kconfig options `HEAPTOP_HISTORY_LEN`, `MAX_TASKS`, `TASK_STACK`, `TASK_PRIO`, `TASK_CORE`, `ALLOC_HOOKS`, `FAILED_ALLOC_CALLBACK`, `FAIL_RING_LEN`, `TASK_HEAP`, `LEAK_TRACE`, `LEAK_RECORDS`, `BUFFERS_IN_PSRAM` and `ALERT_HYSTERESIS_PCT`.

## [0.1.0] - 2026-09-24

### Added

- Sampler task publishing heap regions (internal, DMA, PSRAM), per-task CPU %, stack high-water mark and heap, per-core load, allocation rates and trends.
- `ht` console command: `top`, `heap`, `frag`, `tasks`, `allocs`, `leaks`, `mark`, `diff`, `alerts`, `stream`.
- Leak capture grouped by call stack (heap tracing) and live leak suspicion per task (task tracking).
- Threshold alerts with hysteresis, logging and a callback; limits changeable at runtime.
- JSON Lines stream, from the console or from boot (`CONFIG_HEAPTOP_STREAM_AT_BOOT`).
- `basic` example with `stress` workloads.
- README guided examples (heap health, fragmentation, leaks, CPU, stack, failed allocations, streaming) with real screenshots from an ESP32-S3.
