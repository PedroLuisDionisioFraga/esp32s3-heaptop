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
  for (size_t i = 0; i < n; i++)
  {
    if (v[i] < v[0])
      return false; /* gave memory back below where it started */
    if (v[i] > peak)
      peak = v[i];
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
