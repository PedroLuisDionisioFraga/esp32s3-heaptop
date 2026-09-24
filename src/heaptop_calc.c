#include "heaptop_calc.h"

#include <ctype.h>

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
