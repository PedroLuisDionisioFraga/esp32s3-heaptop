#include "heaptop_calc.h"

#include <ctype.h>
#include <string.h>

void heaptop_ring_init(heaptop_ring_t *r, uint32_t *buf, uint16_t cap)
{
  if (r == NULL)
    return;
  r->buf = buf;
  r->cap = buf ? cap : 0;
  r->head = 0;
  r->count = 0;
}

void heaptop_ring_push(heaptop_ring_t *r, uint32_t v)
{
  if (r == NULL || r->cap == 0)
    return;
  r->buf[r->head] = v;
  r->head = (uint16_t)((r->head + 1u) % r->cap);
  if (r->count < r->cap)
    r->count++;
}

uint16_t heaptop_ring_copy(const heaptop_ring_t *r, uint32_t *out, uint16_t max)
{
  if (r == NULL || out == NULL || r->cap == 0)
    return 0;
  const uint16_t n = r->count < max ? r->count : max;
  const uint16_t start = (uint16_t)((r->head + r->cap - n) % r->cap);
  for (uint16_t i = 0; i < n; i++) out[i] = r->buf[(start + i) % r->cap];
  return n;
}

uint8_t heaptop_calc_bucket(uint32_t size)
{
  static const uint32_t limits[HEAPTOP_FRAG_BUCKETS - 1] = {64u, 256u, 1024u, 4096u, 16384u, 65536u};
  uint8_t b = 0;
  while (b < HEAPTOP_FRAG_BUCKETS - 1 && size >= limits[b]) b++;
  return b;
}

void heaptop_calc_hist_add(heaptop_frag_hist_t *h, uint32_t size)
{
  if (h == NULL)
    return;
  const uint8_t b = heaptop_calc_bucket(size);
  h->count[b]++;
  h->bytes[b] += size;
  h->free_blocks++;
  h->free_bytes += size;
  if (size > h->largest)
    h->largest = size;
}

uint16_t heaptop_calc_fail_copy(const heaptop_fail_t *buf, uint16_t cap, uint16_t head, uint16_t count,
                                heaptop_fail_t *out, uint16_t max)
{
  if (buf == NULL || out == NULL || cap == 0)
    return 0;
  const uint16_t n = count < max ? count : max;
  for (uint16_t i = 0; i < n; i++) out[i] = buf[(head + cap - 1u - i) % cap];
  return n;
}

bool heaptop_calc_leak_add(heaptop_leak_group_t *groups, size_t cap, size_t *n, const uintptr_t *pc, size_t depth,
                           uint32_t size)
{
  if (groups == NULL || n == NULL || pc == NULL)
    return false;
  uintptr_t key[HEAPTOP_LEAK_DEPTH] = {0};
  const size_t d = depth < HEAPTOP_LEAK_DEPTH ? depth : HEAPTOP_LEAK_DEPTH;
  for (size_t k = 0; k < d; k++) key[k] = pc[k];

  for (size_t i = 0; i < *n; i++)
  {
    heaptop_leak_group_t *g = &groups[i];
    if (memcmp(g->pc, key, sizeof(key)) != 0)
      continue;
    g->count++;
    g->bytes += size;
    if (size < g->min_size)
      g->min_size = size;
    if (size > g->max_size)
      g->max_size = size;
    return true;
  }
  if (*n >= cap)
    return false;
  heaptop_leak_group_t *g = &groups[(*n)++];
  memcpy(g->pc, key, sizeof(key));
  g->count = 1;
  g->bytes = size;
  g->min_size = size;
  g->max_size = size;
  return true;
}

void heaptop_calc_leak_sort(heaptop_leak_group_t *groups, size_t n)
{
  if (groups == NULL)
    return;
  for (size_t i = 1; i < n; i++)
  {
    heaptop_leak_group_t cur = groups[i];
    size_t j = i;
    while (j > 0 && groups[j - 1].bytes < cur.bytes)
    {
      groups[j] = groups[j - 1];
      j--;
    }
    groups[j] = cur;
  }
}

