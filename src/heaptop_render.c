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
    heaptop_buf_printf(b, "(more tasks than HEAPTOP_MAX_TASKS=%d; list truncated)\n", HEAPTOP_MAX_TASKS);
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

/* "boot" or "clear 2m05s ago": where the current stats window starts. */
static void _since(char *out, size_t len, const heaptop_snapshot_t *s)
{
  if (s->since_us == 0)
  {
    snprintf(out, len, "boot");
    return;
  }
  char ago[24];
  heaptop_fmt_uptime(ago, sizeof(ago), s->uptime_us > s->since_us ? s->uptime_us - s->since_us : 0);
  snprintf(out, len, "clear %s ago", ago);
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
  if (s->alerts)
  {
    heaptop_buf_printf(b, "ALERTS:");
    for (uint32_t i = 0; i < HEAPTOP_ALERT_COUNT; i++)
    {
      if (s->alerts & (1u << i))
        heaptop_buf_printf(b, " %s", heaptop_alert_name(1u << i));
    }
    heaptop_buf_printf(b, "  (ht health for details)\n");
  }

  if (s->features & HEAPTOP_FEAT_RUNTIME_STATS)
    _core_bars(b, s);

  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
  {
    if (s->region[r].present)
      _region_line(b, s, (heaptop_region_t)r);
  }
  if (s->features & HEAPTOP_FEAT_FAIL_CB)
  {
    char since[40];
    _since(since, sizeof(since), s);
    heaptop_buf_printf(b, "failures %u since %s\n", (unsigned)s->failures, since);
  }
  heaptop_buf_printf(b, "\n");
  heaptop_render_tasks(b, s, view->sort);
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

static const char *const s_alert_names[HEAPTOP_ALERT_COUNT] = {
  "dram_free", "dram_largest", "frag", "psram_free", "stack", "leak", "alloc_fail"};

const char *heaptop_alert_name(uint32_t alert)
{
  for (uint32_t i = 0; i < HEAPTOP_ALERT_COUNT; i++)
  {
    if (alert == (1u << i))
      return s_alert_names[i];
  }
  return "unknown";
}

/* Alive task with the lowest stack high-water mark, or NULL. */
static const heaptop_task_stats_t *_lowest_stack(const heaptop_snapshot_t *s)
{
  const heaptop_task_stats_t *low = NULL;
  for (uint16_t i = 0; i < s->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    const heaptop_task_stats_t *t = &s->tasks[i];
    if (t->state != HEAPTOP_TASK_DELETED && (low == NULL || t->stack_hwm < low->stack_hwm))
      low = t;
  }
  return low;
}

/* Leak suspect that grew the most, or NULL. */
static const heaptop_task_stats_t *_worst_leak(const heaptop_snapshot_t *s)
{
  const heaptop_task_stats_t *worst = NULL;
  for (uint16_t i = 0; i < s->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    const heaptop_task_stats_t *t = &s->tasks[i];
    if (t->leak_suspect && (worst == NULL || t->heap_growth > worst->heap_growth))
      worst = t;
  }
  return worst;
}

void heaptop_render_alert(char *out, size_t len, uint32_t alert, const heaptop_snapshot_t *s,
                          const heaptop_thresholds_t *th)
{
  if (out == NULL || len == 0)
    return;
  out[0] = '\0';
  if (s == NULL || th == NULL)
    return;
  const heaptop_region_stats_t *in = &s->region[HEAPTOP_REGION_INTERNAL];
  char v[16], lim[16];
  switch (alert)
  {
    case HEAPTOP_ALERT_DRAM_FREE:
      heaptop_fmt_bytes(v, sizeof(v), in->free);
      heaptop_fmt_bytes(lim, sizeof(lim), th->dram_free_min);
      snprintf(out, len, "internal RAM free %s below %s", v, lim);
      break;
    case HEAPTOP_ALERT_DRAM_LARGEST:
      heaptop_fmt_bytes(v, sizeof(v), in->largest);
      heaptop_fmt_bytes(lim, sizeof(lim), th->dram_largest_min);
      snprintf(out, len, "largest internal free block %s below %s", v, lim);
      break;
    case HEAPTOP_ALERT_FRAG:
      _pct(v, sizeof(v), in->frag_pct10);
      snprintf(out, len, "internal RAM fragmentation %s above %lu%%", v, (unsigned long)th->frag_pct_max);
      break;
    case HEAPTOP_ALERT_PSRAM_FREE:
      heaptop_fmt_bytes(v, sizeof(v), s->region[HEAPTOP_REGION_PSRAM].free);
      heaptop_fmt_bytes(lim, sizeof(lim), th->psram_free_min);
      snprintf(out, len, "PSRAM free %s below %s", v, lim);
      break;
    case HEAPTOP_ALERT_STACK:
    {
      const heaptop_task_stats_t *t = _lowest_stack(s);
      if (t)
        snprintf(out,
                 len,
                 "task '%s' has %lu bytes of stack left (floor %lu)",
                 t->name,
                 (unsigned long)t->stack_hwm,
                 (unsigned long)th->stack_hwm_min);
      break;
    }
    case HEAPTOP_ALERT_LEAK:
    {
      const heaptop_task_stats_t *t = _worst_leak(s);
      if (t)
      {
        _fmt_signed(v, sizeof(v), t->heap_growth);
        snprintf(out, len, "task '%s' heap grew %s without giving memory back", t->name, v);
      }
      break;
    }
    case HEAPTOP_ALERT_ALLOC_FAIL:
    {
      char since[40];
      _since(since, sizeof(since), s);
      snprintf(out, len, "allocation failed (%lu since %s, see ht health)", (unsigned long)s->failures, since);
      break;
    }
    default:
      snprintf(out, len, "unknown alert 0x%lx", (unsigned long)alert);
      break;
  }
}

static void _failures(heaptop_buf_t *b, const heaptop_snapshot_t *s, const heaptop_fail_t *fails, size_t n)
{
  if (!(s->features & HEAPTOP_FEAT_FAIL_CB))
  {
    heaptop_buf_printf(b, "failure log off: the failed-allocation callback is not registered\n");
    return;
  }
  char since[40];
  _since(since, sizeof(since), s);
  heaptop_buf_printf(b, "failures %u since %s\n", (unsigned)s->failures, since);
  if (fails == NULL || n == 0)
    return;
  heaptop_buf_printf(b, "LAST FAILURES (newest first)\n");
  heaptop_buf_printf(b, "%8s %8s %10s  %-16s %s\n", "AGE", "SIZE", "CAPS", "TASK", "FUNCTION");
  for (size_t i = 0; i < n; i++)
  {
    const heaptop_fail_t *f = &fails[i];
    const uint64_t age = s->uptime_us > f->t_us ? (s->uptime_us - f->t_us) / 100000u : 0;
    char agestr[24], size[12], hex[16]; /* agestr fits a 64-bit unsigned long: 20 digits + ".9s" */
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

/* A value with the task it belongs to, "192 (blink)", or "-" without a task. */
static void _with_task(char *out, size_t len, const char *value, const heaptop_task_stats_t *t)
{
  if (t == NULL)
    snprintf(out, len, "-");
  else
    snprintf(out, len, "%s (%s)", value, t->name);
}

/* Live task that grew the most over the history window, or NULL. */
static const heaptop_task_stats_t *_most_growth(const heaptop_snapshot_t *s)
{
  const heaptop_task_stats_t *top = NULL;
  for (uint16_t i = 0; i < s->task_count && i < HEAPTOP_MAX_TASKS; i++)
  {
    const heaptop_task_stats_t *t = &s->tasks[i];
    if (t->state != HEAPTOP_TASK_DELETED && (top == NULL || t->heap_growth > top->heap_growth))
      top = t;
  }
  return top;
}

static void _verdict(heaptop_buf_t *b, const heaptop_snapshot_t *s)
{
  char since[40];
  _since(since, sizeof(since), s);
  unsigned active = 0;
  for (uint32_t i = 0; i < HEAPTOP_ALERT_COUNT; i++) active += (s->alerts >> i) & 1u;
  if (active == 0)
  {
    heaptop_buf_printf(b, "health OK, stats since %s\n", since);
    return;
  }
  heaptop_buf_printf(b, "health: %u alert%s (", active, active == 1 ? "" : "s");
  bool first = true;
  for (uint32_t i = 0; i < HEAPTOP_ALERT_COUNT; i++)
  {
    if (s->alerts & (1u << i))
    {
      heaptop_buf_printf(b, "%s%s", first ? "" : ", ", s_alert_names[i]);
      first = false;
    }
  }
  heaptop_buf_printf(b, "), stats since %s\n", since);
}

void heaptop_render_health(heaptop_buf_t *b, const heaptop_snapshot_t *s, const heaptop_thresholds_t *th,
                           const heaptop_fail_t *fails, size_t n)
{
  if (b == NULL || s == NULL || th == NULL)
    return;
  const heaptop_region_stats_t *in = &s->region[HEAPTOP_REGION_INTERNAL];
  const heaptop_region_stats_t *ps = &s->region[HEAPTOP_REGION_PSRAM];
  const bool heap_ok = (s->features & HEAPTOP_FEAT_TASK_HEAP) != 0;

  _verdict(b, s);

  /* Current value and limit of every check, in HEAPTOP_ALERT_* bit order. */
  const uint32_t lim[HEAPTOP_ALERT_COUNT] = {th->dram_free_min,
                                             th->dram_largest_min,
                                             th->frag_pct_max,
                                             th->psram_free_min,
                                             th->stack_hwm_min,
                                             th->task_growth,
                                             1};
  char now[HEAPTOP_ALERT_COUNT][40], v[16];
  bool have[HEAPTOP_ALERT_COUNT];
  heaptop_fmt_bytes(now[0], sizeof(now[0]), in->free);
  heaptop_fmt_bytes(now[1], sizeof(now[1]), in->largest);
  _pct(now[2], sizeof(now[2]), in->frag_pct10);
  have[0] = have[1] = have[2] = in->present;
  heaptop_fmt_bytes(now[3], sizeof(now[3]), ps->free);
  have[3] = ps->present;
  const heaptop_task_stats_t *low = _lowest_stack(s);
  heaptop_fmt_bytes(v, sizeof(v), low ? low->stack_hwm : 0);
  _with_task(now[4], sizeof(now[4]), v, low);
  have[4] = low != NULL;
  const heaptop_task_stats_t *grow = _worst_leak(s);
  if (grow == NULL)
    grow = _most_growth(s);
  _fmt_signed(v, sizeof(v), grow ? grow->heap_growth : 0);
  _with_task(now[5], sizeof(now[5]), v, grow);
  have[5] = heap_ok && grow != NULL;
  snprintf(now[6], sizeof(now[6]), "%u", (unsigned)s->failures);
  have[6] = (s->features & HEAPTOP_FEAT_FAIL_CB) != 0;

  heaptop_buf_printf(b, "\n%-13s %-26s %-10s %s\n", "CHECK", "NOW", "LIMIT", "STATE");
  for (uint32_t i = 0; i < HEAPTOP_ALERT_COUNT; i++)
  {
    const uint32_t bit = 1u << i;
    char limit[24];
    if (bit == HEAPTOP_ALERT_ALLOC_FAIL)
      snprintf(limit, sizeof(limit), "any new");
    else if (lim[i] == 0)
      snprintf(limit, sizeof(limit), "off");
    else if (bit == HEAPTOP_ALERT_FRAG)
      snprintf(limit, sizeof(limit), "<= %lu%%", (unsigned long)lim[i]);
    else
    {
      heaptop_fmt_bytes(v, sizeof(v), lim[i]);
      snprintf(limit, sizeof(limit), "%s %s", bit == HEAPTOP_ALERT_LEAK ? "<" : ">=", v);
    }

    const char *state = "ok";
    if (s->alerts & bit)
      state = "ALERT";
    else if (!have[i])
      state = "n/a";
    else if (bit != HEAPTOP_ALERT_ALLOC_FAIL && lim[i] == 0)
      state = "off";
    heaptop_buf_printf(b, "%-13s %-26.26s %-10s %s\n", s_alert_names[i], have[i] ? now[i] : "-", limit, state);
  }
  heaptop_buf_printf(b, "an alert clears once its value is %d%% back past the limit\n", HEAPTOP_HYSTERESIS_PCT);
  if (!heap_ok)
    heaptop_buf_printf(b, "leak check needs CONFIG_HEAP_TASK_TRACKING\n");

  heaptop_buf_printf(b, "\n");
  _failures(b, s, fails, n);
}
