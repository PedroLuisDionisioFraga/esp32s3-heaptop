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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "heaptop_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct heaptop_config
{
  uint32_t sample_period_ms;       /**< Time between samples (>= 100 ms) */
  uint32_t task_stack;             /**< Sampler task stack, bytes */
  uint8_t task_prio;               /**< Sampler task priority; keep it low */
  int8_t task_core;                /**< Core to pin the sampler to; -1 = no affinity */
  heaptop_thresholds_t thresholds; /**< Alert limits; changeable later with heaptop_set_thresholds() */
} heaptop_config_t;

/** Alert limits taken from menuconfig (Component config > Heaptop > Alert thresholds). */
#define HEAPTOP_THRESHOLDS_DEFAULT()                           \
  {                                                            \
    .dram_free_min = CONFIG_HEAPTOP_ALERT_DRAM_FREE_MIN,       \
    .dram_largest_min = CONFIG_HEAPTOP_ALERT_DRAM_LARGEST_MIN, \
    .frag_pct_max = CONFIG_HEAPTOP_ALERT_FRAG_PCT_MAX,         \
    .psram_free_min = CONFIG_HEAPTOP_ALERT_PSRAM_FREE_MIN,     \
    .stack_hwm_min = CONFIG_HEAPTOP_ALERT_STACK_HWM_MIN,       \
    .task_growth = CONFIG_HEAPTOP_ALERT_TASK_GROWTH,           \
    .hysteresis_pct = CONFIG_HEAPTOP_ALERT_HYSTERESIS_PCT,     \
  }

/** Configuration taken from menuconfig (Component config > Heaptop). */
#define HEAPTOP_CONFIG_DEFAULT()                         \
  {                                                      \
    .sample_period_ms = CONFIG_HEAPTOP_SAMPLE_PERIOD_MS, \
    .task_stack = CONFIG_HEAPTOP_TASK_STACK,             \
    .task_prio = CONFIG_HEAPTOP_TASK_PRIO,               \
    .task_core = CONFIG_HEAPTOP_TASK_CORE,               \
    .thresholds = HEAPTOP_THRESHOLDS_DEFAULT(),          \
  }

/**
 * @brief Called when an alert turns on or off.
 *
 * Runs in the sampler task: keep it short and do not block.
 *
 * @param alert One HEAPTOP_ALERT_* bit.
 * @param s The sample that changed the alert.
 */
typedef void (*heaptop_alert_cb_t)(uint32_t alert, bool active, const heaptop_snapshot_t *s, void *ctx);

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
 * @brief Start capturing allocations that are not freed (heap_trace, HEAP_TRACE_LEAKS).
 *
 * The first call takes CONFIG_HEAPTOP_LEAK_RECORDS trace records of internal RAM
 * and keeps them until reboot. heaptop owns heap_trace while a capture runs.
 *
 * @return ESP_OK; ESP_ERR_INVALID_STATE if already running or heaptop is not
 *         initialised; ESP_ERR_NOT_SUPPORTED without CONFIG_HEAPTOP_LEAK_TRACE;
 *         ESP_ERR_NO_MEM for the record buffer.
 */
esp_err_t heaptop_leaks_start(void);

/** @brief Stop the capture; its records stay available to the report. */
esp_err_t heaptop_leaks_stop(void);

/**
 * @brief Print the capture: surviving allocations grouped by call stack, largest first.
 *
 * Works while running (a live view) or after stop. Call-stack addresses are
 * decoded to file:line by idf.py monitor.
 *
 * @param out NULL for stdout.
 * @param max_groups Rows to print.
 */
esp_err_t heaptop_leaks_report(FILE *out, size_t max_groups);

/**
 * @brief Register the alert callback; NULL removes it.
 *
 * Alerts are also logged with ESP_LOGW when they turn on.
 */
esp_err_t heaptop_set_alert_cb(heaptop_alert_cb_t cb, void *ctx);

/** @brief Current alert limits. */
esp_err_t heaptop_get_thresholds(heaptop_thresholds_t *out);

/**
 * @brief Replace the alert limits; takes effect on the next sample.
 *
 * @return ESP_OK; ESP_ERR_INVALID_ARG for NULL, frag_pct_max > 100 or hysteresis_pct > 50.
 */
esp_err_t heaptop_set_thresholds(const heaptop_thresholds_t *th);

/** @brief Remember the latest snapshot as the baseline for heaptop_diff(). */
esp_err_t heaptop_mark(void);

/**
 * @brief Print what changed since heaptop_mark(): region free bytes and per-task heap.
 *
 * @param out NULL for stdout.
 */
esp_err_t heaptop_diff(FILE *out);

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
