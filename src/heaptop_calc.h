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

/** @brief Events per second from a count over @p dt_ms; 0 when @p dt_ms is 0. */
uint32_t heaptop_calc_rate_per_s(uint32_t delta, uint32_t dt_ms);

/** @brief Difference of two free-running 32-bit counters, correct across one wrap. */
uint32_t heaptop_calc_delta_u32(uint32_t now, uint32_t prev);

/**
 * @brief Look up a task's previous counter.
 * @return true and *@p counter when @p handle is in @p prev.
 */
bool heaptop_calc_prev_find(const heaptop_calc_prev_t *prev, size_t n, uintptr_t handle, uint64_t *counter);

/** @brief Histogram bucket of a free block: 0 (<64) .. HEAPTOP_FRAG_BUCKETS-1 (>=64K). */
uint8_t heaptop_calc_bucket(uint32_t size);

/** @brief Add one free block to a histogram. */
void heaptop_calc_hist_add(heaptop_frag_hist_t *h, uint32_t size);

/**
 * @brief Copy failure records from a ring, newest first.
 *
 * @param buf,cap,head,count The ring: head is the next write slot.
 * @return records copied (<= @p max).
 */
uint16_t heaptop_calc_fail_copy(const heaptop_fail_t *buf, uint16_t cap, uint16_t head, uint16_t count,
                                heaptop_fail_t *out, uint16_t max);

/** Minimum history before a task can be called a leak suspect. */
#define HEAPTOP_LEAK_MIN_SAMPLES 8

/**
 * @brief Add one surviving allocation to the group with the same call stack.
 *
 * @param pc Callers, innermost first; only the first min(depth, HEAPTOP_LEAK_DEPTH) are compared.
 * @return false when a new group was needed and the table is full.
 */
bool heaptop_calc_leak_add(heaptop_leak_group_t *groups, size_t cap, size_t *n, const uintptr_t *pc, size_t depth,
                           uint32_t size);

/** @brief Sort leak groups by bytes, largest first. */
void heaptop_calc_leak_sort(heaptop_leak_group_t *groups, size_t n);

/**
 * @brief Does a heap history (oldest first) look like a leak?
 *
 * True when there are at least HEAPTOP_LEAK_MIN_SAMPLES samples, the value grew
 * by at least @p threshold, never dropped below where it started, and is still
 * within 10% of its peak. A @p threshold of 0 disables the check.
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
 * @brief History ring for @p handle at sample @p seq.
 *
 * Reuses the task's slot, else claims a free one or one whose task was not seen
 * in the previous sample (its history restarts empty).
 *
 * @return NULL when every slot belongs to a task that is still alive.
 */
heaptop_ring_t *heaptop_growth_track(heaptop_growth_t *g, uintptr_t handle, uint32_t seq);

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
 * @brief Alert bits for a snapshot.
 *
 * @param active Bits active after the previous sample (for hysteresis).
 * @param prev_failures Failure count of the previous sample.
 */
uint32_t heaptop_calc_alerts(const heaptop_thresholds_t *th, const heaptop_snapshot_t *s, uint32_t active,
                             uint32_t prev_failures);

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
