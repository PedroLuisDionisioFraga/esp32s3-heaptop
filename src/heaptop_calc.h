/**
 * @file heaptop_calc.h
 * @brief Pure arithmetic behind heaptop's metrics: no FreeRTOS, no IDF, no logging.
 *
 * Every function is reentrant and host-testable. Percentages are x10 fixed point.
 */

#ifndef HEAPTOP_CALC_H
#define HEAPTOP_CALC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "heaptop_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Previous run-time counter of one task, matched by handle between samples. */
typedef struct heaptop_calc_prev
{
  uintptr_t handle;
  uint64_t counter;
} heaptop_calc_prev_t;

/** Fixed-capacity ring of 32-bit samples over caller memory. */
typedef struct heaptop_ring
{
  uint32_t *buf;
  uint16_t cap;
  uint16_t head; /* next write position */
  uint16_t count;
} heaptop_ring_t;

void heaptop_ring_init(heaptop_ring_t *r, uint32_t *buf, uint16_t cap);

void heaptop_ring_push(heaptop_ring_t *r, uint32_t v);

/** @brief Copy the newest min(count, max) values, oldest first. @return values copied. */
uint16_t heaptop_ring_copy(const heaptop_ring_t *r, uint32_t *out, uint16_t max);

/**
 * @brief Fragmentation of a region: share of free bytes outside the largest free block.
 * @return 0 (one contiguous block) .. 1000 (fully fragmented); 0 when @p free is 0.
 */
uint16_t heaptop_calc_frag_pct10(uint32_t free, uint32_t largest);

/** @brief @p part / @p whole as x10 percent, clamped to 1000; 0 when @p whole is 0. */
uint16_t heaptop_calc_pct10(uint64_t part, uint64_t whole);

/** @brief Busy share of a core from its idle task's run time: 1000 - idle%. */
uint16_t heaptop_calc_busy_pct10(uint64_t idle_delta, uint64_t wall_delta);

/**
 * @brief Look up a task's previous counter.
 * @return true and *@p counter when @p handle is in @p prev.
 */
bool heaptop_calc_prev_find(const heaptop_calc_prev_t *prev, size_t n, uintptr_t handle, uint64_t *counter);

/**
 * @brief Copy failure records from a ring, newest first.
 *
 * @param buf,cap,head,count The ring: head is the next write slot.
 * @return records copied (<= @p max).
 */
uint16_t heaptop_calc_fail_copy(const heaptop_fail_t *buf, uint16_t cap, uint16_t head, uint16_t count,
                                heaptop_fail_t *out, uint16_t max);

/** Minimum history before a task can be called a leak suspect. Shorter
 *  histories flag the burst of allocations every app makes while starting up. */
#define HEAPTOP_LEAK_MIN_SAMPLES 20
/** Minimum separate increases in that history. */
#define HEAPTOP_LEAK_MIN_RISES 3

/**
 * @brief Does a heap history (oldest first) look like a leak?
 *
 * True when there are at least HEAPTOP_LEAK_MIN_SAMPLES samples, the value grew
 * by at least @p threshold in at least HEAPTOP_LEAK_MIN_RISES separate steps,
 * grew by at least threshold/6 in each third of the window, never dropped below
 * where it started, and is within 10% of its peak. A @p threshold of 0 disables it.
 *
 * @param[out] growth Last minus first sample (may be NULL).
 */
bool heaptop_calc_leak_suspect(const uint32_t *v, size_t n, uint32_t threshold, int32_t *growth);

/** One task's heap history, keyed by task handle. */
typedef struct heaptop_growth_slot
{
  uintptr_t handle; /* 0 = free */
  uint32_t seen;    /* sample number of the last update */
  heaptop_ring_t ring;
  bool rebase;        /* heaptop_growth_clear() ran: take the next IDF peak as peak_base */
  uint32_t peak_base; /* IDF peak when the stats were cleared; 0 = never cleared */
  uint32_t peak_max;  /* highest sampled heap since the slot was claimed or cleared */
} heaptop_growth_slot_t;

/** Fixed table of per-task histories over caller memory. */
typedef struct heaptop_growth
{
  heaptop_growth_slot_t *slots;
  uint16_t n_slots;
  uint32_t *mem; /* n_slots * window values */
  uint16_t window;
} heaptop_growth_t;

void heaptop_growth_init(heaptop_growth_t *g, heaptop_growth_slot_t *slots, uint16_t n_slots, uint32_t *mem,
                         uint16_t window);

