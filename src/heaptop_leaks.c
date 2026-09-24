/**
 * @file heaptop_leaks.c
 * @brief Leak capture on top of IDF standalone heap tracing (HEAP_TRACE_LEAKS).
 *
 * Between start and stop, heap_trace keeps one record per allocation that has
 * not been freed yet; the report groups the survivors by call stack. heap_trace
 * is global, so heaptop owns it while a capture runs. The record buffer lives in
 * internal RAM (records in PSRAM would miss allocations made from ISRs) and is
 * kept from the first start until reboot, so heap_trace never points at freed
 * memory.
 */

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "heaptop.h"
#include "heaptop_calc.h"
#include "heaptop_priv.h"
#include "heaptop_render.h"
#if CONFIG_HEAPTOP_LEAK_TRACE
#include "esp_heap_trace.h"
#endif

#define HEAPTOP_LEAK_GROUPS  32
#define HEAPTOP_REPORT_BYTES 6144

static const char *TAG = "HEAPTOP_LEAK";

typedef struct heaptop_leaks_priv
{
  /* --- Created once in heaptop_leaks_init(); program lifetime. --- */
  SemaphoreHandle_t lock;
  StaticSemaphore_t lock_mem;

  /* --- Guarded by lock. --- */
#if CONFIG_HEAPTOP_LEAK_TRACE
  heap_trace_record_t *records;
#endif
  bool running;
  int64_t start_us;
  int64_t stop_us;
} heaptop_leaks_priv_t;

static heaptop_leaks_priv_t s_leaks;

void heaptop_leaks_init(void)
{
  if (s_leaks.lock == NULL)
    s_leaks.lock = xSemaphoreCreateMutexStatic(&s_leaks.lock_mem);
}

void heaptop_leaks_shutdown(void)
{
  if (s_leaks.lock == NULL)
    return;
  xSemaphoreTake(s_leaks.lock, portMAX_DELAY);
#if CONFIG_HEAPTOP_LEAK_TRACE
  if (s_leaks.running)
  {
    heap_trace_stop();
    s_leaks.running = false;
    s_leaks.stop_us = esp_timer_get_time();
  }
#endif
  xSemaphoreGive(s_leaks.lock);
}

