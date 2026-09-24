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
