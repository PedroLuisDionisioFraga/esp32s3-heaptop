/**
 * @file heaptop.c
 * @brief Sampler service: owns every heaptop buffer and publishes snapshots.
 */

#include "heaptop.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "heaptop_calc.h"
#include "heaptop_priv.h"

#define HEAPTOP_MIN_PERIOD_MS     100
#define HEAPTOP_TASK_STACK        4096
#define HEAPTOP_TASK_PRIO         1 /* low, so heaptop does not disturb what it measures */
#define HEAPTOP_STREAM_LINE_BYTES 1536
#define HEAPTOP_EXIT_POLL_MS      20
#define HEAPTOP_EXIT_POLL_TRIES   100

static const char *TAG = "HEAPTOP";

typedef struct heaptop_priv
{
  /* --- Handles. Created in init, deleted in deinit after the sampler exited. --- */
  SemaphoreHandle_t lock; /* guards latest */
  SemaphoreHandle_t wake; /* given by deinit and clear to cut the sampler's wait short */

  /* --- Config. Written once in init, read-only afterwards. --- */
  heaptop_config_t cfg;

  /* --- Sampler-owned: only the sampler task touches these after init. --- */
  heaptop_snapshot_t *work;
  uint64_t last_sample_us;
  uint64_t since_us; /* uptime of the last clear; 0 = boot */
  uint32_t seq;
  heaptop_ring_t trend[HEAPTOP_TREND_COUNT];
  uint32_t trend_mem[HEAPTOP_TREND_COUNT][HEAPTOP_TREND_LEN];
#if CONFIG_HEAPTOP_STREAM_AT_BOOT
  char *stream_line; /* one JSON line */
  heaptop_emit_state_t stream_state;
#endif

  /* --- Shared with readers, guarded by lock. --- */
  heaptop_snapshot_t *latest;

  /* --- Cross-task words: written whole by one task, read by another. --- */
  volatile bool running;   /* cleared by deinit */
  volatile bool clear_req; /* set by heaptop_clear(), cleared by the sampler once published */
  TaskHandle_t task;       /* set by init, deleted and cleared by deinit */
} heaptop_priv_t;

static heaptop_priv_t s_priv; /* zero-init; program lifetime */

uint32_t heaptop_buffer_caps(void)
{
#if CONFIG_SPIRAM
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0)
    return MALLOC_CAP_SPIRAM;
#endif
  return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
}

static void _init_trends(void)
{
  for (int k = 0; k < HEAPTOP_TREND_COUNT; k++)
    heaptop_ring_init(&s_priv.trend[k], s_priv.trend_mem[k], HEAPTOP_TREND_LEN);
}

static void _push_trends(heaptop_snapshot_t *w)
{
  heaptop_ring_push(&s_priv.trend[HEAPTOP_TREND_INTERNAL_FREE], w->region[HEAPTOP_REGION_INTERNAL].free);
  heaptop_ring_push(&s_priv.trend[HEAPTOP_TREND_INTERNAL_LARGEST], w->region[HEAPTOP_REGION_INTERNAL].largest);
  heaptop_ring_push(&s_priv.trend[HEAPTOP_TREND_PSRAM_FREE], w->region[HEAPTOP_REGION_PSRAM].free);
  for (int k = 0; k < HEAPTOP_TREND_COUNT; k++)
    w->trend_len = heaptop_ring_copy(&s_priv.trend[k], w->trend[k], HEAPTOP_TREND_LEN);
}

/* Everything with a "since" in it starts over. */
static void _apply_clear(uint64_t now_us)
{
  _init_trends();
  heaptop_heap_clear();
  heaptop_tasks_clear();
  heaptop_fails_clear();
  heaptop_alerts_clear();
  s_priv.since_us = now_us;
}

