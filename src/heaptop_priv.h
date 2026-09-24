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

/** Heap caps for heaptop's own buffers: PSRAM when configured and present, else internal RAM. */
uint32_t heaptop_buffer_caps(void);

/** Fill snapshot regions from heap_caps_get_info(). Sampler task only. */
void heaptop_heap_sample(heaptop_snapshot_t *s);

/** Allocate the task-sampling buffers. Called from heaptop_init(). */
esp_err_t heaptop_tasks_init(uint32_t caps);

/** Free the task-sampling buffers. Called from heaptop_deinit() after the sampler exited. */
void heaptop_tasks_deinit(void);

/** Fill tasks, CPU %, core load and per-task heap. Sampler task only. */
void heaptop_tasks_sample(heaptop_snapshot_t *s);

/** Free-block histogram of one region; walks the heap under its lock. Any task. */
void heaptop_heap_histogram(heaptop_region_t region, heaptop_frag_hist_t *h);

/** Register the failed-allocation callback (once per boot). */
void heaptop_hooks_init(void);

/** Fill allocation rates, failure count and feature bits; needs s->dt_ms. Sampler task only. */
void heaptop_hooks_sample(heaptop_snapshot_t *s);

/** Copy the logged allocation failures, newest first. Any task. */
uint16_t heaptop_hooks_failures(heaptop_fail_t *out, uint16_t max);

/** Create the leak-capture lock (once). */
void heaptop_leaks_init(void);

/** Stop a running leak capture; the record buffer is kept. */
void heaptop_leaks_shutdown(void);

/** Load the thresholds and reset alert state. */
esp_err_t heaptop_alerts_init(const heaptop_thresholds_t *th);

/** Evaluate alerts into s->alerts; log and call back on transitions. Sampler task only. */
void heaptop_alerts_sample(heaptop_snapshot_t *s);

/** Current leak-suspicion growth threshold, bytes. */
uint32_t heaptop_alerts_task_growth(void);

/** While quiet, transitions are not logged (the caller owns the terminal). */
void heaptop_alerts_set_quiet(bool quiet);

#if CONFIG_HEAPTOP_FAILED_ALLOC_CALLBACK
#define HEAPTOP_EMIT_FAILS CONFIG_HEAPTOP_FAIL_RING_LEN
#else
#define HEAPTOP_EMIT_FAILS 1
#endif

/** Per-writer stream state: what was already emitted. */
typedef struct heaptop_emit_state
{
  uint32_t alerts;
  uint64_t last_fail_us;
  heaptop_fail_t fails[HEAPTOP_EMIT_FAILS]; /* scratch */
} heaptop_emit_state_t;

/** Write one sample (sample, tasks, alert changes, new failures) as JSON Lines. @p buf holds one line. */
void heaptop_emit(FILE *out, const heaptop_snapshot_t *s, heaptop_emit_state_t *st, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_PRIV_H