bool heaptop_calc_leak_suspect(const uint32_t *v, size_t n, uint32_t threshold, int32_t *growth)
{
  if (v == NULL || n == 0)
    return false;
  int64_t g = (int64_t)v[n - 1] - (int64_t)v[0];
  if (g > INT32_MAX)
    g = INT32_MAX;
  if (g < INT32_MIN)
    g = INT32_MIN;
  if (growth)
    *growth = (int32_t)g;
  if (n < HEAPTOP_LEAK_MIN_SAMPLES || threshold == 0 || g < (int64_t)threshold)
    return false;

  uint32_t peak = v[0];
  size_t rises = 0;
  for (size_t i = 0; i < n; i++)
  {
    if (v[i] < v[0])
      return false; /* gave memory back below where it started */
    if (v[i] > peak)
      peak = v[i];
    if (i > 0 && v[i] > v[i - 1])
      rises++;
  }
  if (rises < HEAPTOP_LEAK_MIN_RISES)
    return false;
  /* One long-lived buffer, start-up allocations that have stopped, or a boot step
   * followed by a trickle are not leaks: a leak grows in every third of the window. */
  const uint64_t per_third = threshold / 6u;
  const size_t cut[4] = {0, n / 3, (2 * n) / 3, n - 1};
  for (int k = 0; k < 3; k++)
  {
    if (v[cut[k + 1]] < v[cut[k]] || (uint64_t)(v[cut[k + 1]] - v[cut[k]]) < per_third)
      return false;
  }
  /* Still holding at least 90% of its peak: nothing was released. */
  return (uint64_t)v[n - 1] * 10u >= (uint64_t)peak * 9u;
}

void heaptop_growth_init(heaptop_growth_t *g, heaptop_growth_slot_t *slots, uint16_t n_slots, uint32_t *mem,
                         uint16_t window)
{
  if (g == NULL)
    return;
  g->slots = slots;
  g->n_slots = (slots && mem) ? n_slots : 0;
  g->mem = mem;
  g->window = window;
  for (uint16_t i = 0; i < g->n_slots; i++)
  {
    slots[i].handle = 0;
    slots[i].seen = 0;
    heaptop_ring_init(&slots[i].ring, mem + (size_t)i * window, window);
  }
}

heaptop_ring_t *heaptop_growth_track(heaptop_growth_t *g, uintptr_t handle, uint32_t seq)
{
  if (g == NULL || handle == 0)
    return NULL;
  for (uint16_t i = 0; i < g->n_slots; i++)
  {
    if (g->slots[i].handle == handle)
    {
      g->slots[i].seen = seq;
      return &g->slots[i].ring;
    }
  }
  for (uint16_t i = 0; i < g->n_slots; i++)
  {
    heaptop_growth_slot_t *s = &g->slots[i];
    /* Free, or its task was missing from the previous sample: the task is gone. */
    if (s->handle == 0 || s->seen + 1u < seq)
    {
      s->handle = handle;
      s->seen = seq;
      heaptop_ring_init(&s->ring, g->mem + (size_t)i * g->window, g->window);
      return &s->ring;
    }
  }
  return NULL;
}

bool heaptop_calc_below_floor(bool active, uint32_t value, uint32_t floor, uint32_t hyst_pct)
{
  if (floor == 0)
    return false;
  if (!active)
    return value < floor;
  const uint64_t clear_at = (uint64_t)floor + ((uint64_t)floor * hyst_pct) / 100u;
  return value < clear_at;
}

bool heaptop_calc_above_ceiling(bool active, uint32_t value, uint32_t ceiling, uint32_t hyst_pct)
{
  if (ceiling == 0)
    return false;
  if (!active)
    return value > ceiling;
  const uint64_t cut = ((uint64_t)ceiling * hyst_pct) / 100u;
  const uint64_t clear_at = ceiling > cut ? ceiling - cut : 0;
  return value > clear_at;
}

