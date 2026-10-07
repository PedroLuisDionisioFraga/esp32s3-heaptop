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

#ifdef __cplusplus
extern "C"
{
#endif

/** Task rows per snapshot. It sets the size of heaptop_snapshot_t, so to change
 *  it define it for the whole build (idf_build_set_property), never in one
 *  component only: a mismatch makes heaptop_get_snapshot() overrun buffers. */
#ifndef HEAPTOP_MAX_TASKS
#define HEAPTOP_MAX_TASKS 32
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
#define HEAPTOP_FEAT_FAIL_CB       (1u << 2) /**< Failed-allocation callback */

/** Alert bits (heaptop_snapshot_t::alerts). */
#define HEAPTOP_ALERT_DRAM_FREE    (1u << 0) /**< Internal RAM free below its floor */
#define HEAPTOP_ALERT_DRAM_LARGEST (1u << 1) /**< Largest internal free block below its floor */
#define HEAPTOP_ALERT_FRAG         (1u << 2) /**< Internal RAM fragmentation above its ceiling */
#define HEAPTOP_ALERT_PSRAM_FREE   (1u << 3) /**< PSRAM free below its floor */
#define HEAPTOP_ALERT_STACK        (1u << 4) /**< Some task's stack high-water mark below its floor */
#define HEAPTOP_ALERT_LEAK         (1u << 5) /**< Some task looks like it leaks */
#define HEAPTOP_ALERT_ALLOC_FAIL   (1u << 6) /**< An allocation failed since the previous sample */
#define HEAPTOP_ALERT_COUNT        7

/** Alert limits. A limit of 0 turns that alert off. */
typedef struct heaptop_thresholds
{
  uint32_t dram_free_min;    /**< bytes */
  uint32_t dram_largest_min; /**< bytes */
  uint32_t frag_pct_max;     /**< percent, 1..100 */
  uint32_t psram_free_min;   /**< bytes */
  uint32_t stack_hwm_min;    /**< bytes */
  uint32_t task_growth;      /**< bytes of heap growth that mark a leak suspect */
} heaptop_thresholds_t;

typedef struct heaptop_region_stats
{
  bool present;         /**< false when the region does not exist (e.g. no PSRAM) */
  uint32_t total;       /**< free + allocated */
  uint32_t free;        /**< free bytes now */
  uint32_t min_free;    /**< lowest free bytes since boot, or since heaptop_clear() */
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
  uint32_t heap_peak;  /**< peak heap held since boot, or since heaptop_clear() */
  uint32_t heap_psram; /**< part of heap_cur that lives in PSRAM */
  int32_t heap_growth; /**< heap_cur change over the history window */
} heaptop_task_stats_t;

/** One failed allocation, as reported by the IDF failed-allocation callback. */
typedef struct heaptop_fail
{
  uint64_t t_us;    /**< uptime when it failed */
  uint32_t size;    /**< bytes requested */
  uint32_t caps;    /**< MALLOC_CAP_* requested */
  const char *func; /**< heap API that failed (string literal) */
  uintptr_t task;   /**< task handle; resolve against the snapshot, never dereference */
  bool isr;         /**< failed inside an interrupt */
} heaptop_fail_t;

/** Samples of history carried in each snapshot for sparklines. */
#define HEAPTOP_TREND_LEN 40

typedef enum heaptop_trend
{
  HEAPTOP_TREND_INTERNAL_FREE = 0,
  HEAPTOP_TREND_INTERNAL_LARGEST,
  HEAPTOP_TREND_PSRAM_FREE,
  HEAPTOP_TREND_COUNT,
} heaptop_trend_t;

typedef struct heaptop_snapshot
{
  uint32_t seq; /**< increments every sample; 0 = no sample yet */
  uint64_t uptime_us;
  uint64_t since_us;  /**< uptime of the last heaptop_clear(); 0 = stats since boot */
  uint32_t period_ms; /**< configured sampling period */
  uint32_t dt_ms;     /**< real time since the previous sample */
  uint32_t self_us;   /**< time heaptop spent taking this sample */
  uint32_t features;  /**< HEAPTOP_FEAT_* */
  uint32_t alerts;    /**< active alert bits */
  uint8_t num_cores;
  uint16_t core_load_pct10[HEAPTOP_MAX_CORES];
  heaptop_region_stats_t region[HEAPTOP_REGION_COUNT];
  uint32_t failures;  /**< failed allocations since boot, or since heaptop_clear() */
  uint32_t check_seen;                     /**< bit i set when check i (HEAPTOP_ALERT_* order) has a value */
  uint32_t check_min[HEAPTOP_ALERT_COUNT]; /**< lowest value of each check since boot, or since heaptop_clear() */
  uint32_t check_max[HEAPTOP_ALERT_COUNT]; /**< highest value of each check, same window */
  uint16_t trend_len; /**< valid entries per trend series */
  uint32_t trend[HEAPTOP_TREND_COUNT][HEAPTOP_TREND_LEN]; /**< oldest first */
  uint16_t task_count;
  bool tasks_truncated; /**< more tasks existed than HEAPTOP_MAX_TASKS */
  heaptop_task_stats_t tasks[HEAPTOP_MAX_TASKS];
} heaptop_snapshot_t;

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_TYPES_H
