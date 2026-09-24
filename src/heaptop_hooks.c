/**
 * @file heaptop_hooks.c
 * @brief Allocation counters (heap hooks) and the failed-allocation log.
 *
 * The hooks and the failure callback can run in any context, including ISRs
 * and while the flash cache is disabled: they live in IRAM, touch only this
 * file's DRAM state, never allocate or print, and hold the spinlock for a few
 * instructions. The sampler turns the counters into per-second rates.
 */

#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heaptop_calc.h"
#include "heaptop_priv.h"

#if CONFIG_HEAPTOP_FAILED_ALLOC_CALLBACK
#define HEAPTOP_FAIL_LEN CONFIG_HEAPTOP_FAIL_RING_LEN
#else
#define HEAPTOP_FAIL_LEN 1
#endif

static const char *TAG = "HEAPTOP";

typedef struct heaptop_hooks_priv
{
  /* --- Written by the hooks/callback from any context, read by the sampler and
   * the console; every access holds mux. --- */
  portMUX_TYPE mux;
  uint32_t allocs; /* free-running; consumers use wrap-safe deltas */
  uint32_t frees;
  uint32_t bytes;
  uint32_t failures;
  heaptop_fail_t ring[HEAPTOP_FAIL_LEN];
  uint16_t head;
  uint16_t count;

  /* --- Sampler-owned. --- */
  uint32_t prev_allocs;
  uint32_t prev_frees;
  uint32_t prev_bytes;
  bool have_prev;
  bool fail_cb_registered;
} heaptop_hooks_priv_t;

static heaptop_hooks_priv_t s_hooks = {.mux = portMUX_INITIALIZER_UNLOCKED};

#if CONFIG_HEAPTOP_ALLOC_HOOKS
/* Definitions of the heap component's hooks. They inherit IRAM placement from
 * the HEAP_IRAM_ATTR declaration in esp_heap_caps.h. The in-place realloc path
 * calls the alloc hook without a matching free, so allocs slightly overcount
 * frees on realloc-heavy code. */
void esp_heap_trace_alloc_hook(void *ptr, size_t size, uint32_t caps)
{
  (void)ptr;
  (void)caps;
  portENTER_CRITICAL_SAFE(&s_hooks.mux);
  s_hooks.allocs++;
  s_hooks.bytes += (uint32_t)size;
  portEXIT_CRITICAL_SAFE(&s_hooks.mux);
}

void esp_heap_trace_free_hook(void *ptr)
{
  (void)ptr;
  portENTER_CRITICAL_SAFE(&s_hooks.mux);
  s_hooks.frees++;
  portEXIT_CRITICAL_SAFE(&s_hooks.mux);
}
#endif

#if CONFIG_HEAPTOP_FAILED_ALLOC_CALLBACK
static IRAM_ATTR void _on_alloc_failed(size_t size, uint32_t caps, const char *function_name)
{
  const bool isr = xPortInIsrContext();
  const heaptop_fail_t rec = {
    .t_us = (uint64_t)esp_timer_get_time(),
    .size = (uint32_t)size,
    .caps = caps,
    .func = function_name,
    .task = isr ? 0 : (uintptr_t)xTaskGetCurrentTaskHandle(),
    .isr = isr,
  };
  portENTER_CRITICAL_SAFE(&s_hooks.mux);
  s_hooks.ring[s_hooks.head] = rec;
  s_hooks.head = (uint16_t)((s_hooks.head + 1u) % HEAPTOP_FAIL_LEN);
  if (s_hooks.count < HEAPTOP_FAIL_LEN)
    s_hooks.count++;
  s_hooks.failures++;
  portEXIT_CRITICAL_SAFE(&s_hooks.mux);
}
#endif

void heaptop_hooks_init(void)
{
  s_hooks.have_prev = false;
#if CONFIG_HEAPTOP_FAILED_ALLOC_CALLBACK
  /* There is only one slot and it cannot be cleared, so it stays registered for
   * the program's lifetime and heaptop_deinit() leaves it alone. */
  if (!s_hooks.fail_cb_registered)
  {
    esp_err_t err = heap_caps_register_failed_alloc_callback(_on_alloc_failed);
    s_hooks.fail_cb_registered = err == ESP_OK;
    if (err != ESP_OK)
      ESP_LOGW(TAG, "failed-allocation callback not registered: %s", esp_err_to_name(err));
  }
#endif
}

void heaptop_hooks_sample(heaptop_snapshot_t *s)
{
  portENTER_CRITICAL_SAFE(&s_hooks.mux);
  const uint32_t allocs = s_hooks.allocs;
  const uint32_t frees = s_hooks.frees;
  const uint32_t bytes = s_hooks.bytes;
  const uint32_t failures = s_hooks.failures;
  portEXIT_CRITICAL_SAFE(&s_hooks.mux);

#if CONFIG_HEAPTOP_ALLOC_HOOKS
  s->features |= HEAPTOP_FEAT_ALLOC_HOOKS;
  if (s_hooks.have_prev)
  {
    s->alloc.allocs_per_s = heaptop_calc_rate_per_s(heaptop_calc_delta_u32(allocs, s_hooks.prev_allocs), s->dt_ms);
    s->alloc.frees_per_s = heaptop_calc_rate_per_s(heaptop_calc_delta_u32(frees, s_hooks.prev_frees), s->dt_ms);
    s->alloc.bytes_per_s = heaptop_calc_rate_per_s(heaptop_calc_delta_u32(bytes, s_hooks.prev_bytes), s->dt_ms);
  }
#endif
  s_hooks.prev_allocs = allocs;
  s_hooks.prev_frees = frees;
  s_hooks.prev_bytes = bytes;
  s_hooks.have_prev = true;

  if (s_hooks.fail_cb_registered)
    s->features |= HEAPTOP_FEAT_FAIL_CB;
  s->alloc.failures = failures;
}

uint16_t heaptop_hooks_failures(heaptop_fail_t *out, uint16_t max)
{
  if (out == NULL || max == 0)
    return 0;
  if (max > HEAPTOP_FAIL_LEN)
    max = HEAPTOP_FAIL_LEN;
  portENTER_CRITICAL_SAFE(&s_hooks.mux);
  const uint16_t n = heaptop_calc_fail_copy(s_hooks.ring, HEAPTOP_FAIL_LEN, s_hooks.head, s_hooks.count, out, max);
  portEXIT_CRITICAL_SAFE(&s_hooks.mux);
  return n;
}