uint32_t heaptop_calc_alerts(const heaptop_thresholds_t *th, const heaptop_snapshot_t *s, uint32_t active,
                             uint32_t prev_failures)
{
  if (th == NULL || s == NULL)
    return 0;
  const uint32_t h = th->hysteresis_pct;
  uint32_t out = 0;

  const heaptop_region_stats_t *in = &s->region[HEAPTOP_REGION_INTERNAL];
  if (in->present)
  {
    if (heaptop_calc_below_floor(active & HEAPTOP_ALERT_DRAM_FREE, in->free, th->dram_free_min, h))
      out |= HEAPTOP_ALERT_DRAM_FREE;
    if (heaptop_calc_below_floor(active & HEAPTOP_ALERT_DRAM_LARGEST, in->largest, th->dram_largest_min, h))
      out |= HEAPTOP_ALERT_DRAM_LARGEST;
    if (heaptop_calc_above_ceiling(active & HEAPTOP_ALERT_FRAG, in->frag_pct10, th->frag_pct_max * 10u, h))
      out |= HEAPTOP_ALERT_FRAG;
  }
  const heaptop_region_stats_t *ps = &s->region[HEAPTOP_REGION_PSRAM];
  if (ps->present && heaptop_calc_below_floor(active & HEAPTOP_ALERT_PSRAM_FREE, ps->free, th->psram_free_min, h))
    out |= HEAPTOP_ALERT_PSRAM_FREE;

  bool any_task = false, any_leak = false;
  uint32_t min_hwm = UINT32_MAX;
  const uint16_t n = s->task_count > HEAPTOP_MAX_TASKS ? HEAPTOP_MAX_TASKS : s->task_count;
  for (uint16_t i = 0; i < n; i++)
  {
    const heaptop_task_stats_t *t = &s->tasks[i];
    if (t->leak_suspect)
      any_leak = true;
    if (t->state == HEAPTOP_TASK_DELETED)
      continue;
    any_task = true;
    if (t->stack_hwm < min_hwm)
      min_hwm = t->stack_hwm;
  }
  if (any_task && heaptop_calc_below_floor(active & HEAPTOP_ALERT_STACK, min_hwm, th->stack_hwm_min, h))
    out |= HEAPTOP_ALERT_STACK;
  if (any_leak && th->task_growth != 0)
    out |= HEAPTOP_ALERT_LEAK;
  if ((s->features & HEAPTOP_FEAT_FAIL_CB) && s->alloc.failures != prev_failures)
    out |= HEAPTOP_ALERT_ALLOC_FAIL;
  return out;
}

static void _set_heap(heaptop_task_stats_t *t, const heaptop_heap_owner_t *o)
{
  t->heap_cur = o->cur;
  t->heap_peak = o->peak;
  t->heap_psram = o->psram;
}

bool heaptop_calc_merge_heap(heaptop_snapshot_t *s, const heaptop_heap_owner_t *owners, size_t n)
{
  if (s == NULL || owners == NULL)
    return true;
  /* Only rows that came from the scheduler are matched; deleted rows appended
   * below never are, so a reused handle cannot mix a dead task into a live one. */
  const uint16_t live = s->task_count > HEAPTOP_MAX_TASKS ? HEAPTOP_MAX_TASKS : s->task_count;
  bool fit = true;
  for (size_t i = 0; i < n; i++)
  {
    const heaptop_heap_owner_t *o = &owners[i];
    if (o->alive)
    {
      for (uint16_t r = 0; r < live; r++)
      {
        if (s->tasks[r].handle == o->handle)
        {
          _set_heap(&s->tasks[r], o);
          break;
        }
      }
      continue;
    }
    if (o->cur == 0)
      continue;
    if (s->task_count >= HEAPTOP_MAX_TASKS)
    {
      s->tasks_truncated = true;
      fit = false;
      continue;
    }
    heaptop_task_stats_t *t = &s->tasks[s->task_count++];
    memset(t, 0, sizeof(*t));
    if (o->name)
    {
      strncpy(t->name, o->name, sizeof(t->name) - 1);
      t->name[sizeof(t->name) - 1] = '\0';
    }
    t->handle = o->handle;
    t->state = HEAPTOP_TASK_DELETED;
    t->core = -1;
    _set_heap(t, o);
  }
  return fit;
}

