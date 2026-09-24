/**
 * @file heaptop_tasks.c
 * @brief Per-task CPU %, stack high-water mark, core load and heap usage.
 *
 * Everything here runs on the sampler task, except init/deinit which run
 * while the sampler does not exist.
 */

#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heaptop_calc.h"
#include "heaptop_priv.h"
#if CONFIG_HEAP_TASK_TRACKING
#include "esp_heap_task_info.h"
#endif

/* uxTaskGetSystemState() fills nothing when the array is smaller than the task
 * count, so the status array gets headroom beyond what the snapshot shows. */
#define HEAPTOP_STATUS_CAP (HEAPTOP_MAX_TASKS * 2)
/* Task tracking keeps deleted tasks too (CONFIG_HEAP_TRACK_DELETED_TASKS). */
#define HEAPTOP_TSTAT_CAP (HEAPTOP_MAX_TASKS * 2)
/* Average heaps per task in the per-heap table. A task needing more than its
 * share only loses its PSRAM split (heap_stat NULL), never its totals. */
#define HEAPTOP_HEAPS_PER_TASK 4

/* Per-task heap history for leak suspicion: HEAPTOP_MAX_TASKS x 4 bytes per
 * sample, so chips without PSRAM keep a shorter window. */
#if CONFIG_SPIRAM
#define HEAPTOP_HISTORY_LEN 120
#else
#define HEAPTOP_HISTORY_LEN 30
#endif

#define HEAPTOP_HAS_RUNTIME_STATS (configGENERATE_RUN_TIME_STATS == 1)

typedef struct heaptop_tasks_priv
{
  /* --- Buffers. Allocated in init, freed in deinit; sampler-owned in between. --- */
  TaskStatus_t *status;
  heaptop_calc_prev_t *prev; /* run-time counters from the previous sample */
  heaptop_calc_prev_t *cur;  /* counters of this sample; swapped with prev */
#if CONFIG_HEAP_TASK_TRACKING
  task_stat_t *tstat;
  heap_stat_t *hstat;
  heaptop_heap_owner_t *owners;        /* tstat reduced for heaptop_calc_merge_heap() */
  heaptop_growth_slot_t *growth_slots; /* per-task heap history and peak since clear */
  uint32_t *growth_mem;
  uint32_t *history; /* scratch: one task's history, oldest first */
  heaptop_growth_t growth;
  uint32_t sample_no;
#endif

  /* --- Sampler-owned state. --- */
  size_t prev_count;
  uint64_t prev_wall;
  bool have_prev;
  TaskHandle_t idle[HEAPTOP_MAX_CORES];
} heaptop_tasks_priv_t;

static heaptop_tasks_priv_t s_tasks;

esp_err_t heaptop_tasks_init(uint32_t caps)
{
  memset(&s_tasks, 0, sizeof(s_tasks));
  s_tasks.status = heap_caps_calloc(HEAPTOP_STATUS_CAP, sizeof(TaskStatus_t), caps);
  s_tasks.prev = heap_caps_calloc(HEAPTOP_STATUS_CAP, sizeof(heaptop_calc_prev_t), caps);
  s_tasks.cur = heap_caps_calloc(HEAPTOP_STATUS_CAP, sizeof(heaptop_calc_prev_t), caps);
  bool ok = s_tasks.status && s_tasks.prev && s_tasks.cur;
#if CONFIG_HEAP_TASK_TRACKING
  s_tasks.tstat = heap_caps_calloc(HEAPTOP_TSTAT_CAP, sizeof(task_stat_t), caps);
  s_tasks.hstat = heap_caps_calloc(HEAPTOP_TSTAT_CAP * HEAPTOP_HEAPS_PER_TASK, sizeof(heap_stat_t), caps);
  s_tasks.owners = heap_caps_calloc(HEAPTOP_TSTAT_CAP, sizeof(heaptop_heap_owner_t), caps);
  s_tasks.growth_slots = heap_caps_calloc(HEAPTOP_MAX_TASKS, sizeof(heaptop_growth_slot_t), caps);
  s_tasks.growth_mem = heap_caps_calloc((size_t)HEAPTOP_MAX_TASKS * HEAPTOP_HISTORY_LEN, sizeof(uint32_t), caps);
  s_tasks.history = heap_caps_calloc(HEAPTOP_HISTORY_LEN, sizeof(uint32_t), caps);
  ok = ok && s_tasks.tstat && s_tasks.hstat && s_tasks.owners && s_tasks.growth_slots && s_tasks.growth_mem &&
       s_tasks.history;
#endif
  if (!ok)
  {
    heaptop_tasks_deinit();
    return ESP_ERR_NO_MEM;
  }
#if CONFIG_HEAP_TASK_TRACKING
  heaptop_growth_init(&s_tasks.growth,
                      s_tasks.growth_slots,
                      HEAPTOP_MAX_TASKS,
                      s_tasks.growth_mem,
                      HEAPTOP_HISTORY_LEN);
#endif
#if !CONFIG_FREERTOS_SMP
  for (int c = 0; c < portNUM_PROCESSORS && c < HEAPTOP_MAX_CORES; c++)
    s_tasks.idle[c] = xTaskGetIdleTaskHandleForCore(c);
#endif
  return ESP_OK;
}

