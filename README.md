# Heaptop

[![Component Registry](https://components.espressif.com/components/pedroluisdionisiofraga/heaptop/badge.svg)](https://components.espressif.com/components/pedroluisdionisiofraga/heaptop)
[![Build Examples](https://img.shields.io/github/actions/workflow/status/PedroLuisDionisioFraga/esp32s3-heaptop/build.yml?branch=dev&label=builds)](https://github.com/PedroLuisDionisioFraga/esp32s3-heaptop/actions/workflows/build.yml)
[![Host Tests](https://img.shields.io/github/actions/workflow/status/PedroLuisDionisioFraga/esp32s3-heaptop/host_tests.yml?branch=dev&label=host%20tests)](https://github.com/PedroLuisDionisioFraga/esp32s3-heaptop/actions/workflows/host_tests.yml)
[![License](https://img.shields.io/github/license/PedroLuisDionisioFraga/esp32s3-heaptop)](LICENSE)
![Targets](https://img.shields.io/badge/targets-ESP32%20%7C%20S3%20%7C%20C3%20%7C%20C6-blue)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-%E2%89%A56.0-orange)

An htop-like memory monitor for ESP-IDF, driven over the serial console. Heaptop does five things:

- **What is used, what is free.** Free, minimum free, largest block and fragmentation for each heap region. Heap, stack and CPU for each task.
- **Health.** One command checks free memory, the largest block, fragmentation, stack margins, tasks that look like they leak, and failed allocations.
- **Clear.** Start a fresh measurement window: minimum free, peaks and failures count from now.
- **CPU stress.** Load every core to a set percentage, for benchmarks and soak tests.
- **Stream.** The same data as JSON Lines, for host scripts.

Everything is built on ESP-IDF's own instrumentation (heap info, task tracking, FreeRTOS run-time stats). One low-priority task takes the samples, and it allocates nothing after start-up.

```text
heaptop  up 1m35s  #95 every 1000ms  cost 1480us  refresh 1000ms  sort cpu
keys: q quit  c/m/s/n sort by cpu/memory/stack/name  +/- refresh  p pause
cpu0 [############........]  61.2%   cpu1 [############........]  60.4%
internal   345.5K free of 403.5K  min 343.8K  largest 263.9K  frag  23.6%
           free ====================  largest ====================
dma        338.1K free of 395.6K  min 336.3K  largest 263.9K  frag  21.9%
psram        7.9M free of 7.9M    min 7.9M    largest 7.8M    frag   0.8%
           free ====================
failures 0 since clear 35s ago

NAME             ST  PRI CORE   CPU%   STACK    HEAP    PEAK   PSRAM
ht_stress0       R     1    0   60.1    1.3K       0       0       0
ht_stress1       R     1    1   60.0    1.3K       0       0       0
IDLE1            Y     0    1   39.6     872       0       0       0
IDLE0            R     0    0   38.8     872       0       0       0
main             B     1    0    0.4    5.8K   95.1K  101.2K   38.0K
heaptop          B     1    -    0.2    2.9K       0       0       0
ipc0             S    24    0    0.0     596       0       0       0
ipc1             S    24    1    0.0     596       0       0       0
```

*`ht top` on an ESP32-S3 with 8 MB of PSRAM while `ht stress cpu 60` runs. Your numbers will differ.*

## Contents

- [Try it in five minutes](#try-it-in-five-minutes)
- [Heap basics in one minute](#heap-basics-in-one-minute)
- [Guided examples](#guided-examples)
  1. [What is used, what is free?](#1-what-is-used-what-is-free)
  2. [Is it healthy?](#2-is-it-healthy)
  3. [Start a fresh window](#3-start-a-fresh-window)
  4. [Stress the CPU](#4-stress-the-cpu)
  5. [Feed a host tool](#5-feed-a-host-tool)
- [Use it in your project](#use-it-in-your-project)
- [Commands](#commands)
- [Configuration](#configuration)
- [Stream protocol](#stream-protocol)
- [Health checks](#health-checks)
- [API](#api)
- [Footprint](#footprint)
- [Caveats](#caveats)
- [Development](#development)

## Try it in five minutes

The [basic example](examples/basic) is a serial console with every `ht` command:

```bash
idf.py create-project-from-example "pedroluisdionisiofraga/heaptop:basic"
cd basic
idf.py set-target esp32s3
idf.py -p PORT flash monitor
```

Type `ht` to list the commands. They all live under `ht`, so they never collide with your own:

```text
esp32s3> ht
usage: ht <subcommand> [args]
  ht top    [refresh_ms]             Live view; q quits, c/m/s/n sort, +/- refresh, p pause
  ht heap                            What is used and free: regions, min free, largest block, frag
  ht tasks  [cpu|heap|stack|name]    Who uses what: CPU %, stack high-water mark, heap
  ht health                          Every check with its value and limit, and the last failed allocations
  ht clear                           Reset min free, peaks, failures and trends: a fresh window
  ht stress cpu <pct> [s] | stop     Load every core to pct% (1..90) for s seconds, 0 = until stop
  ht stream [every_ms]               JSON Lines for host tools, one sample per line group; q stops
```

## Heap basics in one minute

Four numbers describe a heap region, and heaptop shows all four:

| Number | Meaning | Why it matters |
|---|---|---|
| **FREE** | bytes free right now | how much room is left |
| **MIN FREE** | the lowest FREE has been since boot, or since `ht clear` | how close the firmware came to running out |
| **LARGEST** | the biggest single free block | the largest `malloc()` that can succeed right now |
| **FRAG** | share of free bytes outside the largest block: `1 - LARGEST / FREE` | high means plenty free, but only in small pieces |

A heap with 150 holes of 512 bytes, each pinned between small blocks that are still in use:

```text
internal RAM (not to scale)

|used|free 512|s|free 512|s|free 512|s| ... |used ...|         free 187.9K         |
      \_______ 150 holes, 72.4K in all _______/        \____ the largest block ___/

FREE = 338.0K    LARGEST = 187.9K    FRAG = 1 - 187.9 / 338.0 = 44%
```

A `malloc(200 * 1024)` fails here even though 338 KB are free: no single piece is that big.

The regions:

- **internal**: the chip's own SRAM. It is fast, and it is the only RAM that ISRs and most DMA can use.
- **dma**: the part of internal RAM that DMA can reach. It overlaps `internal`, so do not add the two up.
- **psram**: external RAM, when the board has it. It is large but slower.

One more rule explains many surprises: **memory is charged to the task that allocated it**. The stack of a new task counts for the task that called `xTaskCreate()`, and heaptop's own buffers count for the task that called `heaptop_init()`.

## Guided examples

Each example gives the question, the commands, example output and how to read it. It ends with what to do in your own firmware.

### 1. What is used, what is free?

```text
ht heap
```

```text
REGION       TOTAL     FREE  MIN FREE  LARGEST   FRAG  BLOCKS used/free
internal    403.5K   345.5K    343.8K   263.9K  23.6%  117/9
dma         395.6K   338.1K    336.3K   263.9K  21.9%  117/8
psram         7.9M     7.9M      7.9M     7.8M   0.8%  199/12
```

- **internal**: 345.5K of 403.5K is free.
- **MIN FREE** is close to FREE, so nothing has squeezed the heap yet.
- **FRAG** is `1 - 263.9 / 345.5 = 23.6%`. Anything up to about 30% is normal after boot, because drivers and the console leave blocks spread around.
- **BLOCKS** counts used and free blocks. Many free blocks means many holes.

Then see which task holds what:

```text
ht tasks heap
```

```text
NAME             ST  PRI CORE   CPU%   STACK    HEAP    PEAK   PSRAM
main             B     1    0    0.4    5.8K   95.1K  101.2K   38.0K
IDLE1            Y     0    1   99.7     872       0       0       0
IDLE0            R     0    0   98.8     872       0       0       0
heaptop          B     1    -    0.2    2.9K       0       0       0
ipc0             S    24    0    0.0     596       0       0       0
ipc1             S    24    1    0.0     596       0       0       0
```

- **HEAP** is what the task holds now, **PEAK** is the most it has held, and **PSRAM** is the part of HEAP in external RAM.
- **STACK** is the stack high-water mark: the least free stack the task has ever had.
- `main` holds the console and heaptop's buffers, because it called `heaptop_init()`.
- A task marked `LEAK?` keeps growing without giving memory back. See [Health checks](#health-checks).
- Task states: `R` running, `Y` ready, `B` blocked, `S` suspended, `X` deleted but still holding heap. Memory held by an `X` task is leaked by definition.

> **In your firmware:** run `ht heap` after your heaviest scenario, such as Wi-Fi connect, OTA or peak traffic. MIN FREE is the number to watch, because it keeps the worst moment even after the memory comes back.

### 2. Is it healthy?

```text
ht health
```

```text
health OK, stats since boot

CHECK         NOW                        LIMIT      STATE
dram_free     345.5K                     >= 20.0K   ok
dram_largest  263.9K                     >= 8.0K    ok
frag          23.6%                      <= 80%     ok
psram_free    7.9M                       >= 64.0K   ok
stack         596 (ipc0)                 >= 256     ok
leak          +1.0K (main)               < 4.0K     ok
alloc_fail    0                          any new    ok
an alert clears once its value is 10% back past the limit

failures 0 since boot
```

- **The first line** is the verdict. When something is wrong, it names the alerts, for example `health: 1 alert (alloc_fail)`.
- **NOW** is the current value, and **LIMIT** is what counts as healthy. The limits come from menuconfig.
- **stack** is the task with the least stack left. `ipc0`/`ipc1` at about 600 bytes is normal for IDF's own tasks, which is why the limit defaults to 256.
- **leak** is the task whose heap grew the most over its history: the last 120 samples with PSRAM, 30 without.

When an allocation fails, the check fires and the failure is logged:

```text
health: 1 alert (alloc_fail), stats since boot
...
alloc_fail    1                          any new    ALERT
...
failures 1 since boot
LAST FAILURES (newest first)
     AGE     SIZE       CAPS  TASK             FUNCTION
    2.9s    95.3M 0x00000804  main             heap_caps_malloc
```

Here, `main` asked for 95.3M (100,000,000 bytes) of `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` memory. Compare the SIZE with `ht heap`:

- **SIZE larger than FREE:** you are out of memory.
- **SIZE larger than LARGEST, but smaller than FREE:** fragmentation.
- **CAPS asking for internal or DMA memory:** PSRAM does not count for that request.
- **A huge SIZE:** usually a bug, such as a negative length or an uninitialised variable.

Alerts also go to the log as they happen (`W (...) HEAPTOP: ALERT stack: task 'blink' has 192 bytes of stack left (floor 256)`), and to a callback in your code:

```c
static void on_alert(uint32_t alert, bool active, const heaptop_snapshot_t *s, void *ctx)
{
  if (alert == HEAPTOP_ALERT_DRAM_FREE && active)
    shed_load();  // runs in the sampler task: keep it short
}

heaptop_set_alert_cb(on_alert, NULL);
```

> **In your firmware:** keep at least 512 bytes to 1 KB of stack high-water mark in every task, measured after its heaviest path. Logging with `printf` of floats is a classic stack eater. If fragmentation climbs, allocate long-lived buffers once at start-up, and use pools for many objects of the same size.

### 3. Start a fresh window

MIN FREE, PEAK and the failure count normally cover everything since boot, including start-up. To measure one scenario on its own, clear them first:

```text
ht clear
stats cleared; stack high-water marks keep their since-boot minimum
```

Then run the scenario, and read `ht heap`, `ht tasks heap` and `ht health`. Now:

- **MIN FREE** is the lowest free memory *during the scenario*.
- **PEAK** is each task's highest heap since the clear.
- **failures** counts only failed allocations since the clear.
- `ht health` and `ht top` say `since clear 35s ago`.
- Trends and leak suspicion start over.

The stack high-water mark cannot be reset, because FreeRTOS keeps only the since-boot minimum.

> **In your firmware:** `heaptop_clear()` does the same from code. Call it before a test step, and read `heaptop_get_snapshot()` after it.

### 4. Stress the CPU

`ht stress cpu <pct> [seconds]` starts one worker per core (`ht_stress0`, `ht_stress1`). Each one is busy `pct` ms out of every 100 ms. Leave out the seconds, or pass 0, to run until `ht stress stop`:

```text
ht stress cpu 60 30
cpu stress: 60% on 2 cores, 30 s left
watch it with `ht top`
```

The `ht top` at the [top of this page](#heaptop) shows it running:

- **Core bars**: both cores at about 60%. A core's load is 100% minus its idle task.
- **Tasks**: `ht_stress0` and `ht_stress1` each use 60% of their core. CPU % is per core (htop style), so a dual-core chip can add up to 200%.
- `ht stress` on its own shows what is running, and `ht stress stop` ends it within 100 ms.

The load is capped at 90%, so the idle task, and the task watchdog that watches it, keep running. Workers run at priority 1: tasks above that still get the CPU when they need it, and the load takes what is left.

> **In your firmware:** run your timing-sensitive paths (audio, control loops, network throughput) with and without `ht stress cpu 70`. Anything that breaks under load was already close to its limit. From code, it is `heaptop_stress_cpu(70, 60)`.

### 5. Feed a host tool

`ht stream` prints the same data as JSON Lines, one object per line, until you press `q`. Every heaptop line starts with `{"ht":2,`, so a script can pick them out of normal logs. This Python script (with `pip install pyserial`) prints the internal RAM of five samples:

```python
import json

import serial  # pip install pyserial

port = serial.Serial()
port.port, port.baudrate, port.timeout = 'COM6', 115200, 5
port.dtr = port.rts = False  # opening the port must not reset the board
port.open()
port.write(b'ht stream\r')

samples = 0
while samples < 5:
    line = port.readline().decode(errors='replace').strip()
    if not line.startswith('{"ht":2,'):
        continue  # command echo, prompt, ordinary log lines
    record = json.loads(line)
    if record['type'] == 'sample':
        ram = record['regions']['internal']
        print(f"t={record['t_ms'] // 1000}s  free={ram['free']}  "
              f"largest={ram['largest']}  frag={ram['frag10'] / 10}%")
        samples += 1

port.write(b'q')  # give the console back
port.close()
```

Devices without a console can stream from boot with `CONFIG_HEAPTOP_STREAM_AT_BOOT`. The format is in [Stream protocol](#stream-protocol).

## Use it in your project

Add the component:

```bash
idf.py add-dependency "pedroluisdionisiofraga/heaptop^0.2.0"
```

or in `main/idf_component.yml`:

```yaml
dependencies:
  pedroluisdionisiofraga/heaptop: "^0.2.0"
```

Requires ESP-IDF 6.0 or later. Then start it and register the command:

```c
#include "heaptop.h"

void app_main(void)
{
  ESP_ERROR_CHECK(heaptop_init(NULL));  // menuconfig defaults

  // ... esp_console_init() / your REPL setup ...
  ESP_ERROR_CHECK(heaptop_console_register());  // adds the `ht` command
}
```

Heaptop turns on `CONFIG_FREERTOS_USE_TRACE_FACILITY` itself, because it needs `uxTaskGetSystemState()`. That costs a few bytes per task. Everything else it reads is optional. Enable what you want, for example in `sdkconfig.defaults`:

```text
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64=y
CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y
CONFIG_HEAP_TASK_TRACKING=y
CONFIG_HEAP_TRACK_DELETED_TASKS=y
```

| IDF option | Unlocks |
|---|---|
| `FREERTOS_GENERATE_RUN_TIME_STATS` (U64 counter recommended) | CPU % per task and per core |
| `FREERTOS_VTASKLIST_INCLUDE_COREID` | core column |
| `HEAP_TASK_TRACKING` (+ `HEAP_TRACK_DELETED_TASKS`) | heap per task, `LEAK?` and the leak check, deleted tasks holding heap |

Heaptop detects each one. Without it, the matching column shows `-` and the matching check shows `n/a`, rather than a wrong number. With all of them off, `ht tasks` still shows state, priority and stack:

```text
esp32s3> ht tasks
NAME             ST  PRI CORE   CPU%   STACK    HEAP    PEAK   PSRAM
heaptop          R     1    -      -    3.2K       -       -       -
IDLE1            Y     0    -      -     872       -       -       -
IDLE0            Y     0    -      -     872       -       -       -
ipc0             S    24    -      -     600       -       -       -
main             B     1    -      -    5.8K       -       -       -
ipc1             S    24    -      -     600       -       -       -
```

At boot, heaptop logs what its buffers cost, as measured:

```text
I (697) HEAPTOP: started: every 1000 ms, up to 32 tasks, ... bytes of buffers in PSRAM
```

## Commands

| Command | What it shows |
|---|---|
| `ht top [refresh_ms]` | live view; `q` quits, `c`/`m`/`s`/`n` sort by CPU/memory/stack/name, `+`/`-` refresh, `p` pause |
| `ht heap` | region table (from the latest sample) |
| `ht tasks [cpu\|heap\|stack\|name]` | task table, sorted |
| `ht health` | every check with its value and limit, and the last failed allocations |
| `ht clear` | start a fresh window: min free, peaks, failures, trends, leak history |
| `ht stress cpu <pct> [seconds]` / `ht stress stop` / `ht stress` | CPU load on every core / end it / what is running |
| `ht stream [every_ms]` | JSON Lines until `q` |

## Configuration

`idf.py menuconfig` → Component config → Heaptop:

| Option | Default | Meaning |
|---|---|---|
| `HEAPTOP_SAMPLE_PERIOD_MS` | 1000 | time between samples |
| `HEAPTOP_STREAM_AT_BOOT` | n | JSON Lines on stdout from boot, no console needed |
| `HEAPTOP_ALERT_*` | see [Health checks](#health-checks) | health limits |

Everything else is automatic. Heaptop reads whichever IDF data sources are enabled, and it puts its buffers in PSRAM when the chip has it. It shows up to 32 tasks. To change that, define `HEAPTOP_MAX_TASKS` for the whole build, for example with `idf_build_set_property(COMPILE_DEFINITIONS "HEAPTOP_MAX_TASKS=48" APPEND)` in the project's `CMakeLists.txt`.

## Stream protocol

`ht stream` (or `CONFIG_HEAPTOP_STREAM_AT_BOOT`) writes one JSON object per line. Every line starts with `{"ht":2,`. Sizes are bytes, `*10` fields are percent × 10, `t_ms` is uptime in milliseconds, and `null` means the data source is disabled.

| `type` | When | Fields |
|---|---|---|
| `sample` | every sample | `seq`, `t_ms`, `since_ms` (uptime of the last clear, 0 = boot), `dt_ms`, `self_us`, `cpu10[]` (per core), `regions.{internal,dma,psram}.{total,free,min,largest,frag10,used_blocks,free_blocks}` or `null`, `failures`, `alerts[]` |
| `task` | every sample, one per task | `seq`, `name`, `handle`, `state`, `prio`, `core`, `cpu10`, `hwm`, `heap`, `peak`, `psram`, `growth`, `leak` |
| `alert` | an alert turns on or off | `t_ms`, `alert`, `active`, `msg` |
| `fail` | a new allocation failure | `t_ms`, `size`, `caps`, `func`, `task` (name, handle or `null` in an ISR), `isr` |

For example:

```json
{"ht":2,"type":"sample","seq":51,"t_ms":50051,"since_ms":0,"dt_ms":1000,"self_us":2047,"cpu10":[67,1],"regions":{"internal":{"total":412835,"free":338595,"min":266091,"largest":176112,"frag10":479,"used_blocks":117,"free_blocks":17},"dma":{"total":405171,"free":330931,"min":258427,"largest":176112,"frag10":467,"used_blocks":117,"free_blocks":16},"psram":{"total":8382312,"free":8329304,"min":8310816,"largest":8257520,"frag10":8,"used_blocks":199,"free_blocks":12}},"failures":1,"alerts":[]}
{"ht":2,"type":"task","seq":51,"name":"main","handle":1070195020,"state":"blocked","prio":1,"core":0,"cpu10":64,"hwm":5944,"heap":110544,"peak":181980,"psram":51196,"growth":31292,"leak":false}
{"ht":2,"type":"fail","t_ms":43358,"size":100000000,"caps":2052,"func":"heap_caps_malloc","task":"main","isr":false}
```

An alert line looks like `{"ht":2,"type":"alert","t_ms":...,"alert":"leak","active":true,"msg":"task 'worker' heap grew +20.8K without giving memory back"}`.

A task line is about 200 bytes and a sample line about 700, so one sample with 32 tasks is about 7 KB. At 115200 baud (about 11 KB/s), 1 Hz uses most of the link. Stream less often (`ht stream 2000`), or raise `CONFIG_ESP_CONSOLE_UART_BAUDRATE`.

**Changes from version 1** (heaptop 0.1.0): the sample line lost `allocs_s`, `frees_s` and `bytes_s`, and gained `since_ms`. `failures` now counts from the last clear.

## Health checks

| Check | Fires when | Kconfig default |
|---|---|---|
| `dram_free` | internal RAM free below the floor | `HEAPTOP_ALERT_DRAM_FREE_MIN` 20480 |
| `dram_largest` | largest internal free block below the floor | `HEAPTOP_ALERT_DRAM_LARGEST_MIN` 8192 |
| `frag` | internal fragmentation above the ceiling | `HEAPTOP_ALERT_FRAG_PCT_MAX` 80 |
| `psram_free` | PSRAM free below the floor | `HEAPTOP_ALERT_PSRAM_FREE_MIN` 65536 |
| `stack` | the lowest stack high-water mark below the floor | `HEAPTOP_ALERT_STACK_HWM_MIN` 256 |
| `leak` | a task is a leak suspect | `HEAPTOP_ALERT_TASK_GROWTH` 4096 |
| `alloc_fail` | an allocation failed since the previous sample | always on |

A limit of 0 turns its check off. An alert clears only after the value moves 10% back past the limit, so it does not flap. The limits can also be set in code, through `heaptop_config_t.thresholds` passed to `heaptop_init()`.

A task is a leak suspect when all of these hold over its history (at least 20 samples, up to 120 with PSRAM or 30 without):

- its heap grew by at least `HEAPTOP_ALERT_TASK_GROWTH`, in at least three separate steps;
- it grew by at least a sixth of that in every third of the window;
- it never dropped below where it started;
- it still holds at least 90% of its peak.

A task that takes one long-lived buffer, finishes its start-up allocations (like `main` right after boot), or frees what it took does not qualify.

Each transition is logged once and passed to the optional callback (see [example 2](#2-is-it-healthy)). It is logged with `ESP_LOGW` when it fires and `ESP_LOGI` when it clears. Logs are held back while `ht top` or `ht stream` owns the terminal.

## API

See [include/heaptop.h](include/heaptop.h) and [include/heaptop_types.h](include/heaptop_types.h).

| Function | Purpose |
|---|---|
| `heaptop_init(cfg)` / `heaptop_deinit()` | start / stop the sampler (idempotent) |
| `heaptop_get_snapshot(&s)` | copy of the latest sample: regions, tasks, trends, failures, alerts |
| `heaptop_clear()` | start a fresh window |
| `heaptop_set_alert_cb(cb, ctx)` | be told when a check fires or clears |
| `heaptop_stress_cpu(pct, seconds)` / `heaptop_stress_stop()` | CPU load on every core |
| `heaptop_console_register()` | add the `ht` command |

## Footprint

Measured with ESP-IDF 6.0.2 on an ESP32-S3, building [examples/basic](examples/basic):

| What | Cost |
|---|---|
| code (`idf.py size-components`) | 16.2 KB of flash, 180 B of IRAM (the failed-allocation callback), 1.6 KB of static DRAM |
| heaptop's buffers (`heaptop_init()`) | logged at boot. Most of it is the per-task heap history: 32 tasks × 120 samples × 4 bytes = 15 KB with PSRAM, 3.8 KB without |
| console buffers (`heaptop_console_register()`) | about 8.5 KB (a text buffer and a snapshot copy) |
| CPU stress workers | two 2 KB stacks, created by the first `ht stress cpu` and kept until `heaptop_deinit()` |
| sampler time per sample | 0.8 to 2.0 ms at 1 Hz, about 0.1 to 0.2% of one core (measured with heaptop 0.1.0; the sampling work is unchanged). It grows with the number of heap blocks, and `ht top` shows it as `cost` |

The IDF features heaptop reads have costs of their own:

- **Allocation speed.** On heaptop 0.1.0's example config, 30,000 malloc/free pairs of 128 bytes took 0.43 s (14 µs per pair) with every optional source off. With task tracking, heap hooks and heap tracing on, they took 1.85 to 2.41 s (62 to 80 µs per pair). Task tracking was the main cost, and it gets slower as more allocations are alive.
- **Task tracking bookkeeping.** It costs about 60 bytes per live allocation, and it lives in the largest heap, which is PSRAM on this board. None of it appears in any per-task number.

Enable these features for development and debugging builds, not for production firmware.

**Case study: what `ht top` caught in the IDF console example.** The console example saves its command history to flash after every command. With it on, `ht tasks` showed this after a few commands:

```text
NAME             ST  PRI CORE   CPU%   STACK    HEAP    PEAK   PSRAM
main             Y     1    0   57.7    5.6K  107.2K  111.7K   63.1K
ipc1             S    24    1   52.1     564      64      88       0
IDLE1            Y     0    1   48.3     760       0       0       0
IDLE0            Y     0    0   42.1     872       0       0       0
```

`ipc1` is the task IDF uses to park the second core while flash is being written, and here it held core 1 half the time. The basic example keeps history in RAM (`CONFIG_CONSOLE_STORE_HISTORY` off) for that reason.

## Caveats

- **Tasks that delete themselves.** With `CONFIG_HEAP_TASK_TRACKING` on ESP-IDF 6.0.2, a task that calls `vTaskDelete(NULL)` while other tasks allocate or free can abort the chip with `assert failed: prvSelectHighestPriorityTaskSMP ... (xTaskScheduled == 1)`. The idle task frees the dead task's stack, and that free waits on task tracking's mutex. If another task holds the mutex, the idle task blocks, and its core has nothing left to run. Heaptop's sampler and stress workers therefore never delete themselves: they suspend, and `heaptop_deinit()` deletes them. If your firmware hits this assert, do the same, or turn task tracking off.
- **Stack high-water marks survive `ht clear`.** FreeRTOS keeps only the since-boot minimum, so the `stack` check and the STACK column cannot start over.
- **Minimum free after `ht clear`.** Heaptop resets it with `heap_caps_monitor_local_minimum_free_size_start()`, which affects every reader of `heap_caps_get_minimum_free_size()`. `heaptop_deinit()` restores the since-boot values. The first clear allocates a small table in IDF (a few bytes per heap).
- **Task PEAK after `ht clear`.** IDF keeps only a since-boot peak. When a task sets a new record after the clear, PEAK is exact. Otherwise it is the highest value heaptop sampled since the clear, and a spike between two samples can be missed.
- **Failed-allocation callback.** IDF has one slot for it, and heaptop takes it at `heaptop_init()`. If your application needs the slot, register your own callback after `heaptop_init()`. Yours replaces heaptop's, and heaptop's failure log stays empty (`ht health` shows `n/a`).
- **CPU stress.** The workers busy-wait, so they burn power as well as CPU. At 90% the idle task gets 10 ms of every 100, which is enough for the task watchdog but little else at priority 0.
- **Task tracking.** With `CONFIG_HEAP_TASK_TRACKING`, allocating while the scheduler is suspended or from an ISR crashes. That is an IDF constraint, not heaptop's. Memory is charged to the task that allocated it.
- **Heap walks.** `heap_caps_get_info()` walks the heap with its lock held, adding interrupt latency proportional to the number of blocks. Keep the sample period at about 1 s on latency-sensitive systems.
- **Terminal.** `ht top` and `ht stream` take over the console until `q`. Log lines from other tasks still appear, and `ht top` redraws over them. Use a terminal with ANSI support (`idf.py monitor`, Windows Terminal, PuTTY); plain terminals get plain frames.

## Development

Host unit tests for the pure modules (metrics, rendering, JSON) need only gcc and Unity:

```bash
IDF_PATH=/path/to/esp-idf bash test/host/run_tests.sh
```

On-device tests (pytest-embedded) flash the example and drive the console. From `examples/basic`, with the board on `PORT`:

```bash
pytest pytest_heaptop_basic.py --embedded-services esp,idf --target esp32s3 --port PORT --build-dir build
```

## License

[MIT](LICENSE)