uint16_t heaptop_calc_frag_pct10(uint32_t free, uint32_t largest)
{
  if (free == 0 || largest >= free)
    return 0;
  return (uint16_t)(((uint64_t)(free - largest) * 1000u) / free);
}

uint16_t heaptop_calc_pct10(uint64_t part, uint64_t whole)
{
  if (whole == 0)
    return 0;
  if (part >= whole)
    return 1000;
  /* part < whole, so part / (whole / 1000) cannot overflow; scale down huge
   * counters first so part * 1000 stays inside 64 bits. */
  while (part > UINT64_MAX / 1000u)
  {
    part >>= 1;
    whole >>= 1;
  }
  return (uint16_t)((part * 1000u) / whole);
}

uint16_t heaptop_calc_busy_pct10(uint64_t idle_delta, uint64_t wall_delta)
{
  if (wall_delta == 0)
    return 0;
  return (uint16_t)(1000u - heaptop_calc_pct10(idle_delta, wall_delta));
}

uint32_t heaptop_calc_rate_per_s(uint32_t delta, uint32_t dt_ms)
{
  if (dt_ms == 0)
    return 0;
  uint64_t rate = ((uint64_t)delta * 1000u) / dt_ms;
  return rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
}

uint32_t heaptop_calc_delta_u32(uint32_t now, uint32_t prev)
{
  return now - prev;
}

bool heaptop_calc_prev_find(const heaptop_calc_prev_t *prev, size_t n, uintptr_t handle, uint64_t *counter)
{
  if (prev == NULL || counter == NULL)
    return false;
  for (size_t i = 0; i < n; i++)
  {
    if (prev[i].handle == handle)
    {
      *counter = prev[i].counter;
      return true;
    }
  }
  return false;
}

static int _name_cmp(const char *a, const char *b)
{
  for (;; a++, b++)
  {
    int ca = tolower((unsigned char)*a);
    int cb = tolower((unsigned char)*b);
    if (ca != cb || ca == '\0')
      return ca - cb;
  }
}

/* True when task a must come before task b for this key. */
static bool _before(const heaptop_task_stats_t *a, const heaptop_task_stats_t *b, heaptop_sort_t key)
{
  switch (key)
  {
    case HEAPTOP_SORT_CPU:
      return a->cpu_pct10 > b->cpu_pct10;
    case HEAPTOP_SORT_HEAP:
      return a->heap_cur > b->heap_cur;
    case HEAPTOP_SORT_STACK:
      return a->stack_hwm < b->stack_hwm;
    case HEAPTOP_SORT_NAME:
    default:
      return _name_cmp(a->name, b->name) < 0;
  }
}

void heaptop_calc_sort_tasks(const heaptop_task_stats_t *tasks, size_t n, heaptop_sort_t key, uint8_t *idx)
{
  if (tasks == NULL || idx == NULL)
    return;
  for (size_t i = 0; i < n; i++) idx[i] = (uint8_t)i;

  /* Insertion sort: stable, allocation-free, and n is at most a few dozen. */
  for (size_t i = 1; i < n; i++)
  {
    uint8_t cur = idx[i];
    size_t j = i;
    while (j > 0 && _before(&tasks[cur], &tasks[idx[j - 1]], key))
    {
      idx[j] = idx[j - 1];
      j--;
    }
    idx[j] = cur;
  }
}