void heaptop_tasks_deinit(void)
{
  heap_caps_free(s_tasks.status);
  heap_caps_free(s_tasks.prev);
  heap_caps_free(s_tasks.cur);
#if CONFIG_HEAP_TASK_TRACKING
  heap_caps_free(s_tasks.tstat);
  heap_caps_free(s_tasks.hstat);
  heap_caps_free(s_tasks.owners);
  heap_caps_free(s_tasks.growth_slots);
  heap_caps_free(s_tasks.growth_mem);
  heap_caps_free(s_tasks.history);
#endif
  memset(&s_tasks, 0, sizeof(s_tasks));
}

static uint8_t _state(eTaskState st)
{
  switch (st)
  {
    case eRunning:
      return HEAPTOP_TASK_RUNNING;
    case eReady:
      return HEAPTOP_TASK_READY;
    case eBlocked:
      return HEAPTOP_TASK_BLOCKED;
    case eSuspended:
      return HEAPTOP_TASK_SUSPENDED;
    default:
      return HEAPTOP_TASK_DELETED;
  }
}

#if HEAPTOP_HAS_RUNTIME_STATS
/* Run time of a task over the last interval; a task new since then reports its whole run time. */
static uint64_t _delta(uintptr_t handle, uint64_t now)
{
  uint64_t before = 0;
  if (heaptop_calc_prev_find(s_tasks.prev, s_tasks.prev_count, handle, &before) && now >= before)
    return now - before;
  return now;
}
#endif

static void _sample_cpu_and_stack(heaptop_snapshot_t *s)
{
  configRUN_TIME_COUNTER_TYPE total = 0;
  UBaseType_t n = uxTaskGetSystemState(s_tasks.status, HEAPTOP_STATUS_CAP, &total);
  if (n == 0)
  {
    /* More tasks than even the headroom: nothing was filled. */
    s->tasks_truncated = true;
    s_tasks.have_prev = false;
    return;
  }

#if HEAPTOP_HAS_RUNTIME_STATS
  s->features |= HEAPTOP_FEAT_RUNTIME_STATS;
  const uint64_t wall = s_tasks.have_prev ? (uint64_t)total - s_tasks.prev_wall : 0;
#endif

  for (UBaseType_t i = 0; i < n; i++)
  {
    const TaskStatus_t *ts = &s_tasks.status[i];
    const uintptr_t handle = (uintptr_t)ts->xHandle;
#if HEAPTOP_HAS_RUNTIME_STATS
    const uint64_t delta = _delta(handle, (uint64_t)ts->ulRunTimeCounter);
    s_tasks.cur[i].handle = handle;
    s_tasks.cur[i].counter = (uint64_t)ts->ulRunTimeCounter;

    for (int c = 0; c < s->num_cores; c++)
    {
      if (ts->xHandle == s_tasks.idle[c])
        s->core_load_pct10[c] = heaptop_calc_busy_pct10(delta, wall);
    }
#endif

    if (s->task_count >= HEAPTOP_MAX_TASKS)
    {
      s->tasks_truncated = true;
      continue;
    }
    heaptop_task_stats_t *t = &s->tasks[s->task_count++];
    strlcpy(t->name, ts->pcTaskName, sizeof(t->name));
    t->handle = handle;
    t->state = _state(ts->eCurrentState);
    t->prio = (uint8_t)ts->uxCurrentPriority;
#if configTASKLIST_INCLUDE_COREID == 1
    t->core = ts->xCoreID == tskNO_AFFINITY ? -1 : (int8_t)ts->xCoreID;
#else
    t->core = -1;
#endif
    /* StackType_t is uint8_t on ESP-IDF, so the high-water mark is in bytes. */
    t->stack_hwm = (uint32_t)ts->usStackHighWaterMark;
#if HEAPTOP_HAS_RUNTIME_STATS
    t->cpu_pct10 = s_tasks.have_prev ? heaptop_calc_pct10(delta, wall) : 0;
#endif
  }

#if HEAPTOP_HAS_RUNTIME_STATS
  heaptop_calc_prev_t *swap = s_tasks.prev;
  s_tasks.prev = s_tasks.cur;
  s_tasks.cur = swap;
  s_tasks.prev_count = n;
  s_tasks.prev_wall = (uint64_t)total;
  s_tasks.have_prev = true;
#endif
}

