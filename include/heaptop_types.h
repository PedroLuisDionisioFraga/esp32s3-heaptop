/**
 * @file heaptop_types.h
 * @brief Snapshot data model shared by the sampler, the renderers and the stream.
 *
 * Pure C (stdint/stdbool only) so the formatting code compiles and is unit
 * tested on the host. Sizes are in bytes, percentages are fixed-point x10
 * (523 = 52.3%) to keep floats out of the firmware.
 */

#ifndef HEAPTOP_TYPES_H
#define HEAPTOP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef HEAPTOP_MAX_TASKS
#ifdef CONFIG_HEAPTOP_MAX_TASKS
#define HEAPTOP_MAX_TASKS CONFIG_HEAPTOP_MAX_TASKS
#else
#define HEAPTOP_MAX_TASKS 32
#endif
#endif

#define HEAPTOP_TASK_NAME_LEN 16
#define HEAPTOP_MAX_CORES     2

/** Memory classes heaptop reports on. */
typedef enum heaptop_region
{
  HEAPTOP_REGION_INTERNAL = 0, /**< Internal 8-bit capable RAM (DRAM) */
  HEAPTOP_REGION_DMA,          /**< Internal DMA-capable RAM */
  HEAPTOP_REGION_PSRAM,        /**< External SPI RAM, when present */
  HEAPTOP_REGION_COUNT,
} heaptop_region_t;

/** Task scheduler state, mirroring eTaskState plus a heaptop-only "deleted but holding heap". */
typedef enum heaptop_task_state
{
  HEAPTOP_TASK_RUNNING = 0,
  HEAPTOP_TASK_READY,
  HEAPTOP_TASK_BLOCKED,
  HEAPTOP_TASK_SUSPENDED,
  HEAPTOP_TASK_DELETED,
} heaptop_task_state_t;

/** Sort keys for task tables. */
typedef enum heaptop_sort
{
  HEAPTOP_SORT_CPU = 0, /**< Highest CPU first */
  HEAPTOP_SORT_HEAP,    /**< Most heap held first */
  HEAPTOP_SORT_STACK,   /**< Lowest stack high-water mark first */
  HEAPTOP_SORT_NAME,    /**< Alphabetical */
} heaptop_sort_t;

/** Feature bits: which data sources were compiled in and are live. */
#define HEAPTOP_FEAT_RUNTIME_STATS (1u << 0) /**< Per-task CPU % available */
#define HEAPTOP_FEAT_TASK_HEAP     (1u << 1) /**< Per-task heap via task tracking */
#define HEAPTOP_FEAT_ALLOC_HOOKS   (1u << 2) /**< Allocation/free counters */
#define HEAPTOP_FEAT_FAIL_CB       (1u << 3) /**< Failed-allocation callback */
#define HEAPTOP_FEAT_LEAK_TRACE    (1u << 4) /**< Leak capture via heap_trace */

typedef struct heaptop_region_stats
{
  bool present;         /**< false when the region does not exist (e.g. no PSRAM) */
  uint32_t total;       /**< free + allocated */
  uint32_t free;        /**< free bytes now */
  uint32_t min_free;    /**< lowest free bytes since boot */
  uint32_t largest;     /**< largest free block */
  uint32_t used_blocks; /**< allocated blocks */
  uint32_t free_blocks; /**< free blocks */
  uint16_t frag_pct10;  /**< 0 = one contiguous free block, 1000 = fully fragmented */
} heaptop_region_stats_t;

typedef struct heaptop_task_stats
{
  char name[HEAPTOP_TASK_NAME_LEN];
  uintptr_t handle;
  uint8_t state; /**< heaptop_task_state_t */
  uint8_t prio;
  int8_t core;         /**< -1 = no affinity or unknown */
  bool leak_suspect;   /**< heap kept growing across the history window */
  uint16_t cpu_pct10;  /**< % of one core over the last interval */
  uint32_t stack_hwm;  /**< minimum free stack ever, bytes */
  uint32_t heap_cur;   /**< heap held now (internal + PSRAM) */
  uint32_t heap_peak;  /**< peak heap held */
  uint32_t heap_psram; /**< part of heap_cur that lives in PSRAM */
  int32_t heap_growth; /**< heap_cur change over the history window */
} heaptop_task_stats_t;

typedef struct heaptop_alloc_stats
{
  uint32_t allocs_per_s;
  uint32_t frees_per_s;
  uint32_t bytes_per_s; /**< bytes allocated per second */
  uint32_t failures;    /**< failed allocations since boot */
} heaptop_alloc_stats_t;

typedef struct heaptop_snapshot
{
  uint32_t seq; /**< increments every sample; 0 = no sample yet */
  uint64_t uptime_us;
  uint32_t dt_ms;    /**< real time since the previous sample */
  uint32_t self_us;  /**< time heaptop spent taking this sample */
  uint32_t features; /**< HEAPTOP_FEAT_* */
  uint32_t alerts;   /**< active alert bits */
  uint8_t num_cores;
  uint16_t core_load_pct10[HEAPTOP_MAX_CORES];
  heaptop_region_stats_t region[HEAPTOP_REGION_COUNT];
  heaptop_alloc_stats_t alloc;
  uint16_t task_count;
  bool tasks_truncated; /**< more tasks existed than HEAPTOP_MAX_TASKS */
  heaptop_task_stats_t tasks[HEAPTOP_MAX_TASKS];
} heaptop_snapshot_t;

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_TYPES_H
