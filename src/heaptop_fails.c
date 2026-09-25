/**
 * @file heaptop_fails.c
 * @brief The failed-allocation log.
 *
 * The callback can run in any context, including ISRs and while the flash
 * cache is disabled: it lives in IRAM, touches only this file's DRAM state,
 * never allocates or prints, and holds the spinlock for a few instructions.
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

static const char *TAG = "HEAPTOP";

typedef struct heaptop_fails_priv
{
  /* --- Written by the callback from any context, read by the sampler and
   * the console; every access holds mux. --- */
  portMUX_TYPE mux;
  uint32_t failures;
  heaptop_fail_t ring[HEAPTOP_FAIL_LEN];
  uint16_t head;
  uint16_t count;

  /* --- Written by init only. --- */
  bool registered;
} heaptop_fails_priv_t;

static heaptop_fails_priv_t s_fails = {.mux = portMUX_INITIALIZER_UNLOCKED};

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
  portENTER_CRITICAL_SAFE(&s_fails.mux);
  s_fails.ring[s_fails.head] = rec;
  s_fails.head = (uint16_t)((s_fails.head + 1u) % HEAPTOP_FAIL_LEN);
  if (s_fails.count < HEAPTOP_FAIL_LEN)
    s_fails.count++;
  s_fails.failures++;
  portEXIT_CRITICAL_SAFE(&s_fails.mux);
}

void heaptop_fails_init(void)
{
  /* IDF has one slot and no way to clear it, so the callback stays registered
   * for the program's lifetime and heaptop_deinit() leaves it alone. An
   * application that registers its own callback later takes the slot back. */
  if (s_fails.registered)
    return;
  esp_err_t err = heap_caps_register_failed_alloc_callback(_on_alloc_failed);
  s_fails.registered = err == ESP_OK;
  if (err != ESP_OK)
    ESP_LOGW(TAG, "failed-allocation callback not registered: %s", esp_err_to_name(err));
}

void heaptop_fails_sample(heaptop_snapshot_t *s)
{
  portENTER_CRITICAL_SAFE(&s_fails.mux);
  const uint32_t failures = s_fails.failures;
  portEXIT_CRITICAL_SAFE(&s_fails.mux);
  if (s_fails.registered)
    s->features |= HEAPTOP_FEAT_FAIL_CB;
  s->failures = failures;
}

void heaptop_fails_clear(void)
{
  portENTER_CRITICAL_SAFE(&s_fails.mux);
  s_fails.failures = 0;
  s_fails.head = 0;
  s_fails.count = 0;
  portEXIT_CRITICAL_SAFE(&s_fails.mux);
}

uint16_t heaptop_fails_copy(heaptop_fail_t *out, uint16_t max)
{
  if (out == NULL || max == 0)
    return 0;
  if (max > HEAPTOP_FAIL_LEN)
    max = HEAPTOP_FAIL_LEN;
  portENTER_CRITICAL_SAFE(&s_fails.mux);
  const uint16_t n = heaptop_calc_fail_copy(s_fails.ring, HEAPTOP_FAIL_LEN, s_fails.head, s_fails.count, out, max);
  portEXIT_CRITICAL_SAFE(&s_fails.mux);
  return n;
}
