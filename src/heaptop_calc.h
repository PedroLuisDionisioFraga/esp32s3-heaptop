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