static void _sample_once(void)
{
  heaptop_snapshot_t *w = s_priv.work;
  const uint64_t t0 = (uint64_t)esp_timer_get_time();
  const bool clearing = s_priv.clear_req;
  if (clearing)
    _apply_clear(t0);

  memset(w, 0, sizeof(*w));
  w->uptime_us = t0;
  w->since_us = s_priv.since_us;
  w->dt_ms = s_priv.last_sample_us ? (uint32_t)((t0 - s_priv.last_sample_us) / 1000u) : 0;
  s_priv.last_sample_us = t0;
  w->num_cores = portNUM_PROCESSORS > HEAPTOP_MAX_CORES ? HEAPTOP_MAX_CORES : portNUM_PROCESSORS;

  w->period_ms = s_priv.cfg.sample_period_ms;

  heaptop_heap_sample(w);
  heaptop_tasks_sample(w);
  heaptop_fails_sample(w);
  heaptop_alerts_sample(w);
  _push_trends(w);

  w->seq = ++s_priv.seq;
  w->self_us = (uint32_t)((uint64_t)esp_timer_get_time() - t0);

  xSemaphoreTake(s_priv.lock, portMAX_DELAY);
  memcpy(s_priv.latest, w, sizeof(*w));
  xSemaphoreGive(s_priv.lock);
  if (clearing)
    s_priv.clear_req = false;

#if CONFIG_HEAPTOP_STREAM_AT_BOOT
  heaptop_emit(stdout, w, &s_priv.stream_state, s_priv.stream_line, HEAPTOP_STREAM_LINE_BYTES);
#endif
}

static void _heaptop_task(void *arg)
{
  (void)arg;
  const TickType_t period = pdMS_TO_TICKS(s_priv.cfg.sample_period_ms);

  while (s_priv.running)
  {
    _sample_once();
    xSemaphoreTake(s_priv.wake, period);
  }

  /* Deinit deletes this task once it is suspended. Deleting itself would leave
   * the free of its stack to the idle task, and with heap task tracking that
   * free waits on a mutex: if another task holds it, the idle task blocks and
   * FreeRTOS asserts (prvSelectHighestPriorityTaskSMP). */
  vTaskSuspend(NULL);
}

static void _free_buffers(void)
{
  heaptop_tasks_deinit();
  heap_caps_free(s_priv.work);
  heap_caps_free(s_priv.latest);
  s_priv.work = NULL;
  s_priv.latest = NULL;
#if CONFIG_HEAPTOP_STREAM_AT_BOOT
  heap_caps_free(s_priv.stream_line);
  s_priv.stream_line = NULL;
#endif
  if (s_priv.wake)
    vSemaphoreDelete(s_priv.wake);
  if (s_priv.lock)
    vSemaphoreDelete(s_priv.lock);
  s_priv.wake = NULL;
  s_priv.lock = NULL;
}

