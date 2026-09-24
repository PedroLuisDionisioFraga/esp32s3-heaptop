/**
 * @file heaptop.h
 * @brief htop-like heap and task monitor for ESP-IDF, driven over the serial console.
 *
 * Threading model:
 *   - heaptop_init() starts one low-priority sampler task. It is the only code
 *     that touches heaptop's internal state; it never allocates after init, so
 *     it does not disturb the heap it measures.
 *   - Every sample is published under a mutex; heaptop_get_snapshot() copies
 *     the latest one. Readers never see a half-written sample.
 *   - heaptop_console_register() adds the `ht` command to esp_console. Its
 *     subcommands run in the console task.
 *
 * Singleton: one heaptop instance per application.
 *
 * @author Pedro Luis Dionisio Fraga
 * @date 2026
 */

#ifndef HEAPTOP_H
#define HEAPTOP_H

#include <stdint.h>

#include "esp_err.h"
#include "heaptop_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct heaptop_config
{
  uint32_t sample_period_ms; /**< Time between samples (>= 100 ms) */
  uint32_t task_stack;       /**< Sampler task stack, bytes */
  uint8_t task_prio;         /**< Sampler task priority; keep it low */
  int8_t task_core;          /**< Core to pin the sampler to; -1 = no affinity */
} heaptop_config_t;

/** Configuration taken from menuconfig (Component config > Heaptop). */
#define HEAPTOP_CONFIG_DEFAULT()                         \
  {                                                      \
    .sample_period_ms = CONFIG_HEAPTOP_SAMPLE_PERIOD_MS, \
    .task_stack = CONFIG_HEAPTOP_TASK_STACK,             \
    .task_prio = CONFIG_HEAPTOP_TASK_PRIO,               \
    .task_core = CONFIG_HEAPTOP_TASK_CORE,               \
  }

/**
 * @brief Start the sampler task.
 *
 * @param config NULL uses HEAPTOP_CONFIG_DEFAULT().
 * @return ESP_OK, including a second call while already running;
 *         ESP_ERR_INVALID_ARG for a period below 100 ms;
 *         ESP_ERR_NO_MEM when the buffers or the task cannot be created.
 */
esp_err_t heaptop_init(const heaptop_config_t *config);

/**
 * @brief Stop the sampler and free every buffer. Idempotent.
 *
 * Do not call while another task is inside heaptop_get_snapshot().
 *
 * @return ESP_OK, or ESP_ERR_TIMEOUT if the sampler did not exit (nothing freed).
 */
esp_err_t heaptop_deinit(void);

/**
 * @brief Copy the latest sample.
 *
 * @param[out] out Receives the snapshot; out->seq is 0 until the first sample.
 * @return ESP_OK, ESP_ERR_INVALID_ARG for NULL, ESP_ERR_INVALID_STATE if not initialised.
 */
esp_err_t heaptop_get_snapshot(heaptop_snapshot_t *out);

/**
 * @brief Register the `ht` console command. Call after esp_console_init().
 *
 * Works with the esp_console REPL or with a custom linenoise loop.
 *
 * @return ESP_OK, ESP_ERR_NO_MEM, or the esp_console_cmd_register() error.
 */
esp_err_t heaptop_console_register(void);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_H