#if CONFIG_HEAP_TASK_TRACKING
static void _sample_task_heap(heaptop_snapshot_t *s)
{
  heap_all_tasks_stat_t all = {
    .task_count = HEAPTOP_TSTAT_CAP,
    .stat_arr = s_tasks.tstat,
    .heap_count = HEAPTOP_TSTAT_CAP * HEAPTOP_HEAPS_PER_TASK,
    .heap_stat_start = s_tasks.hstat,
    .alloc_count = 0, /* no per-block lists: sizes only */
    .alloc_stat_start = NULL,
  };
  if (heap_caps_get_all_task_stat(&all) != ESP_OK)
    return;
  s->features |= HEAPTOP_FEAT_TASK_HEAP;
  /* Deleted tasks stay in IDF's list forever; a full table may hide live tasks. */
  if (all.task_count >= HEAPTOP_TSTAT_CAP)
    s->tasks_truncated = true;

  for (size_t i = 0; i < all.task_count; i++)
  {
    const task_stat_t *ts = &s_tasks.tstat[i];
    heaptop_heap_owner_t *o = &s_tasks.owners[i];
    o->handle = (uintptr_t)ts->handle;
    o->name = ts->name;
    o->alive = ts->is_alive;
    o->cur = (uint32_t)ts->overall_current_usage;
    o->peak = (uint32_t)ts->overall_peak_usage;
    o->psram = 0;
    if (ts->heap_stat != NULL)
    {
      for (size_t h = 0; h < ts->heap_count; h++)
      {
        if (ts->heap_stat[h].caps & MALLOC_CAP_SPIRAM)
          o->psram += (uint32_t)ts->heap_stat[h].current_usage;
      }
    }
  }
  heaptop_calc_merge_heap(s, s_tasks.owners, all.task_count);
}
#endif

#if CONFIG_HEAP_TASK_TRACKING
/* Push each task's heap into its history, flag steady, unreleased growth, and
 * turn IDF's since-boot peak into the peak since the last clear. */
static void _track_history(heaptop_snapshot_t *s)
{
  heaptop_thresholds_t th;
  heaptop_alerts_thresholds(&th);
  s_tasks.sample_no++;
  for (uint16_t i = 0; i < s->task_count; i++)
  {
    heaptop_task_stats_t *t = &s->tasks[i];
    /* A dead task cannot grow, and its handle may already belong to a live one. */
    if (t->state == HEAPTOP_TASK_DELETED)
      continue;
    heaptop_growth_slot_t *slot = heaptop_growth_track(&s_tasks.growth, t->handle, s_tasks.sample_no);
    if (slot == NULL)
      continue;
    heaptop_ring_push(&slot->ring, t->heap_cur);
    const uint16_t n = heaptop_ring_copy(&slot->ring, s_tasks.history, HEAPTOP_HISTORY_LEN);
    t->leak_suspect = heaptop_calc_leak_suspect(s_tasks.history, n, th.task_growth, &t->heap_growth);

    if (slot->rebase)
    {
      slot->peak_base = t->heap_peak;
      slot->peak_max = 0;
      slot->rebase = false;
    }
    if (t->heap_cur > slot->peak_max)
      slot->peak_max = t->heap_cur;
    t->heap_peak = heaptop_calc_peak_since(slot->peak_base, slot->peak_max, t->heap_peak);
  }
}
#endif

void heaptop_tasks_clear(void)
{
#if CONFIG_HEAP_TASK_TRACKING
  heaptop_growth_clear(&s_tasks.growth);
#endif
}

void heaptop_tasks_sample(heaptop_snapshot_t *s)
{
  _sample_cpu_and_stack(s);
#if CONFIG_HEAP_TASK_TRACKING
  _sample_task_heap(s);
  _track_history(s);
#endif
}
