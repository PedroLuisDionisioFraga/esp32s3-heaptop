# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-09-24

### Added

- Sampler task publishing heap regions (internal, DMA, PSRAM), per-task CPU %, stack high-water mark and heap, per-core load, allocation rates and trends.
- `ht` console command: `top`, `heap`, `frag`, `tasks`, `allocs`, `leaks`, `mark`, `diff`, `alerts`, `stream`.
- Leak capture grouped by call stack (heap tracing) and live leak suspicion per task (task tracking).
- Threshold alerts with hysteresis, logging and a callback; limits changeable at runtime.
- JSON Lines stream, from the console or from boot (`CONFIG_HEAPTOP_STREAM_AT_BOOT`).
- `basic` example with `stress` workloads.
