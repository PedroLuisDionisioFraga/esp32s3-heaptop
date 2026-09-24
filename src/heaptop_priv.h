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

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_PRIV_H
