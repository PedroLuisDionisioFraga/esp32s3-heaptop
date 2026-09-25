/**
 * @file heaptop_emit.c
 * @brief Writes one sample of the JSON Lines stream to a FILE, line by line.
 *
 * Used by `ht stream` (console task) and by CONFIG_HEAPTOP_STREAM_AT_BOOT
 * (sampler task); each caller owns its own heaptop_emit_state_t.
 */

#include <stdio.h>
#include <string.h>

#include "heaptop.h"
#include "heaptop_priv.h"
#include "heaptop_stream.h"

static void _line(FILE *out, heaptop_buf_t *b, char *buf, size_t len)
{
  fwrite(b->p, 1, b->len, out);
  heaptop_buf_init(b, buf, len);
}

void heaptop_emit(FILE *out, const heaptop_snapshot_t *s, heaptop_emit_state_t *st, char *buf, size_t len)
{
  if (out == NULL || s == NULL || st == NULL || buf == NULL || len == 0)
    return;
  heaptop_buf_t b;
  heaptop_buf_init(&b, buf, len);

  heaptop_stream_sample(&b, s);
  _line(out, &b, buf, len);
  for (uint16_t i = 0; i < s->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    heaptop_stream_task(&b, s, i);
    _line(out, &b, buf, len);
  }

  /* Alert transitions since the previous emitted sample. */
  const uint32_t changed = s->alerts ^ st->alerts;
  if (changed)
  {
    heaptop_thresholds_t th;
    heaptop_alerts_thresholds(&th);
    for (uint32_t i = 0; i < HEAPTOP_ALERT_COUNT; i++)
    {
      const uint32_t bit = 1u << i;
      if (!(changed & bit))
        continue;
      const bool on = (s->alerts & bit) != 0;
      char msg[112] = "";
      if (on)
        heaptop_render_alert(msg, sizeof(msg), bit, s, &th);
      heaptop_stream_alert(&b, s, bit, on, msg);
      _line(out, &b, buf, len);
    }
  }
  st->alerts = s->alerts;

  /* Failures newer than the last one emitted, oldest first. */
  const uint16_t n = heaptop_fails_copy(st->fails, HEAPTOP_FAIL_LEN);
  uint64_t newest = st->last_fail_us;
  for (int i = (int)n - 1; i >= 0; i--)
  {
    if (st->fails[i].t_us <= st->last_fail_us)
      continue;
    heaptop_stream_fail(&b, s, &st->fails[i]);
    _line(out, &b, buf, len);
    if (st->fails[i].t_us > newest)
      newest = st->fails[i].t_us;
  }
  st->last_fail_us = newest;
  fflush(out);
}
