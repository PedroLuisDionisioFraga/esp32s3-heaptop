/**
 * @file heaptop_stress.c
 * @brief CPU load generator: one worker per core, busy a share of every 100 ms.
 *
 * Workers are created on first use and never delete themselves: between runs
 * they wait on a task notification, and heaptop_deinit() deletes them once they
 * have suspended (see the self-delete caveat in heaptop.c). API calls may come
 * from any task; a spinlock guards the handles, and a busy flag keeps two
 * callers from creating workers at the same time.
 */

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heaptop_priv.h"

#define HEAPTOP_STRESS_WINDOW_MS  100
#define HEAPTOP_STRESS_STACK      2048
#define HEAPTOP_STRESS_PRIO       1
#define HEAPTOP_STRESS_EXIT_POLL  20 /* ms */
#define HEAPTOP_STRESS_EXIT_TRIES 50
#define HEAPTOP_STRESS_CORES      (portNUM_PROCESSORS > HEAPTOP_MAX_CORES ? HEAPTOP_MAX_CORES : portNUM_PROCESSORS)

static const char *TAG = "HEAPTOP";

typedef struct heaptop_stress_priv
{
  /* --- Guarded by mux. --- */
  portMUX_TYPE mux;
  bool busy; /* an API call is creating or deleting workers */
  TaskHandle_t worker[HEAPTOP_MAX_CORES];

  /* --- Run parameters: written by the API before it notifies, read by the
   * workers every window. A worker that reads a half-updated set runs one
   * window with it, nothing worse. --- */
  volatile uint32_t pct;
  volatile TickType_t start;
  volatile TickType_t run_ticks; /* 0 = until stopped */
  volatile bool active;
  volatile bool exit;
} heaptop_stress_priv_t;

static heaptop_stress_priv_t s_stress = {.mux = portMUX_INITIALIZER_UNLOCKED};

static bool _expired(void)
{
  const TickType_t run = s_stress.run_ticks;
  return run != 0 && (TickType_t)(xTaskGetTickCount() - s_stress.start) >= run;
}

static void _worker(void *arg)
{
  (void)arg;
  while (!s_stress.exit)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (s_stress.active && !s_stress.exit && !_expired())
    {
      const uint32_t pct = s_stress.pct;
      const int64_t busy_until = esp_timer_get_time() + (int64_t)pct * HEAPTOP_STRESS_WINDOW_MS * 10;
      while (esp_timer_get_time() < busy_until)
      {
      }
      vTaskDelay(pdMS_TO_TICKS(HEAPTOP_STRESS_WINDOW_MS * (100u - pct) / 100u));
    }
  }
  vTaskSuspend(NULL);
}

static bool _claim(void)
{
  taskENTER_CRITICAL(&s_stress.mux);
  const bool ok = !s_stress.busy;
  s_stress.busy = true;
  taskEXIT_CRITICAL(&s_stress.mux);
  return ok;
}

static void _release(void)
{
  taskENTER_CRITICAL(&s_stress.mux);
  s_stress.busy = false;
  taskEXIT_CRITICAL(&s_stress.mux);
}

static esp_err_t _create_workers(void)
{
  for (int c = 0; c < HEAPTOP_STRESS_CORES; c++)
  {
    if (s_stress.worker[c] != NULL)
      continue;
    char name[16];
    snprintf(name, sizeof(name), "ht_stress%d", c);
    TaskHandle_t h = NULL;
    if (xTaskCreatePinnedToCore(_worker, name, HEAPTOP_STRESS_STACK, NULL, HEAPTOP_STRESS_PRIO, &h, c) != pdPASS)
    {
      ESP_LOGE(TAG, "xTaskCreate(%s) failed", name);
      return ESP_ERR_NO_MEM;
    }
    taskENTER_CRITICAL(&s_stress.mux);
    s_stress.worker[c] = h;
    taskEXIT_CRITICAL(&s_stress.mux);
  }
  return ESP_OK;
}

esp_err_t heaptop_stress_cpu(uint8_t pct, uint32_t seconds)
{
  ESP_RETURN_ON_FALSE(pct >= 1 && pct <= HEAPTOP_STRESS_MAX_PCT,
                      ESP_ERR_INVALID_ARG,
                      TAG,
                      "pct must be 1..%d",
                      HEAPTOP_STRESS_MAX_PCT);
  ESP_RETURN_ON_FALSE(seconds <= UINT32_MAX / configTICK_RATE_HZ, ESP_ERR_INVALID_ARG, TAG, "seconds too large");
  ESP_RETURN_ON_FALSE(_claim(), ESP_ERR_INVALID_STATE, TAG, "another stress call is running");

  esp_err_t err = _create_workers();
  if (err == ESP_OK)
  {
    s_stress.pct = pct;
    s_stress.start = xTaskGetTickCount();
    s_stress.run_ticks = (TickType_t)(seconds * configTICK_RATE_HZ);
    s_stress.active = true;
    for (int c = 0; c < HEAPTOP_STRESS_CORES; c++) xTaskNotifyGive(s_stress.worker[c]);
  }
  _release();
  return err;
}

esp_err_t heaptop_stress_stop(void)
{
  s_stress.active = false;
  return ESP_OK;
}

void heaptop_stress_status(heaptop_stress_status_t *out)
{
  memset(out, 0, sizeof(*out));
  taskENTER_CRITICAL(&s_stress.mux);
  for (int c = 0; c < HEAPTOP_STRESS_CORES; c++) out->workers += s_stress.worker[c] != NULL;
  taskEXIT_CRITICAL(&s_stress.mux);
  out->pct = (uint8_t)s_stress.pct;
  out->running = out->workers > 0 && s_stress.active && !_expired();
  const TickType_t run = s_stress.run_ticks;
  if (out->running && run != 0)
  {
    const TickType_t left = run - (TickType_t)(xTaskGetTickCount() - s_stress.start);
    out->left_s = (uint32_t)((left + configTICK_RATE_HZ - 1) / configTICK_RATE_HZ);
  }
}

esp_err_t heaptop_stress_deinit(void)
{
  ESP_RETURN_ON_FALSE(_claim(), ESP_ERR_INVALID_STATE, TAG, "another stress call is running");
  s_stress.active = false;
  s_stress.exit = true;
  esp_err_t err = ESP_OK;
  for (int c = 0; c < HEAPTOP_STRESS_CORES; c++)
  {
    TaskHandle_t h = s_stress.worker[c];
    if (h == NULL)
      continue;
    xTaskNotifyGive(h);
    bool parked = false;
    for (int i = 0; i < HEAPTOP_STRESS_EXIT_TRIES && !parked; i++)
    {
      parked = eTaskGetState(h) == eSuspended;
      if (!parked)
        vTaskDelay(pdMS_TO_TICKS(HEAPTOP_STRESS_EXIT_POLL));
    }
    if (!parked)
    {
      err = ESP_ERR_TIMEOUT;
      continue;
    }
    vTaskDelete(h); /* suspended, so freed here rather than by the idle task */
    taskENTER_CRITICAL(&s_stress.mux);
    s_stress.worker[c] = NULL;
    taskEXIT_CRITICAL(&s_stress.mux);
  }
  if (err == ESP_OK)
    s_stress.exit = false;
  _release();
  return err;
}
