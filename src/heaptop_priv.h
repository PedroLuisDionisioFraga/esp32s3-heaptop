/**
 * @file heaptop_priv.h
 * @brief Internal interfaces between heaptop's modules. Not part of the public API.
 */

#ifndef HEAPTOP_PRIV_H
#define HEAPTOP_PRIV_H

#include <stdint.h>

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

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_PRIV_H