/**
 * @brief History slot for @p handle at sample @p seq.
 *
 * Reuses the task's slot, else claims a free one or one whose task was not seen
 * in the previous sample (its history restarts empty).
 *
 * @return NULL when every slot belongs to a task that is still alive.
 */
heaptop_growth_slot_t *heaptop_growth_track(heaptop_growth_t *g, uintptr_t handle, uint32_t seq);

/** @brief Empty every history and mark each slot to take a new peak baseline. */
void heaptop_growth_clear(heaptop_growth_t *g);

/**
 * @brief A task's peak heap since the stats were cleared.
 *
 * IDF only keeps the peak since boot. If it rose past @p base (its value at the
 * clear), the task set a new record after the clear and that record is exact.
 * Otherwise the highest sampled value, @p max_cur, is the best estimate.
 * With @p base 0 (never cleared) this is the IDF peak.
 */
uint32_t heaptop_calc_peak_since(uint32_t base, uint32_t max_cur, uint32_t idf_peak);

/**
 * @brief A region's minimum free size since the stats were cleared.
 *
 * The mirror of heaptop_calc_peak_since(): if IDF's since-boot minimum fell
 * below @p base (its value at the clear), that new record is exact; otherwise
 * the lowest sampled free size, @p low, is the best estimate.
 */
uint32_t heaptop_calc_min_since(uint32_t base, uint32_t low, uint32_t idf_min);

/** How far back past its limit a value must go before its alert clears, in percent. */
#define HEAPTOP_HYSTERESIS_PCT 10

/**
 * @brief Floor alert with hysteresis.
 *
 * Fires when @p value drops below @p floor; once active, clears only when the
 * value is back at floor + hyst_pct%. A @p floor of 0 is off.
 */
bool heaptop_calc_below_floor(bool active, uint32_t value, uint32_t floor, uint32_t hyst_pct);

/** @brief Ceiling alert with hysteresis: fires above @p ceiling, clears at ceiling - hyst_pct%. 0 is off. */
bool heaptop_calc_above_ceiling(bool active, uint32_t value, uint32_t ceiling, uint32_t hyst_pct);

/**
 * @brief Alert bits for a snapshot, with HEAPTOP_HYSTERESIS_PCT.
 *
 * @param active Bits active after the previous sample (for hysteresis).
 * @param prev_failures Failure count of the previous sample.
 */
uint32_t heaptop_calc_alerts(const heaptop_thresholds_t *th, const heaptop_snapshot_t *s, uint32_t active,
                             uint32_t prev_failures);

/**
 * @brief The number behind each health check, in HEAPTOP_ALERT_* bit order.
 *
 * Bytes, except frag (x10 percent) and alloc_fail (a count). stack is the lowest stack
 * high-water mark of live tasks; leak is the largest heap growth, 0 when nothing grew.
 *
 * @param[out] out HEAPTOP_ALERT_COUNT values; only the ones in the returned mask are set.
 * @return mask of the checks that have a value (a missing region or source has none).
 */
uint32_t heaptop_calc_check_values(const heaptop_snapshot_t *s, uint32_t *out);

/** @brief Fold @p v (the checks in @p have) into the running @p lo / @p hi; @p seen marks the checks with a history. */
void heaptop_calc_extremes(const uint32_t *v, uint32_t have, uint32_t *seen, uint32_t *lo, uint32_t *hi);

/** One IDF task-tracking entry, reduced to what heaptop shows. */
typedef struct heaptop_heap_owner
{
  uintptr_t handle;
  const char *name;
  bool alive;
  uint32_t cur;
  uint32_t peak;
  uint32_t psram;
} heaptop_heap_owner_t;

/**
 * @brief Merge task-tracking entries into the snapshot's task rows.
 *
 * Alive entries fill the live row with the same handle; alive entries with no
 * row (the "Pre-scheduler" bucket) are skipped. Dead entries that still hold
 * heap become their own deleted rows, even when a live task reuses the handle.
 *
 * @return false when a deleted row did not fit (s->tasks_truncated is set).
 */
bool heaptop_calc_merge_heap(heaptop_snapshot_t *s, const heaptop_heap_owner_t *owners, size_t n);

/**
 * @brief Order task indices by @p key without moving the tasks.
 *
 * Ties keep snapshot order. @p idx receives @p n indices (n <= 255).
 */
void heaptop_calc_sort_tasks(const heaptop_task_stats_t *tasks, size_t n, heaptop_sort_t key, uint8_t *idx);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_CALC_H
