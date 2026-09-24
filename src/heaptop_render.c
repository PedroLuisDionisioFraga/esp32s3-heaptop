#include "heaptop_render.h"

#include <stdarg.h>
#include <stdio.h>

#include "heaptop_calc.h"

static const char *const s_region_names[HEAPTOP_REGION_COUNT] = {"internal", "dma", "psram"};

void heaptop_buf_init(heaptop_buf_t *b, char *mem, size_t cap)
{
  if (b == NULL)
    return;
  b->p = mem;
  b->cap = mem ? cap : 0;
  b->len = 0;
  b->truncated = false;
  if (b->cap > 0)
    b->p[0] = '\0';
}

void heaptop_buf_printf(heaptop_buf_t *b, const char *fmt, ...)
{
  if (b == NULL || fmt == NULL || b->cap == 0)
    return;
  size_t room = b->cap - b->len;
  if (room <= 1)
  {
    b->truncated = true;
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b->p + b->len, room, fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  if ((size_t)n >= room)
  {
    b->len = b->cap - 1;
    b->truncated = true;
  }
  else
  {
    b->len += (size_t)n;
  }
}

void heaptop_fmt_bytes(char *out, size_t len, uint32_t bytes)
{
  if (out == NULL || len == 0)
    return;
  if (bytes < 1024u)
  {
    snprintf(out, len, "%u", (unsigned)bytes);
    return;
  }
  const bool mega = bytes >= 1024u * 1024u;
  const uint64_t unit = mega ? 1024u * 1024u : 1024u;
  const unsigned tenths = (unsigned)(((uint64_t)bytes * 10u) / unit);
  snprintf(out, len, "%u.%u%c", tenths / 10u, tenths % 10u, mega ? 'M' : 'K');
}

static void _pct(char *out, size_t len, uint16_t pct10)
{
  snprintf(out, len, "%u.%u%%", pct10 / 10u, pct10 % 10u);
}

void heaptop_render_heap(heaptop_buf_t *b, const heaptop_snapshot_t *s)
{
  if (b == NULL || s == NULL)
    return;
  heaptop_buf_printf(b,
                     "%-9s %8s %8s %9s %8s %6s  %s\n",
                     "REGION",
                     "TOTAL",
                     "FREE",
                     "MIN FREE",
                     "LARGEST",
                     "FRAG",
                     "BLOCKS used/free");
  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
  {
    const heaptop_region_stats_t *rs = &s->region[r];
    if (!rs->present)
    {
      heaptop_buf_printf(b, "%-9s not present\n", s_region_names[r]);
      continue;
    }
    char total[12], free_[12], min[12], largest[12], frag[10];
    heaptop_fmt_bytes(total, sizeof(total), rs->total);
    heaptop_fmt_bytes(free_, sizeof(free_), rs->free);
    heaptop_fmt_bytes(min, sizeof(min), rs->min_free);
    heaptop_fmt_bytes(largest, sizeof(largest), rs->largest);
    _pct(frag, sizeof(frag), rs->frag_pct10);
    heaptop_buf_printf(b,
                       "%-9s %8s %8s %9s %8s %6s  %u/%u\n",
                       s_region_names[r],
                       total,
                       free_,
                       min,
                       largest,
                       frag,
                       (unsigned)rs->used_blocks,
                       (unsigned)rs->free_blocks);
  }
}

static char _state_char(uint8_t state)
{
  switch (state)
  {
    case HEAPTOP_TASK_RUNNING:
      return 'R';
    case HEAPTOP_TASK_READY:
      return 'Y';
    case HEAPTOP_TASK_BLOCKED:
      return 'B';
    case HEAPTOP_TASK_SUSPENDED:
      return 'S';
    case HEAPTOP_TASK_DELETED:
      return 'X';
    default:
      return '?';
  }
}

void heaptop_render_tasks(heaptop_buf_t *b, const heaptop_snapshot_t *s, heaptop_sort_t key)
{
  if (b == NULL || s == NULL)
    return;
  const bool cpu_ok = (s->features & HEAPTOP_FEAT_RUNTIME_STATS) != 0;
  const bool heap_ok = (s->features & HEAPTOP_FEAT_TASK_HEAP) != 0;
  const size_t n = s->task_count > HEAPTOP_MAX_TASKS ? HEAPTOP_MAX_TASKS : s->task_count;

  uint8_t idx[HEAPTOP_MAX_TASKS];
  heaptop_calc_sort_tasks(s->tasks, n, key, idx);

  heaptop_buf_printf(b,
                     "%-16s %s %4s %4s %6s %7s %7s %7s %7s\n",
                     "NAME",
                     "ST",
                     "PRI",
                     "CORE",
                     "CPU%",
                     "STACK",
                     "HEAP",
                     "PEAK",
                     "PSRAM");
  for (size_t i = 0; i < n; i++)
  {
    const heaptop_task_stats_t *t = &s->tasks[idx[i]];
    const bool deleted = t->state == HEAPTOP_TASK_DELETED;
    char prio[6] = "-", core[6] = "-", cpu[10] = "-", stack[12] = "-";
    char heap[12] = "-", peak[12] = "-", psram[12] = "-";
    if (!deleted)
    {
      snprintf(prio, sizeof(prio), "%u", t->prio);
      if (t->core >= 0)
        snprintf(core, sizeof(core), "%d", t->core);
      if (cpu_ok)
        snprintf(cpu, sizeof(cpu), "%u.%u", t->cpu_pct10 / 10u, t->cpu_pct10 % 10u);
      heaptop_fmt_bytes(stack, sizeof(stack), t->stack_hwm);
    }
    if (heap_ok)
    {
      heaptop_fmt_bytes(heap, sizeof(heap), t->heap_cur);
      heaptop_fmt_bytes(peak, sizeof(peak), t->heap_peak);
      heaptop_fmt_bytes(psram, sizeof(psram), t->heap_psram);
    }
    heaptop_buf_printf(b,
                       "%-16.16s %c  %4s %4s %6s %7s %7s %7s %7s%s\n",
                       t->name,
                       _state_char(t->state),
                       prio,
                       core,
                       cpu,
                       stack,
                       heap,
                       peak,
                       psram,
                       t->leak_suspect ? "  LEAK?" : "");
  }
  if (s->tasks_truncated)
    heaptop_buf_printf(b, "(more tasks than CONFIG_HEAPTOP_MAX_TASKS=%d; list truncated)\n", HEAPTOP_MAX_TASKS);
}

void heaptop_fmt_uptime(char *out, size_t len, uint64_t us)
{
  if (out == NULL || len == 0)
    return;
  const uint64_t t = us / 1000000u;
  const unsigned d = (unsigned)(t / 86400u);
  const unsigned h = (unsigned)((t / 3600u) % 24u);
  const unsigned m = (unsigned)((t / 60u) % 60u);
  const unsigned sec = (unsigned)(t % 60u);
  if (d)
    snprintf(out, len, "%ud%02uh%02um", d, h, m);
  else if (h)
    snprintf(out, len, "%uh%02um%02us", h, m, sec);
  else if (m)
    snprintf(out, len, "%um%02us", m, sec);
  else
    snprintf(out, len, "%us", sec);
}

void heaptop_render_sparkline(char *out, size_t len, const uint32_t *v, size_t n)
{
  if (out == NULL || len == 0)
    return;
  out[0] = '\0';
  if (v == NULL || n == 0)
    return;

  /* Keep the newest values when the series is wider than the buffer. */
  if (n > len - 1)
  {
    v += n - (len - 1);
    n = len - 1;
  }
  uint32_t lo = v[0], hi = v[0];
  for (size_t i = 1; i < n; i++)
  {
    if (v[i] < lo)
      lo = v[i];
    if (v[i] > hi)
      hi = v[i];
  }
  const size_t levels = sizeof(HEAPTOP_SPARK_LEVELS) - 1;
  for (size_t i = 0; i < n; i++)
  {
    size_t l = levels / 2;
    if (hi > lo)
      l = (size_t)(((uint64_t)(v[i] - lo) * (levels - 1)) / (hi - lo));
    out[i] = HEAPTOP_SPARK_LEVELS[l];
  }
  out[n] = '\0';
}

static const char *const s_sort_names[] = {"cpu", "heap", "stack", "name"};

#define HEAPTOP_BAR_WIDTH   20
#define HEAPTOP_SPARK_WIDTH 20

static void _core_bars(heaptop_buf_t *b, const heaptop_snapshot_t *s)
{
  for (int c = 0; c < s->num_cores && c < HEAPTOP_MAX_CORES; c++)
  {
    const uint16_t pct = s->core_load_pct10[c] > 1000 ? 1000 : s->core_load_pct10[c];
    const int filled = (pct * HEAPTOP_BAR_WIDTH) / 1000;
    char bar[HEAPTOP_BAR_WIDTH + 1];
    for (int i = 0; i < HEAPTOP_BAR_WIDTH; i++) bar[i] = i < filled ? '#' : '.';
    bar[HEAPTOP_BAR_WIDTH] = '\0';
    heaptop_buf_printf(b, "cpu%d [%s] %3u.%u%%   ", c, bar, pct / 10u, pct % 10u);
  }
  heaptop_buf_printf(b, "\n");
}

static void _trend(heaptop_buf_t *b, const char *label, const heaptop_snapshot_t *s, heaptop_trend_t series)
{
  char spark[HEAPTOP_SPARK_WIDTH + 1];
  heaptop_render_sparkline(spark, sizeof(spark), s->trend[series], s->trend_len);
  heaptop_buf_printf(b, "  %s %-*s", label, HEAPTOP_SPARK_WIDTH, spark);
}

static void _region_line(heaptop_buf_t *b, const heaptop_snapshot_t *s, heaptop_region_t r)
{
  const heaptop_region_stats_t *rs = &s->region[r];
  char total[12], free_[12], min[12], largest[12], frag[10];
  heaptop_fmt_bytes(total, sizeof(total), rs->total);
  heaptop_fmt_bytes(free_, sizeof(free_), rs->free);
  heaptop_fmt_bytes(min, sizeof(min), rs->min_free);
  heaptop_fmt_bytes(largest, sizeof(largest), rs->largest);
  _pct(frag, sizeof(frag), rs->frag_pct10);
  heaptop_buf_printf(b,
                     "%-9s %7s free of %-7s min %-7s largest %-7s frag %6s\n",
                     s_region_names[r],
                     free_,
                     total,
                     min,
                     largest,
                     frag);
  if (s->trend_len == 0)
    return;
  if (r == HEAPTOP_REGION_INTERNAL)
  {
    heaptop_buf_printf(b, "%-9s", "");
    _trend(b, "free", s, HEAPTOP_TREND_INTERNAL_FREE);
    _trend(b, "largest", s, HEAPTOP_TREND_INTERNAL_LARGEST);
    heaptop_buf_printf(b, "\n");
  }
  else if (r == HEAPTOP_REGION_PSRAM)
  {
    heaptop_buf_printf(b, "%-9s", "");
    _trend(b, "free", s, HEAPTOP_TREND_PSRAM_FREE);
    heaptop_buf_printf(b, "\n");
  }
}

static void _alloc_rates(heaptop_buf_t *b, const heaptop_snapshot_t *s)
{
  char bytes[12];
  heaptop_fmt_bytes(bytes, sizeof(bytes), s->alloc.bytes_per_s);
  heaptop_buf_printf(b,
                     "allocs/s %u  frees/s %u  bytes/s %s",
                     (unsigned)s->alloc.allocs_per_s,
                     (unsigned)s->alloc.frees_per_s,
                     bytes);
}

void heaptop_render_top(heaptop_buf_t *b, const heaptop_snapshot_t *s, const heaptop_top_view_t *view)
{
  if (b == NULL || s == NULL || view == NULL)
    return;
  char up[24];
  heaptop_fmt_uptime(up, sizeof(up), s->uptime_us);
  const unsigned sort = view->sort < 4 ? (unsigned)view->sort : 0u;
  heaptop_buf_printf(b,
                     "heaptop  up %s  #%u every %ums  cost %uus  refresh %ums  sort %s%s\n",
                     up,
                     (unsigned)s->seq,
                     (unsigned)s->period_ms,
                     (unsigned)s->self_us,
                     (unsigned)view->refresh_ms,
                     s_sort_names[sort],
                     view->paused ? "  PAUSED" : "");
  heaptop_buf_printf(b, "keys: q quit  c/m/s/n sort by cpu/memory/stack/name  +/- refresh  p pause\n");

  if (s->features & HEAPTOP_FEAT_RUNTIME_STATS)
    _core_bars(b, s);

  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
  {
    if (s->region[r].present)
      _region_line(b, s, (heaptop_region_t)r);
  }
  if (s->features & HEAPTOP_FEAT_ALLOC_HOOKS)
  {
    _alloc_rates(b, s);
    if (s->features & HEAPTOP_FEAT_FAIL_CB)
      heaptop_buf_printf(b, "  failures %u", (unsigned)s->alloc.failures);
    heaptop_buf_printf(b, "\n");
  }
  heaptop_buf_printf(b, "\n");
  heaptop_render_tasks(b, s, view->sort);
}

#define HEAPTOP_HIST_BAR_WIDTH 30

void heaptop_render_frag(heaptop_buf_t *b, const char *name, const heaptop_frag_hist_t *h)
{
  static const char *const labels[HEAPTOP_FRAG_BUCKETS] = {"<64", "<256", "<1K", "<4K", "<16K", "<64K", ">=64K"};
  if (b == NULL || name == NULL || h == NULL)
    return;
  if (h->free_blocks == 0)
  {
    heaptop_buf_printf(b, "%s: no free blocks\n", name);
    return;
  }
  char total[12], largest[12];
  heaptop_fmt_bytes(total, sizeof(total), h->free_bytes);
  heaptop_fmt_bytes(largest, sizeof(largest), h->largest);
  heaptop_buf_printf(b, "%s: %u free blocks, %s free, largest %s\n", name, (unsigned)h->free_blocks, total, largest);
  heaptop_buf_printf(b, "%-7s %8s %9s  %s\n", "SIZE", "BLOCKS", "BYTES", "SHARE OF FREE BYTES");
  for (int i = 0; i < HEAPTOP_FRAG_BUCKETS; i++)
  {
    char bytes[12];
    heaptop_fmt_bytes(bytes, sizeof(bytes), h->bytes[i]);
    int width = (int)(((uint64_t)h->bytes[i] * HEAPTOP_HIST_BAR_WIDTH) / h->free_bytes);
    if (width == 0 && h->bytes[i] > 0)
      width = 1;
    char bar[HEAPTOP_HIST_BAR_WIDTH + 1];
    for (int k = 0; k < width; k++) bar[k] = '#';
    bar[width] = '\0';
    heaptop_buf_printf(b, "%-7s %8u %9s  %s\n", labels[i], (unsigned)h->count[i], bytes, bar);
  }
}

static const char *_task_name(const heaptop_snapshot_t *s, const heaptop_fail_t *f, char *hex, size_t len)
{
  if (f->isr)
    return "(ISR)";
  for (uint16_t i = 0; i < s->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    if (s->tasks[i].handle == f->task)
      return s->tasks[i].name;
  }
  snprintf(hex, len, "0x%08lx", (unsigned long)f->task);
  return hex;
}

void heaptop_render_allocs(heaptop_buf_t *b, const heaptop_snapshot_t *s, const heaptop_fail_t *fails, size_t n)
{
  if (b == NULL || s == NULL)
    return;
  if (s->features & HEAPTOP_FEAT_ALLOC_HOOKS)
  {
    _alloc_rates(b, s);
    heaptop_buf_printf(b, "\n");
  }
  else
  {
    heaptop_buf_printf(b, "allocation counters off: enable CONFIG_HEAP_USE_HOOKS\n");
  }

  if (!(s->features & HEAPTOP_FEAT_FAIL_CB))
  {
    heaptop_buf_printf(b, "failure log off: enable CONFIG_HEAPTOP_FAILED_ALLOC_CALLBACK\n");
    return;
  }
  heaptop_buf_printf(b, "failures %u since boot\n", (unsigned)s->alloc.failures);
  if (fails == NULL || n == 0)
    return;
  heaptop_buf_printf(b, "LAST FAILURES (newest first)\n");
  heaptop_buf_printf(b, "%8s %8s %10s  %-16s %s\n", "AGE", "SIZE", "CAPS", "TASK", "FUNCTION");
  for (size_t i = 0; i < n; i++)
  {
    const heaptop_fail_t *f = &fails[i];
    const uint64_t age = s->uptime_us > f->t_us ? (s->uptime_us - f->t_us) / 100000u : 0;
    char agestr[16], size[12], hex[16];
    snprintf(agestr, sizeof(agestr), "%lu.%lus", (unsigned long)(age / 10u), (unsigned long)(age % 10u));
    heaptop_fmt_bytes(size, sizeof(size), f->size);
    heaptop_buf_printf(b,
                       "%8s %8s 0x%08lx  %-16.16s %s\n",
                       agestr,
                       size,
                       (unsigned long)f->caps,
                       _task_name(s, f, hex, sizeof(hex)),
                       f->func ? f->func : "?");
  }
}

void heaptop_render_leaks(heaptop_buf_t *b, const heaptop_leak_info_t *info, const heaptop_leak_group_t *groups,
                          size_t n)
{
  if (b == NULL || info == NULL)
    return;
  if (!info->available)
  {
    heaptop_buf_printf(b, "leak trace off: enable CONFIG_HEAP_TRACING_STANDALONE (and CONFIG_HEAPTOP_LEAK_TRACE)\n");
    return;
  }
  heaptop_buf_printf(b,
                     "leak trace %s %lu.%lus: %lu surviving allocations (buffer %lu/%lu)\n",
                     info->running ? "running for" : "stopped after",
                     (unsigned long)(info->duration_ms / 1000u),
                     (unsigned long)((info->duration_ms % 1000u) / 100u),
                     (unsigned long)info->records,
                     (unsigned long)info->records,
                     (unsigned long)info->capacity);
  if (info->overflowed)
    heaptop_buf_printf(b,
                       "WARNING: record buffer filled up, results are incomplete: raise CONFIG_HEAPTOP_LEAK_RECORDS\n");
  if (groups == NULL || n == 0)
  {
    heaptop_buf_printf(b, "no surviving allocations\n");
    return;
  }

  heaptop_buf_printf(b, "%9s %6s %10s  %s\n", "BYTES", "COUNT", "SIZE", "CALL STACK (innermost first)");
  for (size_t i = 0; i < n; i++)
  {
    const heaptop_leak_group_t *g = &groups[i];
    char bytes[12], size[24], stack[HEAPTOP_LEAK_DEPTH * 12 + 1];
    heaptop_fmt_bytes(bytes, sizeof(bytes), g->bytes);
    if (g->min_size == g->max_size)
      snprintf(size, sizeof(size), "%lu", (unsigned long)g->min_size);
    else
      snprintf(size, sizeof(size), "%lu..%lu", (unsigned long)g->min_size, (unsigned long)g->max_size);
    size_t used = 0;
    stack[0] = '\0';
    for (int k = 0; k < HEAPTOP_LEAK_DEPTH && g->pc[k] != 0; k++)
    {
      int w = snprintf(stack + used, sizeof(stack) - used, " 0x%08lx", (unsigned long)g->pc[k]);
      if (w < 0 || (size_t)w >= sizeof(stack) - used)
        break;
      used += (size_t)w;
    }
    heaptop_buf_printf(b, "%9s %6lu %10s %s\n", bytes, (unsigned long)g->count, size, stack);
  }
  if (info->ungrouped)
    heaptop_buf_printf(b, "(%lu more allocations from call sites beyond the table)\n", (unsigned long)info->ungrouped);
  heaptop_buf_printf(b, "idf.py monitor decodes the 0x4... addresses into function and file:line\n");
}

static void _fmt_signed(char *out, size_t len, int64_t delta)
{
  if (delta == 0)
  {
    snprintf(out, len, "0");
    return;
  }
  char mag[12];
  const uint64_t abs = delta < 0 ? (uint64_t)(-delta) : (uint64_t)delta;
  heaptop_fmt_bytes(mag, sizeof(mag), abs > UINT32_MAX ? UINT32_MAX : (uint32_t)abs);
  snprintf(out, len, "%c%s", delta < 0 ? '-' : '+', mag);
}

static const heaptop_task_stats_t *_task_by_handle(const heaptop_snapshot_t *s, uintptr_t handle)
{
  for (uint16_t i = 0; i < s->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    if (s->tasks[i].handle == handle)
      return &s->tasks[i];
  }
  return NULL;
}

static void _diff_row(heaptop_buf_t *b, const char *name, const char *tag, const char *before, const char *now,
                      int64_t delta)
{
  char label[HEAPTOP_TASK_NAME_LEN + 8], change[16];
  snprintf(label, sizeof(label), "%s%s", name, tag);
  _fmt_signed(change, sizeof(change), delta);
  heaptop_buf_printf(b, "%-23s %11s %10s %10s\n", label, before, now, change);
}

void heaptop_render_diff(heaptop_buf_t *b, const heaptop_snapshot_t *before, const heaptop_snapshot_t *now)
{
  if (b == NULL || before == NULL || now == NULL)
    return;
  if (before->seq == 0)
  {
    heaptop_buf_printf(b, "no mark set: run `ht mark` first, then `ht diff`\n");
    return;
  }
  char elapsed[24];
  heaptop_fmt_uptime(elapsed,
                     sizeof(elapsed),
                     now->uptime_us > before->uptime_us ? now->uptime_us - before->uptime_us : 0);
  heaptop_buf_printf(b,
                     "diff over %s (sample #%lu -> #%lu)\n",
                     elapsed,
                     (unsigned long)before->seq,
                     (unsigned long)now->seq);

  heaptop_buf_printf(b, "%-23s %11s %10s %10s\n", "REGION", "FREE BEFORE", "FREE NOW", "CHANGE");
  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
  {
    if (!before->region[r].present && !now->region[r].present)
      continue;
    char was[12], is[12];
    heaptop_fmt_bytes(was, sizeof(was), before->region[r].free);
    heaptop_fmt_bytes(is, sizeof(is), now->region[r].free);
    _diff_row(b, s_region_names[r], "", was, is, (int64_t)now->region[r].free - (int64_t)before->region[r].free);
  }

  if (!(now->features & HEAPTOP_FEAT_TASK_HEAP))
  {
    heaptop_buf_printf(b, "per-task heap: enable CONFIG_HEAP_TASK_TRACKING\n");
    return;
  }
  heaptop_buf_printf(b, "\n%-23s %11s %10s %10s\n", "TASK", "HEAP BEFORE", "HEAP NOW", "CHANGE");
  bool any = false;
  for (uint16_t i = 0; i < now->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    const heaptop_task_stats_t *t = &now->tasks[i];
    const heaptop_task_stats_t *old = _task_by_handle(before, t->handle);
    char was[12] = "-", is[12];
    heaptop_fmt_bytes(is, sizeof(is), t->heap_cur);
    if (old == NULL)
    {
      _diff_row(b, t->name, " (new)", was, is, (int64_t)t->heap_cur);
      any = true;
      continue;
    }
    if (old->heap_cur == t->heap_cur)
      continue;
    heaptop_fmt_bytes(was, sizeof(was), old->heap_cur);
    _diff_row(b, t->name, "", was, is, (int64_t)t->heap_cur - (int64_t)old->heap_cur);
    any = true;
  }
  for (uint16_t i = 0; i < before->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    const heaptop_task_stats_t *old = &before->tasks[i];
    if (_task_by_handle(now, old->handle) != NULL)
      continue;
    char was[12];
    heaptop_fmt_bytes(was, sizeof(was), old->heap_cur);
    _diff_row(b, old->name, " (gone)", was, "-", -(int64_t)old->heap_cur);
    any = true;
  }
  if (!any)
    heaptop_buf_printf(b, "no per-task heap changes\n");
}
