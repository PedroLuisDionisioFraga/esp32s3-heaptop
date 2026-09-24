# Heaptop

[![Component Registry](https://components.espressif.com/components/pedroluisdionisiofraga/heaptop/badge.svg)](https://components.espressif.com/components/pedroluisdionisiofraga/heaptop)

An htop-like heap and task monitor for ESP-IDF, driven over the serial console. Heaptop answers three questions about a running firmware:

- **Is it leaking?** Per-task heap growth flags suspects live, and a leak capture groups every allocation that was never freed by call stack.
- **Is the heap fragmenting?** Per-region fragmentation, largest free block trends and a free-block size histogram.
- **How does it behave?** CPU % per task and per core, stack high-water marks, allocation and free rates, and a log of failed allocations.

Everything is built on ESP-IDF's own instrumentation (task tracking, heap tracing, heap hooks, FreeRTOS run-time stats), sampled by one low-priority task that allocates nothing after start-up.

## Contents

- [Features](#features)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Commands](#commands)
- [Configuration](#configuration)
- [Stream protocol](#stream-protocol)
- [Alerts](#alerts)
- [API](#api)
- [Cost](#cost)
- [Caveats](#caveats)
- [Examples](#examples)

## Features

- `ht top`: live full-screen view with per-core load bars, memory regions with free and largest-block sparklines, allocation rates, active alerts and a sortable task table.
- `ht heap` / `ht frag`: internal, DMA and PSRAM regions (free, minimum ever, largest block, fragmentation %) and a free-block size histogram.
- `ht tasks`: state, priority, core, CPU %, stack high-water mark, heap held (with the PSRAM part), and `LEAK?` on tasks whose heap keeps growing. Deleted tasks that still own heap are listed too: that memory is leaked by definition.
- `ht leaks`: capture allocations that are never freed between `start` and `stop`, grouped by call stack and sorted by bytes. `idf.py monitor` turns the addresses into `file:line`.
- `ht allocs`: allocations, frees and bytes per second, and the last allocation failures with size, caps, heap function and task (or ISR).
- `ht mark` / `ht diff`: what changed in every region and task since a baseline.
- `ht alerts`: limits with hysteresis for free RAM, largest block, fragmentation, PSRAM, stack, leak suspects and allocation failures, logged once per transition and available as a callback.
- `ht stream`: JSON Lines for host tools, also available from boot without a console.

## Installation

```bash
idf.py add-dependency "pedroluisdionisiofraga/heaptop^0.1.0"
```

or in `main/idf_component.yml`:

```yaml
dependencies:
  pedroluisdionisiofraga/heaptop: "^0.1.0"
```

Requires ESP-IDF 6.0 or later.

## Quick start

```c
#include "heaptop.h"

void app_main(void)
{
  ESP_ERROR_CHECK(heaptop_init(NULL));  // Kconfig defaults

  // ... esp_console_init() / your REPL setup ...
  ESP_ERROR_CHECK(heaptop_console_register());  // adds the `ht` command
}
```

Heaptop turns on `CONFIG_FREERTOS_USE_TRACE_FACILITY` itself (it needs `uxTaskGetSystemState()`; the cost is a few bytes per task). Then enable the IDF data sources heaptop reads, for example in `sdkconfig.defaults`:

```
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64=y
CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y
CONFIG_HEAP_TASK_TRACKING=y
CONFIG_HEAP_TRACK_DELETED_TASKS=y
CONFIG_HEAP_USE_HOOKS=y
CONFIG_HEAP_TRACING_STANDALONE=y
CONFIG_HEAP_TRACING_STACK_DEPTH=4
```

Each one is optional. Without it, the matching column or command says which option to enable instead of showing wrong numbers:

| IDF option | Unlocks |
|---|---|
| `FREERTOS_GENERATE_RUN_TIME_STATS` (U64 counter recommended) | CPU % per task and per core |
| `FREERTOS_VTASKLIST_INCLUDE_COREID` | core column |
| `HEAP_TASK_TRACKING` (+ `HEAP_TRACK_DELETED_TASKS`) | heap per task, `LEAK?`, leak alert, deleted tasks holding heap |
| `HEAP_USE_HOOKS` | allocation, free and byte rates |
| `HEAP_TRACING_STANDALONE` | `ht leaks` |

## Commands

All commands live under `ht`, so they never collide with your own. `ht help` lists them.

| Command | What it shows |
|---|---|
| `ht top [refresh_ms]` | live view; `q` quits, `c`/`m`/`s`/`n` sort by CPU/memory/stack/name, `+`/`-` refresh, `p` pause |
| `ht heap` | region table |
| `ht frag [internal\|dma\|psram]` | free-block size histogram |
| `ht tasks [cpu\|heap\|stack\|name]` | task table, sorted |
| `ht allocs` | allocation rates and the failure log |
| `ht leaks start\|stop\|report [rows]\|status` | leak capture |
| `ht mark` / `ht diff` | changes since a baseline |
| `ht alerts [show\|set <key> <value>]` | limits and active alerts |
| `ht stream [every_ms]` | JSON Lines until `q` |

Example output (synthetic data, real format):

```
heaptop  up 3m04s  #184 every 1000ms  cost 1840us  refresh 1000ms  sort cpu
keys: q quit  c/m/s/n sort by cpu/memory/stack/name  +/- refresh  p pause
ALERTS: leak  (ht alerts for details)
cpu0 [########............]  41.2%   cpu1 [#...................]   9.6%
internal   183.5K free of 348.0K  min 167.0K  largest 92.0K   frag  49.9%
           free @#*++++=~~~~-,,,,.__  largest @___________________
dma        166.5K free of 327.0K  min 150.0K  largest 92.0K   frag  44.7%
psram        7.8M free of 8.0M    min 7.8M    largest 7.8M    frag   0.5%
           free ====================
allocs/s 212  frees/s 202  bytes/s 5.0K  failures 1

NAME             ST  PRI CORE   CPU%   STACK    HEAP    PEAK   PSRAM
IDLE1            Y     0    1   90.4     832       0       0       0
IDLE0            Y     0    0   58.8     816       0       0       0
main             R     1    0   21.4    4.1K    3.7K    8.9K       0
heaptop          Y     1    -    1.8    1.9K       0       0       0
stress_leak      B     1    -    0.3    2.6K   13.0K   13.0K       0  LEAK?
esp_timer        S    22    0    0.1    3.2K     104     104       0
ipc0             S    24    0    0.0     588       0       0       0
worker           X     -    -      -       -    2.0K    2.0K       0
```

States: `R` running, `Y` ready, `B` blocked, `S` suspended, `X` deleted but still holding heap. CPU % is per core (htop style), so a busy dual-core chip can add up to 200%. Fragmentation is the share of free bytes outside the largest free block: 0% means all free memory is one block.

```
leak trace stopped after 30.4s: 58 surviving allocations (buffer 58/256)
    BYTES  COUNT       SIZE  CALL STACK (innermost first)
    13.0K     52        256  0x42009a3c 0x42009b10 0x4037a2f0
     1.1K      6    64..320  0x4200c418 0x4200c52e 0x42012e04
idf.py monitor decodes the 0x4... addresses into function and file:line
```

A typical leak hunt:

1. `ht top`, and watch for `LEAK?` or a falling free/largest sparkline.
2. `ht leaks start`, exercise the suspect code path, then `ht leaks stop`. (`ht leaks report` also works while the capture runs: it pauses the capture for the moment it takes to print, so a block freed in that moment can still be listed.)
3. Read the call stacks in `idf.py monitor`. `ht mark` before and `ht diff` after the operation show which task kept the memory.

## Configuration

`idf.py menuconfig` → Component config → Heaptop:

| Option | Default | Meaning |
|---|---|---|
| `HEAPTOP_SAMPLE_PERIOD_MS` | 1000 | time between samples |
| `HEAPTOP_HISTORY_LEN` | 120 (30 without PSRAM) | per-task heap history for leak suspicion |
| `HEAPTOP_MAX_TASKS` | 32 | tasks shown |
| `HEAPTOP_TASK_STACK` / `_PRIO` / `_CORE` | 4096 / 1 / -1 | sampler task |
| `HEAPTOP_ALLOC_HOOKS` | y | define the heap hooks (needs `HEAP_USE_HOOKS`) |
| `HEAPTOP_FAILED_ALLOC_CALLBACK` | y | take the failed-allocation callback slot |
| `HEAPTOP_TASK_HEAP` | y | per-task heap (needs `HEAP_TASK_TRACKING`) |
| `HEAPTOP_LEAK_TRACE` / `_RECORDS` | y / 256 | leak capture (needs `HEAP_TRACING_STANDALONE`) |
| `HEAPTOP_BUFFERS_IN_PSRAM` | y | put heaptop's buffers in PSRAM when present |
| `HEAPTOP_STREAM_AT_BOOT` | n | JSON Lines on stdout from boot, no console needed |
| `HEAPTOP_ALERT_*` | see [Alerts](#alerts) | alert limits |

## Stream protocol

`ht stream` (or `CONFIG_HEAPTOP_STREAM_AT_BOOT`) writes one JSON object per line. Every line starts with `{"ht":1,` so a host script can pick heaptop lines out of normal log output. Sizes are bytes, `*10` fields are percent × 10, `t_ms` is uptime in milliseconds, and `null` means the data source is disabled.

| `type` | When | Fields |
|---|---|---|
| `sample` | every sample | `seq`, `t_ms`, `dt_ms`, `self_us`, `cpu10[]` (per core), `regions.{internal,dma,psram}.{total,free,min,largest,frag10,used_blocks,free_blocks}` or `null`, `allocs_s`, `frees_s`, `bytes_s`, `failures`, `alerts[]` |
| `task` | every sample, one per task | `seq`, `name`, `handle`, `state`, `prio`, `core`, `cpu10`, `hwm`, `heap`, `peak`, `psram`, `growth`, `leak` |
| `alert` | an alert turns on or off | `t_ms`, `alert`, `active`, `msg` |
| `fail` | a new allocation failure | `t_ms`, `size`, `caps`, `func`, `task` (name, handle or `null` in an ISR), `isr` |

```json
{"ht":1,"type":"sample","seq":184,"t_ms":184250,"dt_ms":1000,"self_us":1840,"cpu10":[412,96],"regions":{"internal":{"total":356352,"free":187904,"min":171008,"largest":94208,"frag10":499,"used_blocks":431,"free_blocks":37},"dma":{...},"psram":{...}},"allocs_s":212,"frees_s":202,"bytes_s":5222,"failures":1,"alerts":["leak"]}
{"ht":1,"type":"task","seq":184,"name":"stress_leak","handle":1070138368,"state":"blocked","prio":1,"core":null,"cpu10":3,"hwm":2716,"heap":13312,"peak":13312,"psram":0,"growth":12288,"leak":true}
{"ht":1,"type":"alert","t_ms":184250,"alert":"leak","active":true,"msg":"task 'stress_leak' heap grew +12.0K without giving memory back"}
```

A task line is about 200 bytes and a sample line about 700, so one sample with 32 tasks is about 7 KB: at 115200 baud (about 11 KB/s) 1 Hz uses most of the link. Stream less often (`ht stream 2000`), or raise `CONFIG_ESP_CONSOLE_UART_BAUDRATE`.

## Alerts

| Alert | Fires when | Kconfig default |
|---|---|---|
| `dram_free` | internal RAM free below the floor | `HEAPTOP_ALERT_DRAM_FREE_MIN` 20480 |
| `dram_largest` | largest internal free block below the floor | `HEAPTOP_ALERT_DRAM_LARGEST_MIN` 8192 |
| `frag` | internal fragmentation above the ceiling | `HEAPTOP_ALERT_FRAG_PCT_MAX` 80 |
| `psram_free` | PSRAM free below the floor | `HEAPTOP_ALERT_PSRAM_FREE_MIN` 65536 |
| `stack` | the lowest stack high-water mark below the floor | `HEAPTOP_ALERT_STACK_HWM_MIN` 512 |
| `leak` | a task is a leak suspect | `HEAPTOP_ALERT_TASK_GROWTH` 4096 |
| `alloc_fail` | an allocation failed since the previous sample | always on with the failure callback |

A limit of 0 turns its alert off. An alert clears only after the value moves `HEAPTOP_ALERT_HYSTERESIS_PCT` (10%) back past the limit, so it does not flap. Limits can be changed at runtime with `ht alerts set frag 70` or `heaptop_set_thresholds()`.

A task is a leak suspect when, over its history (at least 8 samples), its heap grew by at least `HEAPTOP_ALERT_TASK_GROWTH` in at least three separate steps, was still growing in the second half of the window, never dropped below where it started, and still holds at least 90% of its peak. A task that takes one long-lived buffer, finishes its start-up allocations, or frees what it took does not qualify.

Each transition is logged once (`ESP_LOGW` when it fires, `ESP_LOGI` when it clears; logs are held back while `ht top` or `ht stream` owns the terminal) and passed to the optional callback:

```c
static void on_alert(uint32_t alert, bool active, const heaptop_snapshot_t *s, void *ctx)
{
  if (alert == HEAPTOP_ALERT_DRAM_FREE && active)
    shed_load();  // runs in the sampler task: keep it short
}

heaptop_set_alert_cb(on_alert, NULL);
```

## API

See [include/heaptop.h](include/heaptop.h) and [include/heaptop_types.h](include/heaptop_types.h).

| Function | Purpose |
|---|---|
| `heaptop_init(cfg)` / `heaptop_deinit()` | start / stop the sampler (idempotent) |
| `heaptop_get_snapshot(&s)` | copy of the latest sample: regions, tasks, rates, trends, alerts |
| `heaptop_console_register()` | add the `ht` command |
| `heaptop_leaks_start()` / `_stop()` / `_report(out, rows)` | leak capture |
| `heaptop_mark()` / `heaptop_diff(out)` | baseline and changes |
| `heaptop_set_alert_cb()`, `heaptop_get_thresholds()`, `heaptop_set_thresholds()` | alerts |

## Cost

Measured with `idf.py size-components` on ESP32-S3 (IDF 6.0.2): 17.6 KB of flash code, about 310 B of IRAM code (the heap hooks and the failure callback), and about 1.8 KB of static DRAM.

Buffers allocated by `heaptop_init()` and `heaptop_console_register()`, estimated from structure sizes with the defaults: about 45 KB, in PSRAM when present, or about 34 KB of internal RAM on chips without PSRAM. `HEAPTOP_MAX_TASKS` and `HEAPTOP_HISTORY_LEN` shrink it. The first `ht leaks start` takes about 14 KB more internal RAM for the trace records (256 records at stack depth 4) and keeps it until reboot.

The sampler measures its own cost every sample: `cost` in the `ht top` header and `self_us` in the stream.

The IDF features heaptop reads have costs of their own. Task tracking and heap tracing slow every allocation, and task tracking adds bytes to each block. Enable them for development and debugging builds, not for production firmware.

## Caveats

- **Heap hooks.** Heaptop defines `esp_heap_trace_alloc_hook()` and `esp_heap_trace_free_hook()`. The definitions are weak (IDF declares them that way): if your application defines them too, yours win silently and heaptop's allocation rates stay at 0, so disable `HEAPTOP_ALLOC_HOOKS`. An in-place `realloc` calls the alloc hook without a matching free, so allocation counts run slightly ahead of frees in realloc-heavy code.
- **Failed-allocation callback.** IDF has one slot for it, and it cannot be unregistered. Disable `HEAPTOP_FAILED_ALLOC_CALLBACK` if your application needs the slot.
- **Task tracking.** With `CONFIG_HEAP_TASK_TRACKING`, allocating while the scheduler is suspended or from an ISR crashes. That is an IDF constraint, not heaptop's. Task tracking's own bookkeeping is not visible in any statistic.
- **heap_trace.** While `ht leaks` runs, heaptop owns the global `heap_trace`. Do not start another trace at the same time. Leak capture needs internal RAM for its records: records in PSRAM would miss allocations made from ISRs.
- **Heap walks.** `heap_caps_get_info()` and `ht frag` walk the heap with its lock held, adding interrupt latency proportional to the number of blocks. Keep the sample period at about 1 s on latency-sensitive systems. The walk for `ht frag` runs only on demand.
- **Terminal.** `ht top` and `ht stream` take over the console until `q`. Log lines from other tasks still appear, and `ht top` redraws over them. Use a terminal with ANSI support (`idf.py monitor`, Windows Terminal, PuTTY); plain terminals get plain frames.

## Examples

| Example | Description |
|---|---|
| [basic](examples/basic) | Interactive console with every `ht` command and `stress` workloads (leaks, fragmentation, CPU, stack, allocation failures) to try them on |

```bash
idf.py create-project-from-example "pedroluisdionisiofraga/heaptop:basic"
```

## Development

Host unit tests for the pure modules (metrics, rendering, JSON) need only gcc and Unity:

```bash
IDF_PATH=/path/to/esp-idf bash test/host/run_tests.sh
```

On-device tests: `pytest examples/basic/pytest_heaptop_basic.py --target esp32s3` (pytest-embedded).

## License

[MIT](LICENSE)