esp_err_t heaptop_leaks_start(void)
{
#if CONFIG_HEAPTOP_LEAK_TRACE
  if (s_leaks.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  esp_err_t err = ESP_OK;
  xSemaphoreTake(s_leaks.lock, portMAX_DELAY);
  if (s_leaks.running)
  {
    err = ESP_ERR_INVALID_STATE;
    goto out;
  }
  if (s_leaks.records == NULL)
  {
    s_leaks.records =
      heap_caps_calloc(CONFIG_HEAPTOP_LEAK_RECORDS, sizeof(heap_trace_record_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_leaks.records == NULL)
    {
      ESP_LOGE(TAG, "no internal RAM for %d trace records", CONFIG_HEAPTOP_LEAK_RECORDS);
      err = ESP_ERR_NO_MEM;
      goto out;
    }
    err = heap_trace_init_standalone(s_leaks.records, CONFIG_HEAPTOP_LEAK_RECORDS);
    if (err != ESP_OK)
      goto out;
  }
  err = heap_trace_start(HEAP_TRACE_LEAKS);
  if (err == ESP_OK)
  {
    s_leaks.running = true;
    s_leaks.start_us = esp_timer_get_time();
    s_leaks.stop_us = 0;
  }
out:
  xSemaphoreGive(s_leaks.lock);
  return err;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t heaptop_leaks_stop(void)
{
#if CONFIG_HEAPTOP_LEAK_TRACE
  if (s_leaks.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  esp_err_t err = ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_leaks.lock, portMAX_DELAY);
  if (s_leaks.running)
  {
    err = heap_trace_stop();
    s_leaks.running = false;
    s_leaks.stop_us = esp_timer_get_time();
  }
  xSemaphoreGive(s_leaks.lock);
  return err;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* Info and groups; the caller holds lock and tracing is stopped. */
static void _collect(heaptop_leak_info_t *info, heaptop_leak_group_t *groups, size_t cap, size_t *n)
{
  memset(info, 0, sizeof(*info));
  *n = 0;
#if CONFIG_HEAPTOP_LEAK_TRACE
  info->available = true;
  info->capacity = CONFIG_HEAPTOP_LEAK_RECORDS;
  if (s_leaks.records == NULL)
    return; /* never started */
  info->running = s_leaks.running;
  const int64_t end = s_leaks.running ? esp_timer_get_time() : s_leaks.stop_us;
  info->duration_ms = (uint32_t)((end - s_leaks.start_us) / 1000);

  heap_trace_summary_t sum;
  if (heap_trace_summary(&sum) == ESP_OK)
    info->overflowed = sum.has_overflowed;

  const size_t count = heap_trace_get_count();
  const size_t depth =
    CONFIG_HEAP_TRACING_STACK_DEPTH < HEAPTOP_LEAK_DEPTH ? CONFIG_HEAP_TRACING_STACK_DEPTH : HEAPTOP_LEAK_DEPTH;
  for (size_t i = 0; i < count; i++)
  {
    heap_trace_record_t rec;
    if (heap_trace_get(i, &rec) != ESP_OK)
      break;
    if (rec.freed || rec.address == NULL)
      continue;
    info->records++;
    uintptr_t pc[HEAPTOP_LEAK_DEPTH] = {0};
    for (size_t k = 0; k < depth; k++) pc[k] = (uintptr_t)rec.alloced_by[k];
    if (!heaptop_calc_leak_add(groups, cap, n, pc, depth, (uint32_t)rec.size))
      info->ungrouped++;
  }
  heaptop_calc_leak_sort(groups, *n);
#else
  (void)groups;
  (void)cap;
#endif
}

esp_err_t heaptop_leaks_report(FILE *out, size_t max_groups)
{
  if (s_leaks.lock == NULL)
    return ESP_ERR_INVALID_STATE;
  if (out == NULL)
    out = stdout;

  xSemaphoreTake(s_leaks.lock, portMAX_DELAY);

  /* heap_trace_get() walks the record list with a cursor that a concurrent free
   * can invalidate (IDF then asserts), and this report's own buffer would be
   * recorded as a leak. So a running capture is stopped for the length of the
   * report and resumed with its records intact. */
#if CONFIG_HEAPTOP_LEAK_TRACE
  const bool paused = s_leaks.running && heap_trace_stop() == ESP_OK;
#endif

  const size_t groups_bytes = HEAPTOP_LEAK_GROUPS * sizeof(heaptop_leak_group_t);
  uint8_t *mem = heap_caps_malloc(groups_bytes + HEAPTOP_REPORT_BYTES, heaptop_buffer_caps());
  esp_err_t err = ESP_OK;
  if (mem == NULL)
  {
    ESP_LOGE(TAG, "no memory for the report");
    err = ESP_ERR_NO_MEM;
    goto out;
  }
  heaptop_leak_group_t *groups = (heaptop_leak_group_t *)mem;
  char *text = (char *)mem + groups_bytes;

  bool never_started = false;
#if CONFIG_HEAPTOP_LEAK_TRACE
  never_started = s_leaks.records == NULL;
#endif
  if (never_started)
  {
    fputs("leak trace: never started; run `ht leaks start`, exercise the code, then `ht leaks stop`\n", out);
  }
  else
  {
    heaptop_leak_info_t info;
    size_t n = 0;
    _collect(&info, groups, HEAPTOP_LEAK_GROUPS, &n);
    heaptop_buf_t b;
    heaptop_buf_init(&b, text, HEAPTOP_REPORT_BYTES);
    heaptop_render_leaks(&b, &info, groups, n < max_groups ? n : max_groups);
    fwrite(b.p, 1, b.len, out);
    if (b.truncated)
      fputs("... (output truncated)\n", out);
  }
  fflush(out);
  heap_caps_free(mem);

out:
#if CONFIG_HEAPTOP_LEAK_TRACE
  if (paused)
    heap_trace_resume();
#endif
  xSemaphoreGive(s_leaks.lock);
  return err;
}
