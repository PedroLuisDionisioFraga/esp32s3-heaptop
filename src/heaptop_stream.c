#include "heaptop_stream.h"

#include <stdio.h>

#include "heaptop_jsonutil.h"

static void _head(heaptop_buf_t *b, const char *type)
{
  heaptop_buf_printf(b, "{\"ht\":%d,\"type\":\"%s\"", HEAPTOP_STREAM_VERSION, type);
}

void heaptop_stream_sample(heaptop_buf_t *b, const heaptop_snapshot_t *s)
{
  if (b == NULL || s == NULL)
    return;
  _head(b, "sample");
  heaptop_buf_printf(b,
                     ",\"seq\":%lu,\"t_ms\":%llu,\"since_ms\":%llu,\"dt_ms\":%lu,\"self_us\":%lu",
                     (unsigned long)s->seq,
                     heaptop_jsonutil_ms(s->uptime_us),
                     heaptop_jsonutil_ms(s->since_us),
                     (unsigned long)s->dt_ms,
                     (unsigned long)s->self_us);

  if (s->features & HEAPTOP_FEAT_RUNTIME_STATS)
  {
    heaptop_buf_printf(b, ",\"cpu10\":[");
    for (int c = 0; c < s->num_cores && c < HEAPTOP_MAX_CORES; c++)
      heaptop_buf_printf(b, "%s%u", c ? "," : "", (unsigned)s->core_load_pct10[c]);
    heaptop_buf_printf(b, "]");
  }
  else
  {
    heaptop_buf_printf(b, ",\"cpu10\":null");
  }

  heaptop_buf_printf(b, ",\"regions\":{");
  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
  {
    const heaptop_region_stats_t *rs = &s->region[r];
    heaptop_buf_printf(b, "%s\"%s\":", r ? "," : "", heaptop_jsonutil_region_key(r));
    if (!rs->present)
    {
      heaptop_buf_printf(b, "null");
      continue;
    }
    heaptop_buf_printf(b,
                       "{\"total\":%lu,\"free\":%lu,\"min\":%lu,\"largest\":%lu,\"frag10\":%u,"
                       "\"used_blocks\":%lu,\"free_blocks\":%lu}",
                       (unsigned long)rs->total,
                       (unsigned long)rs->free,
                       (unsigned long)rs->min_free,
                       (unsigned long)rs->largest,
                       (unsigned)rs->frag_pct10,
                       (unsigned long)rs->used_blocks,
                       (unsigned long)rs->free_blocks);
  }
  heaptop_buf_printf(b, "}");

  if (s->features & HEAPTOP_FEAT_FAIL_CB)
    heaptop_buf_printf(b, ",\"failures\":%lu", (unsigned long)s->failures);
  else
    heaptop_buf_printf(b, ",\"failures\":null");

  heaptop_buf_printf(b, ",\"alerts\":[");
  bool first = true;
  for (uint32_t i = 0; i < HEAPTOP_ALERT_COUNT; i++)
  {
    if (s->alerts & (1u << i))
    {
      heaptop_buf_printf(b, "%s\"%s\"", first ? "" : ",", heaptop_alert_name(1u << i));
      first = false;
    }
  }
  heaptop_buf_printf(b, "]}\n");
}

void heaptop_stream_tasks(heaptop_buf_t *b, const heaptop_snapshot_t *s)
{
  if (b == NULL || s == NULL)
    return;
  for (uint16_t i = 0; i < s->task_count && i < HEAPTOP_MAX_TASKS; i++) heaptop_stream_task(b, s, i);
}

void heaptop_stream_task(heaptop_buf_t *b, const heaptop_snapshot_t *s, uint16_t i)
{
  if (b == NULL || s == NULL || i >= s->task_count || i >= HEAPTOP_MAX_TASKS)
    return;
  const bool cpu_ok = (s->features & HEAPTOP_FEAT_RUNTIME_STATS) != 0;
  const bool heap_ok = (s->features & HEAPTOP_FEAT_TASK_HEAP) != 0;
  {
    const heaptop_task_stats_t *t = &s->tasks[i];
    _head(b, "task");
    heaptop_buf_printf(b, ",\"seq\":%lu,\"name\":", (unsigned long)s->seq);
    heaptop_jsonutil_str(b, t->name);
    heaptop_buf_printf(b,
                       ",\"handle\":%lu,\"state\":\"%s\",\"prio\":%u",
                       (unsigned long)t->handle,
                       heaptop_jsonutil_state_name(t->state),
                       (unsigned)t->prio);
    if (t->core >= 0)
      heaptop_buf_printf(b, ",\"core\":%d", t->core);
    else
      heaptop_buf_printf(b, ",\"core\":null");
    if (cpu_ok)
      heaptop_buf_printf(b, ",\"cpu10\":%u", (unsigned)t->cpu_pct10);
    else
      heaptop_buf_printf(b, ",\"cpu10\":null");
    heaptop_buf_printf(b, ",\"hwm\":%lu", (unsigned long)t->stack_hwm);
    if (heap_ok)
      heaptop_buf_printf(b,
                         ",\"heap\":%lu,\"peak\":%lu,\"psram\":%lu,\"growth\":%ld,\"leak\":%s",
                         (unsigned long)t->heap_cur,
                         (unsigned long)t->heap_peak,
                         (unsigned long)t->heap_psram,
                         (long)t->heap_growth,
                         t->leak_suspect ? "true" : "false");
    else
      heaptop_buf_printf(b, ",\"heap\":null,\"peak\":null,\"psram\":null,\"growth\":null,\"leak\":null");
    heaptop_buf_printf(b, "}\n");
  }
}

void heaptop_stream_alert(heaptop_buf_t *b, const heaptop_snapshot_t *s, uint32_t alert, bool active, const char *msg)
{
  if (b == NULL || s == NULL)
    return;
  _head(b, "alert");
  heaptop_buf_printf(b,
                     ",\"t_ms\":%llu,\"alert\":\"%s\",\"active\":%s,\"msg\":",
                     heaptop_jsonutil_ms(s->uptime_us),
                     heaptop_alert_name(alert),
                     active ? "true" : "false");
  heaptop_jsonutil_str(b, msg ? msg : "");
  heaptop_buf_printf(b, "}\n");
}

void heaptop_stream_fail(heaptop_buf_t *b, const heaptop_snapshot_t *s, const heaptop_fail_t *f)
{
  if (b == NULL || s == NULL || f == NULL)
    return;
  _head(b, "fail");
  heaptop_buf_printf(b,
                     ",\"t_ms\":%llu,\"size\":%lu,\"caps\":%lu,\"func\":",
                     heaptop_jsonutil_ms(f->t_us),
                     (unsigned long)f->size,
                     (unsigned long)f->caps);
  heaptop_jsonutil_str(b, f->func ? f->func : "?");
  heaptop_buf_printf(b, ",\"task\":");
  const char *name = NULL;
  for (uint16_t i = 0; !f->isr && i < s->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    if (s->tasks[i].handle == f->task)
      name = s->tasks[i].name;
  }
  if (f->isr)
    heaptop_buf_printf(b, "null");
  else if (name)
    heaptop_jsonutil_str(b, name);
  else
    heaptop_buf_printf(b, "%lu", (unsigned long)f->task);
  heaptop_buf_printf(b, ",\"isr\":%s}\n", f->isr ? "true" : "false");
}