esp_err_t heaptop_init(const heaptop_config_t *config)
{
  if (s_priv.running)
    return ESP_OK;

  const heaptop_config_t defaults = HEAPTOP_CONFIG_DEFAULT();
  const heaptop_config_t cfg = config ? *config : defaults;
  ESP_RETURN_ON_FALSE(cfg.sample_period_ms >= HEAPTOP_MIN_PERIOD_MS,
                      ESP_ERR_INVALID_ARG,
                      TAG,
                      "sample_period_ms must be >= %d",
                      HEAPTOP_MIN_PERIOD_MS);
  ESP_RETURN_ON_ERROR(heaptop_alerts_init(&cfg.thresholds), TAG, "invalid thresholds");
  s_priv.cfg = cfg;
  s_priv.seq = 0;
  s_priv.last_sample_us = 0;
  s_priv.since_us = 0;
  s_priv.clear_req = false;
  _init_trends();
  heaptop_heap_init();

  const uint32_t caps = heaptop_buffer_caps();
  const size_t free_before = heap_caps_get_free_size(caps);
  esp_err_t err = ESP_ERR_NO_MEM;
  s_priv.lock = xSemaphoreCreateMutex();
  s_priv.wake = xSemaphoreCreateBinary();
  s_priv.work = heap_caps_calloc(1, sizeof(heaptop_snapshot_t), caps);
  s_priv.latest = heap_caps_calloc(1, sizeof(heaptop_snapshot_t), caps);
  bool ok = s_priv.lock && s_priv.wake && s_priv.work && s_priv.latest;
#if CONFIG_HEAPTOP_STREAM_AT_BOOT
  memset(&s_priv.stream_state, 0, sizeof(s_priv.stream_state));
  s_priv.stream_line = heap_caps_malloc(HEAPTOP_STREAM_LINE_BYTES, caps);
  ok = ok && s_priv.stream_line;
#endif
  if (!ok)
  {
    ESP_LOGE(TAG, "no memory for snapshot buffers");
    goto fail;
  }
  err = heaptop_tasks_init(caps);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "task buffers: %s", esp_err_to_name(err));
    goto fail;
  }

  heaptop_fails_init();

  s_priv.running = true;
  TaskHandle_t task = NULL;
  if (xTaskCreate(_heaptop_task, "heaptop", HEAPTOP_TASK_STACK, NULL, HEAPTOP_TASK_PRIO, &task) != pdPASS)
  {
    ESP_LOGE(TAG, "xTaskCreate(heaptop) failed");
    s_priv.running = false;
    err = ESP_ERR_NO_MEM;
    goto fail;
  }
  s_priv.task = task;

  /* Measured, allocator overhead included; other tasks allocating meanwhile can skew it. */
  const size_t free_after = heap_caps_get_free_size(caps);
  ESP_LOGI(TAG,
           "started: every %" PRIu32 " ms, up to %d tasks, %u bytes of buffers in %s",
           cfg.sample_period_ms,
           HEAPTOP_MAX_TASKS,
           (unsigned)(free_before > free_after ? free_before - free_after : 0),
           (caps & MALLOC_CAP_SPIRAM) ? "PSRAM" : "internal RAM");
  return ESP_OK;

fail:
  _free_buffers();
  return err;
}

esp_err_t heaptop_deinit(void)
{
  ESP_RETURN_ON_ERROR(heaptop_stress_deinit(), TAG, "stress workers did not exit");
  if (!s_priv.running)
    return ESP_OK;

  s_priv.running = false;
  xSemaphoreGive(s_priv.wake);
  bool parked = false;
  for (int i = 0; i < HEAPTOP_EXIT_POLL_TRIES && !parked; i++)
  {
    parked = eTaskGetState(s_priv.task) == eSuspended;
    if (!parked)
      vTaskDelay(pdMS_TO_TICKS(HEAPTOP_EXIT_POLL_MS));
  }
  if (!parked)
  {
    ESP_LOGE(TAG, "sampler did not exit; buffers kept");
    return ESP_ERR_TIMEOUT;
  }
  vTaskDelete(s_priv.task); /* suspended, so freed here rather than by the idle task */
  s_priv.task = NULL;

  _free_buffers();
  ESP_LOGI(TAG, "stopped");
  return ESP_OK;
}

esp_err_t heaptop_get_snapshot(heaptop_snapshot_t *out)
{
  ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out is NULL");
  /* running is set only once every buffer exists, and cleared before any is freed. */
  if (!s_priv.running)
    return ESP_ERR_INVALID_STATE;

  xSemaphoreTake(s_priv.lock, portMAX_DELAY);
  memcpy(out, s_priv.latest, sizeof(*out));
  xSemaphoreGive(s_priv.lock);
  return ESP_OK;
}

esp_err_t heaptop_clear(void)
{
  if (!s_priv.running)
    return ESP_ERR_INVALID_STATE;
  /* From the alert callback the sampler would be waiting on itself. */
  ESP_RETURN_ON_FALSE(xTaskGetCurrentTaskHandle() != s_priv.task,
                      ESP_ERR_INVALID_STATE,
                      TAG,
                      "heaptop_clear() cannot run in the alert callback");

  s_priv.clear_req = true;
  xSemaphoreGive(s_priv.wake);
  for (int i = 0; i < HEAPTOP_EXIT_POLL_TRIES; i++)
  {
    if (!s_priv.clear_req)
      return ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(HEAPTOP_EXIT_POLL_MS));
  }
  return ESP_ERR_TIMEOUT;
}
