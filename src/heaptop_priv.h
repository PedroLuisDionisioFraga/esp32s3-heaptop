/**
 * @file heaptop_priv.h
 * @brief Internal interfaces between heaptop's modules. Not part of the public API.
 */

#ifndef HEAPTOP_PRIV_H
#define HEAPTOP_PRIV_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "heaptop.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Failed allocations kept for `ht health` and the stream. */
#define HEAPTOP_FAIL_LEN 8

/** Heap caps for heaptop's own buffers: PSRAM when present, else internal RAM. */
uint32_t heaptop_buffer_caps(void);

/** Fill snapshot regions from heap_caps_get_info(). Sampler task only. */
void heaptop_heap_sample(heaptop_snapshot_t *s);

/** Reset every heap's minimum free size to its free size now. Any task. */
void heaptop_heap_clear_min(void);

/** Bring back the since-boot minimum free sizes. Any task. */
void heaptop_heap_restore_min(void);

/** Allocate the task-sampling buffers. Called from heaptop_init(). */
esp_err_t heaptop_tasks_init(uint32_t caps);

/** Free the task-sampling buffers. Called from heaptop_deinit() after the sampler exited. */
void heaptop_tasks_deinit(void);

/** Fill tasks, CPU %, core load and per-task heap. Sampler task only. */
void heaptop_tasks_sample(heaptop_snapshot_t *s);

/** Restart leak-suspicion histories and task peaks. Sampler task only. */
void heaptop_tasks_clear(void);

/** Register the failed-allocation callback (once per boot). */
void heaptop_fails_init(void);

/** Fill the failure count and feature bit. Sampler task only. */
void heaptop_fails_sample(heaptop_snapshot_t *s);

/** Forget every logged failure. Any task. */
void heaptop_fails_clear(void);

/** Copy the logged allocation failures, newest first. Any task. */
uint16_t heaptop_fails_copy(heaptop_fail_t *out, uint16_t max);

/** Load the thresholds and reset alert state. */
esp_err_t heaptop_alerts_init(const heaptop_thresholds_t *th);

/** Current alert limits. Any task. */
void heaptop_alerts_thresholds(heaptop_thresholds_t *out);

/** Evaluate alerts into s->alerts; log and call back on transitions. Sampler task only. */
void heaptop_alerts_sample(heaptop_snapshot_t *s);

/** Take a new failure baseline after heaptop_fails_clear(). Sampler task only. */
void heaptop_alerts_clear(void);

/** While quiet, transitions are not logged (the caller owns the terminal). */
void heaptop_alerts_set_quiet(bool quiet);

/** Highest CPU stress load: the rest keeps the idle task, and its watchdog, alive. */
#define HEAPTOP_STRESS_MAX_PCT 90

/** CPU stress state for `ht stress`. */
typedef struct heaptop_stress_status
{
  bool running;
  uint8_t pct;
  uint8_t workers;
  uint32_t left_s; /**< 0 = until stopped */
} heaptop_stress_status_t;

/** Current CPU stress run. Any task. */
void heaptop_stress_status(heaptop_stress_status_t *out);

/** Stop and delete the stress workers. Called from heaptop_deinit(). */
esp_err_t heaptop_stress_deinit(void);

/** Per-writer stream state: what was already emitted. */
typedef struct heaptop_emit_state
{
  uint32_t alerts;
  uint64_t last_fail_us;
  heaptop_fail_t fails[HEAPTOP_FAIL_LEN]; /* scratch */
} heaptop_emit_state_t;

/** Write one sample (sample, tasks, alert changes, new failures) as JSON Lines. @p buf holds one line. */
void heaptop_emit(FILE *out, const heaptop_snapshot_t *s, heaptop_emit_state_t *st, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_PRIV_H
