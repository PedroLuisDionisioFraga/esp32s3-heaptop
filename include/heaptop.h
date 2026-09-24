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

#include "esp_err.h"
#include "heaptop_types.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct heaptop_config
{
  uint32_t sample_period_ms;       /**< Time between samples (>= 100 ms) */
  heaptop_thresholds_t thresholds; /**< Alert limits */
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
  }

/** Configuration taken from menuconfig (Component config > Heaptop). */
#define HEAPTOP_CONFIG_DEFAULT()                         \
  {                                                      \
    .sample_period_ms = CONFIG_HEAPTOP_SAMPLE_PERIOD_MS, \
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
 *         ESP_ERR_INVALID_ARG for a period below 100 ms or frag_pct_max above 100;
 *         ESP_ERR_NO_MEM when the buffers or the task cannot be created.
 */
esp_err_t heaptop_init(const heaptop_config_t *config);

/**
 * @brief Stop the sampler and the CPU stress workers, and free every buffer. Idempotent.
 *
 * Do not call while another task is inside heaptop_get_snapshot().
 *
 * @return ESP_OK, or ESP_ERR_TIMEOUT if a heaptop task did not exit (nothing freed).
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
 * @brief Start a fresh measurement window.
 *
 * Resets the minimum free size of every region, task peaks, the failure count
 * and log, alerts, trends and leak-suspicion history. Stack high-water marks
 * keep their since-boot minimum: FreeRTOS cannot reset them.
 *
 * Takes a sample right away and returns once it is published.
 *
 * @return ESP_OK; ESP_ERR_INVALID_STATE if not initialised; ESP_ERR_TIMEOUT if
 *         the sampler did not pick the request up.
 */
esp_err_t heaptop_clear(void);

/**
 * @brief Register the alert callback; NULL removes it.
 *
 * Alerts are also logged with ESP_LOGW when they turn on. Replacing or removing
 * the callback does not wait for a call already running in the sampler: keep
 * @p ctx valid until at least one more sample period has passed.
 */
esp_err_t heaptop_set_alert_cb(heaptop_alert_cb_t cb, void *ctx);

/**
 * @brief Load every core to @p pct percent, for benchmarks and soak tests.
 *
 * One worker task per core (`ht_stress0`, `ht_stress1`), priority 1, busy
 * @p pct ms of every 100 ms. Workers are created on first use and then wait,
 * blocked, until the next run. Calling again while running replaces the load
 * and the duration.
 *
 * @param pct 1..90: the rest keeps the idle task, and its watchdog, alive.
 * @param seconds Run time; 0 runs until heaptop_stress_stop().
 * @return ESP_OK; ESP_ERR_INVALID_ARG for pct out of range;
 *         ESP_ERR_NO_MEM when a worker cannot be created.
 */
esp_err_t heaptop_stress_cpu(uint8_t pct, uint32_t seconds);

/** @brief End the CPU load; the workers go back to waiting within 100 ms. */
esp_err_t heaptop_stress_stop(void);

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
