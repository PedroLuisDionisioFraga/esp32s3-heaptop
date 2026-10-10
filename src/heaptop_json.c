#include "heaptop_json.h"

#include "heaptop_jsonutil.h"
#include "heaptop_render.h"
#include "heaptop_stream.h"

/* The text is built here and handed to the sink whenever the next piece might not fit. */
#define HEAPTOP_JSON_BUF_BYTES 512

/* Most bytes one piece of the document can take, with every number at its widest and a task name made only of
 * escaped characters. A piece is never split across two chunks, so every reserve must hold its piece. The host test
 * writes a snapshot at its maximum to keep these honest. */
#define PIECE_HEAD   224 /* up to 197: head, with both cores' load */
#define PIECE_REGION 192 /* up to 154: one region object and its comma */
#define PIECE_HEALTH 160 /* up to 110: end of regions, failures, every alert name */
#define PIECE_LIMITS 224 /* up to 174 */
#define PIECE_TASK   288 /* up to 264: one task object with its heap fields, and its comma */
#define PIECE_NUMBER 12  /* a trend value and its comma */
#define PIECE_SMALL  64  /* section headers and closers */

typedef struct json_out
{
  heaptop_json_sink_t sink;
  void *ctx;
  bool ok; /* false once the sink stopped or a piece outgrew its reserve; everything after is skipped */
  heaptop_buf_t buf;
  char mem[HEAPTOP_JSON_BUF_BYTES];
} json_out_t;

/* Hand the text built so far to the sink and start over. */
static void _flush(json_out_t *o)
{
  if (!o->ok)
    return;
  if (o->buf.truncated)
    o->ok = false; /* a piece outgrew its reserve: the text is cut, so it must not go out as if it were whole */
  else if (o->buf.len > 0 && !o->sink(o->buf.p, o->buf.len, o->ctx))
    o->ok = false;
  heaptop_buf_init(&o->buf, o->mem, sizeof(o->mem));
}

/* Make room for a piece of up to @p need bytes, flushing first if it might not fit. False once the document failed. */
static bool _room(json_out_t *o, size_t need)
{
  if (o->ok && o->buf.cap - o->buf.len <= need)
    _flush(o);
  return o->ok;
}

static void _head(json_out_t *o, const heaptop_snapshot_t *s)
{
  if (!_room(o, PIECE_HEAD))
    return;
  heaptop_buf_t *b = &o->buf;
  heaptop_buf_printf(b,
                     "{\"ht\":%d,\"seq\":%lu,\"t_ms\":%llu,\"since_ms\":%llu,\"period_ms\":%lu,\"dt_ms\":%lu,"
                     "\"self_us\":%lu,\"features\":%lu,",
                     HEAPTOP_STREAM_VERSION,
                     (unsigned long)s->seq,
                     heaptop_jsonutil_ms(s->uptime_us),
                     heaptop_jsonutil_ms(s->since_us),
                     (unsigned long)s->period_ms,
                     (unsigned long)s->dt_ms,
                     (unsigned long)s->self_us,
                     (unsigned long)s->features);

  if (s->features & HEAPTOP_FEAT_RUNTIME_STATS)
  {
    heaptop_buf_printf(b, "\"cpu10\":[");
    for (int c = 0; c < s->num_cores && c < HEAPTOP_MAX_CORES; c++)
      heaptop_buf_printf(b, "%s%u", c ? "," : "", (unsigned)s->core_load_pct10[c]);
    heaptop_buf_printf(b, "],");
  }
  else
  {
    heaptop_buf_printf(b, "\"cpu10\":null,");
  }
  heaptop_buf_printf(b, "\"regions\":{");
}

