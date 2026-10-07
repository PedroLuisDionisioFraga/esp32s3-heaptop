/**
 * @file heaptop_alerts.c
 * @brief Threshold alerts: evaluated by the sampler, fired once per transition.
 *
 * Thresholds are written at init; the callback by any task. The sampler reads
 * both under a spinlock (a few words). Alert state is sampler-owned.
 */

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "heaptop.h"
#include "heaptop_calc.h"
#include "heaptop_priv.h"
#include "heaptop_render.h"

#define HEAPTOP_ALERT_MSG_LEN 112

static const char *TAG = "HEAPTOP";

typedef struct heaptop_alerts_priv
{
  /* --- Written by any task, read by the sampler; guarded by mux. --- */
  portMUX_TYPE mux;
  heaptop_thresholds_t th;
  heaptop_alert_cb_t cb;
  void *cb_ctx;

  /* --- Sampler-owned. --- */
  uint32_t active;
  uint32_t prev_failures;
  bool have_prev;
  uint32_t seen; /* checks with a min/max history since boot or the last clear */
  uint32_t lo[HEAPTOP_ALERT_COUNT];
  uint32_t hi[HEAPTOP_ALERT_COUNT];

  /* --- Cross-task flag: set by `ht top` / `ht stream` while they own the terminal. --- */
  volatile bool quiet;
} heaptop_alerts_priv_t;

static heaptop_alerts_priv_t s_alerts = {.mux = portMUX_INITIALIZER_UNLOCKED};

static esp_err_t _validate(const heaptop_thresholds_t *th)
{
  ESP_RETURN_ON_FALSE(th != NULL, ESP_ERR_INVALID_ARG, TAG, "thresholds is NULL");
  ESP_RETURN_ON_FALSE(th->frag_pct_max <= 100, ESP_ERR_INVALID_ARG, TAG, "frag_pct_max must be 0..100");
  return ESP_OK;
}

esp_err_t heaptop_alerts_init(const heaptop_thresholds_t *th)
{
  ESP_RETURN_ON_ERROR(_validate(th), TAG, "invalid thresholds");
  taskENTER_CRITICAL(&s_alerts.mux);
  s_alerts.th = *th;
  taskEXIT_CRITICAL(&s_alerts.mux);
  s_alerts.active = 0;
  s_alerts.have_prev = false;
  return ESP_OK;
}

void heaptop_alerts_thresholds(heaptop_thresholds_t *out)
{
  taskENTER_CRITICAL(&s_alerts.mux);
  *out = s_alerts.th;
  taskEXIT_CRITICAL(&s_alerts.mux);
}

esp_err_t heaptop_set_alert_cb(heaptop_alert_cb_t cb, void *ctx)
{
  taskENTER_CRITICAL(&s_alerts.mux);
  s_alerts.cb = cb;
  s_alerts.cb_ctx = ctx;
  taskEXIT_CRITICAL(&s_alerts.mux);
  return ESP_OK;
}

void heaptop_alerts_clear(void)
{
  /* heaptop_fails_clear() just set the failure count to 0, so 0 is the exact
   * baseline: a failure logged before this sample reads its count still raises
   * the alert, where re-taking the baseline from that count would swallow it.
   * Level alerts follow their values, so a cleared leak history or a recovered
   * minimum turns them off on their own. */
  s_alerts.prev_failures = 0;
  s_alerts.have_prev = true;
  s_alerts.seen = 0; /* min and max start over */
}

void heaptop_alerts_set_quiet(bool quiet)
{
  s_alerts.quiet = quiet;
}

void heaptop_alerts_sample(heaptop_snapshot_t *s)
{
  heaptop_thresholds_t th;
  heaptop_alert_cb_t cb;
  void *ctx;
  taskENTER_CRITICAL(&s_alerts.mux);
  th = s_alerts.th;
  cb = s_alerts.cb;
  ctx = s_alerts.cb_ctx;
  taskEXIT_CRITICAL(&s_alerts.mux);

  /* Failures before the first sample are history, not news. */
  if (!s_alerts.have_prev)
  {
    s_alerts.prev_failures = s->failures;
    s_alerts.have_prev = true;
  }
  const uint32_t now = heaptop_calc_alerts(&th, s, s_alerts.active, s_alerts.prev_failures);
  const uint32_t rising = now & ~s_alerts.active;
  const uint32_t falling = s_alerts.active & ~now;
  s->alerts = now;
  s_alerts.active = now;

  uint32_t v[HEAPTOP_ALERT_COUNT] = {0};
  heaptop_calc_extremes(v, heaptop_calc_check_values(s, v), &s_alerts.seen, s_alerts.lo, s_alerts.hi);
  s->check_seen = s_alerts.seen;
  memcpy(s->check_min, s_alerts.lo, sizeof(s->check_min));
  memcpy(s->check_max, s_alerts.hi, sizeof(s->check_max));
  s_alerts.prev_failures = s->failures;

  for (uint32_t i = 0; i < HEAPTOP_ALERT_COUNT; i++)
  {
    const uint32_t bit = 1u << i;
    if (!((rising | falling) & bit))
      continue;
    const bool on = (rising & bit) != 0;
    if (!s_alerts.quiet)
    {
      if (on)
      {
        char msg[HEAPTOP_ALERT_MSG_LEN];
        heaptop_render_alert(msg, sizeof(msg), bit, s, &th);
        ESP_LOGW(TAG, "ALERT %s: %s", heaptop_alert_name(bit), msg);
      }
      else if (bit != HEAPTOP_ALERT_ALLOC_FAIL) /* an event, not a level: clearing is noise */
      {
        ESP_LOGI(TAG, "alert %s cleared", heaptop_alert_name(bit));
      }
    }
    if (cb)
      cb(bit, on, s, ctx);
  }
}