static void _regions(json_out_t *o, const heaptop_snapshot_t *s)
{
  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
  {
    if (!_room(o, PIECE_REGION))
      return;
    const heaptop_region_stats_t *rs = &s->region[r];
    heaptop_buf_printf(&o->buf, "%s\"%s\":", r ? "," : "", heaptop_jsonutil_region_key(r));
    if (!rs->present)
    {
      heaptop_buf_printf(&o->buf, "null");
      continue;
    }
    heaptop_buf_printf(&o->buf,
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
}

/* Closes "regions", then writes the failure count and the names of the active alerts. */
static void _health(json_out_t *o, const heaptop_snapshot_t *s)
{
  if (!_room(o, PIECE_HEALTH))
    return;
  heaptop_buf_t *b = &o->buf;
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
  heaptop_buf_printf(b, "]");
}

static void _limits(json_out_t *o, const heaptop_thresholds_t *th)
{
  if (!_room(o, PIECE_LIMITS))
    return;
  heaptop_buf_printf(&o->buf,
                     ",\"limits\":{\"dram_free_min\":%lu,\"dram_largest_min\":%lu,\"frag_pct_max\":%lu,"
                     "\"psram_free_min\":%lu,\"stack_hwm_min\":%lu,\"task_growth\":%lu}",
                     (unsigned long)th->dram_free_min,
                     (unsigned long)th->dram_largest_min,
                     (unsigned long)th->frag_pct_max,
                     (unsigned long)th->psram_free_min,
                     (unsigned long)th->stack_hwm_min,
                     (unsigned long)th->task_growth);
}

static void _series(json_out_t *o, const char *key, const uint32_t *v, uint16_t n)
{
  if (!_room(o, PIECE_SMALL))
    return;
  heaptop_buf_printf(&o->buf, ",\"%s\":[", key);
  for (uint16_t i = 0; i < n; i++)
  {
    if (!_room(o, PIECE_NUMBER))
      return;
    heaptop_buf_printf(&o->buf, "%s%lu", i ? "," : "", (unsigned long)v[i]);
  }
  if (_room(o, 1))
    heaptop_buf_printf(&o->buf, "]");
}

static void _trend(json_out_t *o, const heaptop_snapshot_t *s)
{
  const uint16_t n = s->trend_len < HEAPTOP_TREND_LEN ? s->trend_len : HEAPTOP_TREND_LEN;
  if (!_room(o, PIECE_SMALL))
    return;
  heaptop_buf_printf(&o->buf, ",\"trend\":{\"len\":%u", (unsigned)n);
  _series(o, "internal_free", s->trend[HEAPTOP_TREND_INTERNAL_FREE], n);
  _series(o, "internal_largest", s->trend[HEAPTOP_TREND_INTERNAL_LARGEST], n);
  if (s->region[HEAPTOP_REGION_PSRAM].present)
    _series(o, "psram_free", s->trend[HEAPTOP_TREND_PSRAM_FREE], n);
  if (_room(o, 1))
    heaptop_buf_printf(&o->buf, "}");
}

static void _tasks(json_out_t *o, const heaptop_snapshot_t *s)
{
  const bool cpu_ok = (s->features & HEAPTOP_FEAT_RUNTIME_STATS) != 0;
  const bool heap_ok = (s->features & HEAPTOP_FEAT_TASK_HEAP) != 0;

  if (!_room(o, PIECE_SMALL))
    return;
  heaptop_buf_printf(&o->buf, ",\"tasks\":[");
  for (uint16_t i = 0; i < s->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    if (!_room(o, PIECE_TASK))
      return;
    heaptop_buf_t *b = &o->buf;
    const heaptop_task_stats_t *t = &s->tasks[i];
    heaptop_buf_printf(b, "%s{\"name\":", i ? "," : "");
    heaptop_jsonutil_str(b, t->name);
    heaptop_buf_printf(b, ",\"state\":\"%s\",\"prio\":%u", heaptop_jsonutil_state_name(t->state), (unsigned)t->prio);
    if (t->core >= 0)
      heaptop_buf_printf(b, ",\"core\":%d", (int)t->core);
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
    heaptop_buf_printf(b, "}");
  }

  if (!_room(o, PIECE_SMALL))
    return;
  heaptop_buf_printf(&o->buf, "],\"tasks_truncated\":%s", s->tasks_truncated ? "true" : "false");
}

bool heaptop_json_snapshot(const heaptop_snapshot_t *s, const heaptop_thresholds_t *limits, uint32_t flags,
                           heaptop_json_sink_t sink, void *ctx)
{
  if (s == NULL || sink == NULL)
    return false;

  json_out_t o;
  o.sink = sink;
  o.ctx = ctx;
  o.ok = true;
  heaptop_buf_init(&o.buf, o.mem, sizeof(o.mem));

  _head(&o, s);
  _regions(&o, s);
  _health(&o, s);
  if (limits != NULL)
    _limits(&o, limits);
  if (flags & HEAPTOP_JSON_TRENDS)
    _trend(&o, s);
  if (flags & HEAPTOP_JSON_TASKS)
    _tasks(&o, s);
  if (_room(&o, PIECE_SMALL))
    heaptop_buf_printf(&o.buf, "}");
  _flush(&o);
  return o.ok;
}
