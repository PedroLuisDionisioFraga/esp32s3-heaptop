#include <string.h>

#include "heaptop_calc.h"
#include "unity.h"

void setUp(void)
{
}
void tearDown(void)
{
}

static void test_frag_is_zero_when_free_is_one_block(void)
{
  TEST_ASSERT_EQUAL_UINT16(0, heaptop_calc_frag_pct10(4096, 4096));
}

static void test_frag_counts_free_bytes_outside_largest_block(void)
{
  TEST_ASSERT_EQUAL_UINT16(750, heaptop_calc_frag_pct10(1000, 250));
}

static void test_frag_is_zero_when_nothing_is_free(void)
{
  TEST_ASSERT_EQUAL_UINT16(0, heaptop_calc_frag_pct10(0, 0));
}

static void test_frag_clamps_when_largest_exceeds_free(void)
{
  TEST_ASSERT_EQUAL_UINT16(0, heaptop_calc_frag_pct10(100, 200));
}

static void test_pct10_basic_and_zero_whole(void)
{
  TEST_ASSERT_EQUAL_UINT16(500, heaptop_calc_pct10(50, 100));
  TEST_ASSERT_EQUAL_UINT16(123, heaptop_calc_pct10(123, 1000));
  TEST_ASSERT_EQUAL_UINT16(0, heaptop_calc_pct10(5, 0));
}

static void test_pct10_clamps_to_100_percent(void)
{
  TEST_ASSERT_EQUAL_UINT16(1000, heaptop_calc_pct10(300, 100));
}

static void test_pct10_handles_large_64bit_counters(void)
{
  TEST_ASSERT_EQUAL_UINT16(500, heaptop_calc_pct10(3000000000000ULL, 6000000000000ULL));
}

static void test_busy_is_complement_of_idle(void)
{
  TEST_ASSERT_EQUAL_UINT16(750, heaptop_calc_busy_pct10(250, 1000));
  TEST_ASSERT_EQUAL_UINT16(0, heaptop_calc_busy_pct10(1200, 1000));
  TEST_ASSERT_EQUAL_UINT16(0, heaptop_calc_busy_pct10(0, 0));
}

static void test_prev_find_returns_counter_of_matching_handle(void)
{
  const heaptop_calc_prev_t prev[] = {{0x1000, 11}, {0x2000, 22}, {0x3000, 33}};
  uint64_t c = 0;
  TEST_ASSERT_TRUE(heaptop_calc_prev_find(prev, 3, 0x2000, &c));
  TEST_ASSERT_EQUAL_UINT64(22, c);
}

static void test_prev_find_misses_unknown_handle(void)
{
  const heaptop_calc_prev_t prev[] = {{0x1000, 11}};
  uint64_t c = 99;
  TEST_ASSERT_FALSE(heaptop_calc_prev_find(prev, 1, 0x4000, &c));
  TEST_ASSERT_FALSE(heaptop_calc_prev_find(NULL, 0, 0x1000, &c));
}

static heaptop_task_stats_t s_tasks[4];

static void _fill_tasks(void)
{
  memset(s_tasks, 0, sizeof(s_tasks));
  strcpy(s_tasks[0].name, "wifi");
  s_tasks[0].cpu_pct10 = 50;
  s_tasks[0].stack_hwm = 900;
  s_tasks[0].heap_cur = 4000;
  strcpy(s_tasks[1].name, "app");
  s_tasks[1].cpu_pct10 = 300;
  s_tasks[1].stack_hwm = 2000;
  s_tasks[1].heap_cur = 100;
  strcpy(s_tasks[2].name, "IDLE0");
  s_tasks[2].cpu_pct10 = 600;
  s_tasks[2].stack_hwm = 400;
  s_tasks[2].heap_cur = 0;
  strcpy(s_tasks[3].name, "blink");
  s_tasks[3].cpu_pct10 = 50;
  s_tasks[3].stack_hwm = 1500;
  s_tasks[3].heap_cur = 4000;
}

static void test_sort_by_cpu_descending_ties_keep_order(void)
{
  uint8_t idx[4];
  _fill_tasks();
  heaptop_calc_sort_tasks(s_tasks, 4, HEAPTOP_SORT_CPU, idx);
  const uint8_t expected[] = {2, 1, 0, 3};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, idx, 4);
}

static void test_sort_by_stack_ascending(void)
{
  uint8_t idx[4];
  _fill_tasks();
  heaptop_calc_sort_tasks(s_tasks, 4, HEAPTOP_SORT_STACK, idx);
  const uint8_t expected[] = {2, 0, 3, 1};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, idx, 4);
}

static void test_sort_by_heap_descending(void)
{
  uint8_t idx[4];
  _fill_tasks();
  heaptop_calc_sort_tasks(s_tasks, 4, HEAPTOP_SORT_HEAP, idx);
  const uint8_t expected[] = {0, 3, 1, 2};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, idx, 4);
}

static void test_sort_by_name_ignores_case(void)
{
  uint8_t idx[4];
  _fill_tasks();
  heaptop_calc_sort_tasks(s_tasks, 4, HEAPTOP_SORT_NAME, idx);
  const uint8_t expected[] = {1, 3, 2, 0};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, idx, 4);
}

static void test_ring_copies_oldest_first_before_wrap(void)
{
  uint32_t mem[4];
  uint32_t out[4] = {0};
  heaptop_ring_t r;
  heaptop_ring_init(&r, mem, 4);
  heaptop_ring_push(&r, 1);
  heaptop_ring_push(&r, 2);
  heaptop_ring_push(&r, 3);
  TEST_ASSERT_EQUAL_UINT16(3, heaptop_ring_copy(&r, out, 4));
  const uint32_t expected[] = {1, 2, 3};
  TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, out, 3);
}

static void test_ring_keeps_newest_after_wrap(void)
{
  uint32_t mem[3];
  uint32_t out[3] = {0};
  heaptop_ring_t r;
  heaptop_ring_init(&r, mem, 3);
  for (uint32_t v = 1; v <= 5; v++) heaptop_ring_push(&r, v);
  TEST_ASSERT_EQUAL_UINT16(3, heaptop_ring_copy(&r, out, 3));
  const uint32_t expected[] = {3, 4, 5};
  TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, out, 3);
}

static void test_ring_copy_limits_to_newest_values(void)
{
  uint32_t mem[5];
  uint32_t out[2] = {0};
  heaptop_ring_t r;
  heaptop_ring_init(&r, mem, 5);
  for (uint32_t v = 1; v <= 4; v++) heaptop_ring_push(&r, v);
  TEST_ASSERT_EQUAL_UINT16(2, heaptop_ring_copy(&r, out, 2));
  const uint32_t expected[] = {3, 4};
  TEST_ASSERT_EQUAL_UINT32_ARRAY(expected, out, 2);
}

static void test_ring_empty_copies_nothing(void)
{
  uint32_t mem[2];
  uint32_t out[2] = {7, 7};
  heaptop_ring_t r;
  heaptop_ring_init(&r, mem, 2);
  TEST_ASSERT_EQUAL_UINT16(0, heaptop_ring_copy(&r, out, 2));
  TEST_ASSERT_EQUAL_UINT32(7, out[0]);
}

static void _fail_ring(heaptop_fail_t *buf, uint16_t cap, uint16_t pushes, uint16_t *head, uint16_t *count)
{
  *head = 0;
  *count = 0;
  for (uint16_t i = 1; i <= pushes; i++)
  {
    memset(&buf[*head], 0, sizeof(buf[0]));
    buf[*head].size = i;
    *head = (uint16_t)((*head + 1) % cap);
    if (*count < cap)
      (*count)++;
  }
}

static void test_fail_copy_is_newest_first(void)
{
  heaptop_fail_t buf[4], out[4];
  uint16_t head, count;
  _fail_ring(buf, 4, 3, &head, &count);
  TEST_ASSERT_EQUAL_UINT16(3, heaptop_calc_fail_copy(buf, 4, head, count, out, 4));
  TEST_ASSERT_EQUAL_UINT32(3, out[0].size);
  TEST_ASSERT_EQUAL_UINT32(2, out[1].size);
  TEST_ASSERT_EQUAL_UINT32(1, out[2].size);
}

static void test_fail_copy_after_wrap_and_limited(void)
{
  heaptop_fail_t buf[3], out[2];
  uint16_t head, count;
  _fail_ring(buf, 3, 7, &head, &count);
  TEST_ASSERT_EQUAL_UINT16(2, heaptop_calc_fail_copy(buf, 3, head, count, out, 2));
  TEST_ASSERT_EQUAL_UINT32(7, out[0].size);
  TEST_ASSERT_EQUAL_UINT32(6, out[1].size);
}

/* Histories of 21 samples (one per second): long enough for any minimum history rule. */
#define HIST_N 21
static uint32_t s_h[HIST_N];

static void _steady(uint32_t start, uint32_t step)
{
  for (int i = 0; i < HIST_N; i++) s_h[i] = start + step * (uint32_t)i;
}

static void test_leak_suspect_steady_growth(void)
{
  int32_t growth = 0;
  _steady(100, 100);
  TEST_ASSERT_TRUE(heaptop_calc_leak_suspect(s_h, HIST_N, 500, &growth));
  TEST_ASSERT_EQUAL_INT32(2000, growth);
}

static void test_leak_suspect_needs_enough_samples_and_growth(void)
{
  _steady(100, 1000);
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(s_h, 5, 500, NULL));
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(s_h, HIST_N, 30000, NULL));
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(s_h, HIST_N, 0, NULL));
}

static void test_leak_suspect_ignores_dip_below_start(void)
{
  _steady(400, 100);
  s_h[0] = 500;
  s_h[1] = 400; /* below where it started */
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(s_h, HIST_N, 500, NULL));
}

static void test_leak_suspect_ignores_memory_given_back(void)
{
  int32_t growth = 0;
  _steady(100, 1000);
  s_h[HIST_N - 1] = 8100; /* peaked at 19100, gave most of it back */
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(s_h, HIST_N, 1000, &growth));
  TEST_ASSERT_EQUAL_INT32(8000, growth);
}

static void test_leak_suspect_ignores_one_step_then_flat(void)
{
  /* A task that allocates one long-lived buffer (a driver, a worker's stack). */
  _steady(5100, 0);
  s_h[0] = 100;
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(s_h, HIST_N, 4096, NULL));
}

static void test_leak_suspect_ignores_growth_that_stopped(void)
{
  /* Start-up allocations in a few steps, then stable. */
  _steady(6000, 0);
  s_h[0] = 100;
  s_h[1] = 2000;
  s_h[2] = 4000;
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(s_h, HIST_N, 4096, NULL));
}

static void test_leak_suspect_ignores_boot_step_then_small_growth(void)
{
  /* Seen on hardware: `main` after boot (+29 KB in one step), then console
   * history adding a few hundred bytes per command. */
  _steady(30000, 100);
  s_h[0] = 1000;
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(s_h, HIST_N, 4096, NULL));
}

static void test_leak_suspect_catches_staircase_leak(void)
{
  int32_t growth = 0;
  for (int i = 0; i < HIST_N; i++) s_h[i] = 100 + 1000u * (uint32_t)(i / 2);
  TEST_ASSERT_TRUE(heaptop_calc_leak_suspect(s_h, HIST_N, 4096, &growth));
  TEST_ASSERT_EQUAL_INT32(10000, growth);
}

static void test_leak_suspect_ignores_setup_burst_right_after_boot(void)
{
  /* Seen on hardware: 9 s after boot, `main` had grown in every third of its
   * short history (boot, then stress frag, then ht leaks start and a new task). */
  const uint32_t v[] = {1000, 30000, 30100, 30200, 37200, 37300, 37400, 51400, 55500};
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(v, 9, 4096, NULL));
}

static void test_growth_track_reuses_and_claims_slots(void)
{
  heaptop_growth_slot_t slots[2];
  uint32_t mem[2 * 4];
  heaptop_growth_t g;
  heaptop_growth_init(&g, slots, 2, mem, 4);
  heaptop_growth_slot_t *a = heaptop_growth_track(&g, 0xA, 1);
  heaptop_growth_slot_t *b = heaptop_growth_track(&g, 0xB, 1);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_EQUAL_PTR(a, heaptop_growth_track(&g, 0xA, 2));
  TEST_ASSERT_NULL(heaptop_growth_track(&g, 0xC, 2));
}

static void test_growth_track_evicts_task_not_seen_last_sample(void)
{
  heaptop_growth_slot_t slots[2];
  uint32_t mem[2 * 4];
  heaptop_growth_t g;
  heaptop_growth_init(&g, slots, 2, mem, 4);
  heaptop_growth_slot_t *a = heaptop_growth_track(&g, 0xA, 1);
  heaptop_ring_push(&a->ring, 111);
  a->peak_max = 5000;
  heaptop_growth_track(&g, 0xB, 1);
  heaptop_growth_track(&g, 0xB, 2);
  heaptop_growth_slot_t *c = heaptop_growth_track(&g, 0xC, 3);
  TEST_ASSERT_NOT_NULL(c);
  TEST_ASSERT_EQUAL_UINT16(0, c->ring.count);
  TEST_ASSERT_EQUAL_UINT32(0, c->peak_max);
  TEST_ASSERT_FALSE(c->rebase);
}

static void test_growth_clear_empties_history_and_asks_for_new_baseline(void)
{
  heaptop_growth_slot_t slots[2];
  uint32_t mem[2 * 4];
  heaptop_growth_t g;
  heaptop_growth_init(&g, slots, 2, mem, 4);
  heaptop_growth_slot_t *a = heaptop_growth_track(&g, 0xA, 1);
  heaptop_ring_push(&a->ring, 111);
  heaptop_ring_push(&a->ring, 222);
  heaptop_growth_clear(&g);
  TEST_ASSERT_EQUAL_PTR(a, heaptop_growth_track(&g, 0xA, 2));
  TEST_ASSERT_EQUAL_UINT16(0, a->ring.count);
  TEST_ASSERT_TRUE(a->rebase);
}

static void test_peak_since_clear(void)
{
  /* Never cleared: IDF's since-boot peak. */
  TEST_ASSERT_EQUAL_UINT32(9000, heaptop_calc_peak_since(0, 0, 9000));
  /* Cleared at an IDF peak of 9000, sampled at most 3000 since: IDF has no newer record. */
  TEST_ASSERT_EQUAL_UINT32(3000, heaptop_calc_peak_since(9000, 3000, 9000));
  /* A new record after the clear is exact, even if no sample caught it. */
  TEST_ASSERT_EQUAL_UINT32(12000, heaptop_calc_peak_since(9000, 3000, 12000));
}

static heaptop_snapshot_t s_msnap;

static void _live_row(uintptr_t handle, const char *name)
{
  heaptop_task_stats_t *t = &s_msnap.tasks[s_msnap.task_count++];
  memset(t, 0, sizeof(*t));
  strcpy(t->name, name);
  t->handle = handle;
  t->state = HEAPTOP_TASK_BLOCKED;
}

static void test_merge_fills_live_row(void)
{
  memset(&s_msnap, 0, sizeof(s_msnap));
  _live_row(0x10, "app");
  const heaptop_heap_owner_t o[] = {
    {.handle = 0x10, .name = "app", .alive = true, .cur = 500, .peak = 900, .psram = 100}};
  TEST_ASSERT_TRUE(heaptop_calc_merge_heap(&s_msnap, o, 1));
  TEST_ASSERT_EQUAL_UINT16(1, s_msnap.task_count);
  TEST_ASSERT_EQUAL_UINT32(500, s_msnap.tasks[0].heap_cur);
  TEST_ASSERT_EQUAL_UINT32(900, s_msnap.tasks[0].heap_peak);
  TEST_ASSERT_EQUAL_UINT32(100, s_msnap.tasks[0].heap_psram);
}

static void _check_reused_handle(const heaptop_heap_owner_t *o, size_t n)
{
  memset(&s_msnap, 0, sizeof(s_msnap));
  _live_row(0x10, "new");
  TEST_ASSERT_TRUE(heaptop_calc_merge_heap(&s_msnap, o, n));
  TEST_ASSERT_EQUAL_UINT16(2, s_msnap.task_count);
  TEST_ASSERT_EQUAL_UINT32(500, s_msnap.tasks[0].heap_cur);
  TEST_ASSERT_EQUAL_STRING("old", s_msnap.tasks[1].name);
  TEST_ASSERT_EQUAL_UINT8(HEAPTOP_TASK_DELETED, s_msnap.tasks[1].state);
  TEST_ASSERT_EQUAL_UINT32(2048, s_msnap.tasks[1].heap_cur);
  TEST_ASSERT_EQUAL_INT8(-1, s_msnap.tasks[1].core);
}

static void test_merge_keeps_dead_task_apart_when_handle_is_reused(void)
{
  const heaptop_heap_owner_t dead_first[] = {{.handle = 0x10, .name = "old", .alive = false, .cur = 2048, .peak = 2048},
                                             {.handle = 0x10, .name = "new", .alive = true, .cur = 500, .peak = 500}};
  _check_reused_handle(dead_first, 2);
  const heaptop_heap_owner_t alive_first[] = {
    {.handle = 0x10, .name = "new", .alive = true, .cur = 500, .peak = 500},
    {.handle = 0x10, .name = "old", .alive = false, .cur = 2048, .peak = 2048}};
  _check_reused_handle(alive_first, 2);
}

static void test_merge_skips_empty_dead_and_unlisted_alive(void)
{
  memset(&s_msnap, 0, sizeof(s_msnap));
  _live_row(0x10, "app");
  const heaptop_heap_owner_t o[] = {{.handle = 0x20, .name = "gone", .alive = false, .cur = 0},
                                    {.handle = 0, .name = "Pre-scheduler", .alive = true, .cur = 30000}};
  TEST_ASSERT_TRUE(heaptop_calc_merge_heap(&s_msnap, o, 2));
  TEST_ASSERT_EQUAL_UINT16(1, s_msnap.task_count);
}

static void test_merge_reports_full_table(void)
{
  memset(&s_msnap, 0, sizeof(s_msnap));
  for (uint16_t i = 0; i < HEAPTOP_MAX_TASKS; i++) _live_row(0x100 + i, "t");
  const heaptop_heap_owner_t o[] = {{.handle = 0x20, .name = "gone", .alive = false, .cur = 64}};
  TEST_ASSERT_FALSE(heaptop_calc_merge_heap(&s_msnap, o, 1));
  TEST_ASSERT_TRUE(s_msnap.tasks_truncated);
  TEST_ASSERT_EQUAL_UINT16(HEAPTOP_MAX_TASKS, s_msnap.task_count);
}

static void test_below_floor_fires_and_clears_with_hysteresis(void)
{
  TEST_ASSERT_FALSE(heaptop_calc_below_floor(false, 1000, 1000, 10));
  TEST_ASSERT_TRUE(heaptop_calc_below_floor(false, 999, 1000, 10));
  TEST_ASSERT_TRUE(heaptop_calc_below_floor(true, 1050, 1000, 10));
  TEST_ASSERT_FALSE(heaptop_calc_below_floor(true, 1100, 1000, 10));
  TEST_ASSERT_FALSE(heaptop_calc_below_floor(false, 1, 0, 10));
}

static void test_above_ceiling_fires_and_clears_with_hysteresis(void)
{
  TEST_ASSERT_FALSE(heaptop_calc_above_ceiling(false, 800, 800, 10));
  TEST_ASSERT_TRUE(heaptop_calc_above_ceiling(false, 801, 800, 10));
  TEST_ASSERT_TRUE(heaptop_calc_above_ceiling(true, 750, 800, 10));
  TEST_ASSERT_FALSE(heaptop_calc_above_ceiling(true, 720, 800, 10));
  TEST_ASSERT_FALSE(heaptop_calc_above_ceiling(false, 1000, 0, 10));
}

static heaptop_snapshot_t s_asnap;

static heaptop_thresholds_t _th(void)
{
  return (heaptop_thresholds_t){.dram_free_min = 20000,
                                .dram_largest_min = 8000,
                                .frag_pct_max = 80,
                                .psram_free_min = 65536,
                                .stack_hwm_min = 512,
                                .task_growth = 4096};
}

static void _healthy(void)
{
  memset(&s_asnap, 0, sizeof(s_asnap));
  s_asnap.region[HEAPTOP_REGION_INTERNAL] =
    (heaptop_region_stats_t){.present = true, .free = 100000, .largest = 60000, .frag_pct10 = 400};
  s_asnap.region[HEAPTOP_REGION_PSRAM] = (heaptop_region_stats_t){.present = true, .free = 4000000};
  s_asnap.task_count = 2;
  s_asnap.tasks[0].stack_hwm = 2000;
  s_asnap.tasks[1].stack_hwm = 3000;
  s_asnap.features = HEAPTOP_FEAT_FAIL_CB;
  s_asnap.failures = 5;
}

static void test_alerts_quiet_when_healthy(void)
{
  const heaptop_thresholds_t th = _th();
  _healthy();
  TEST_ASSERT_EQUAL_HEX32(0, heaptop_calc_alerts(&th, &s_asnap, 0, 5));
}

static void test_alerts_each_condition(void)
{
  const heaptop_thresholds_t th = _th();
  _healthy();
  s_asnap.region[HEAPTOP_REGION_INTERNAL].free = 15000;
  s_asnap.region[HEAPTOP_REGION_INTERNAL].largest = 4000;
  s_asnap.region[HEAPTOP_REGION_INTERNAL].frag_pct10 = 850;
  s_asnap.region[HEAPTOP_REGION_PSRAM].free = 1000;
  s_asnap.tasks[1].stack_hwm = 300;
  s_asnap.tasks[0].leak_suspect = true;
  s_asnap.failures = 6;
  const uint32_t all = HEAPTOP_ALERT_DRAM_FREE | HEAPTOP_ALERT_DRAM_LARGEST | HEAPTOP_ALERT_FRAG |
                       HEAPTOP_ALERT_PSRAM_FREE | HEAPTOP_ALERT_STACK | HEAPTOP_ALERT_LEAK | HEAPTOP_ALERT_ALLOC_FAIL;
  TEST_ASSERT_EQUAL_HEX32(all, heaptop_calc_alerts(&th, &s_asnap, 0, 5));
}

static void test_alerts_skip_missing_psram_and_deleted_tasks(void)
{
  const heaptop_thresholds_t th = _th();
  _healthy();
  s_asnap.region[HEAPTOP_REGION_PSRAM] = (heaptop_region_stats_t){.present = false};
  s_asnap.tasks[1].state = HEAPTOP_TASK_DELETED;
  s_asnap.tasks[1].stack_hwm = 0;
  TEST_ASSERT_EQUAL_HEX32(0, heaptop_calc_alerts(&th, &s_asnap, 0, 5));
}

static void test_alerts_keep_state_inside_hysteresis(void)
{
  const heaptop_thresholds_t th = _th();
  _healthy();
  s_asnap.region[HEAPTOP_REGION_INTERNAL].free = 21000;
  TEST_ASSERT_EQUAL_HEX32(HEAPTOP_ALERT_DRAM_FREE, heaptop_calc_alerts(&th, &s_asnap, HEAPTOP_ALERT_DRAM_FREE, 5));
  TEST_ASSERT_EQUAL_HEX32(0, heaptop_calc_alerts(&th, &s_asnap, 0, 5));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_merge_fills_live_row);
  RUN_TEST(test_merge_keeps_dead_task_apart_when_handle_is_reused);
  RUN_TEST(test_merge_skips_empty_dead_and_unlisted_alive);
  RUN_TEST(test_merge_reports_full_table);
  RUN_TEST(test_below_floor_fires_and_clears_with_hysteresis);
  RUN_TEST(test_above_ceiling_fires_and_clears_with_hysteresis);
  RUN_TEST(test_alerts_quiet_when_healthy);
  RUN_TEST(test_alerts_each_condition);
  RUN_TEST(test_alerts_skip_missing_psram_and_deleted_tasks);
  RUN_TEST(test_alerts_keep_state_inside_hysteresis);
  RUN_TEST(test_leak_suspect_steady_growth);
  RUN_TEST(test_leak_suspect_needs_enough_samples_and_growth);
  RUN_TEST(test_leak_suspect_ignores_dip_below_start);
  RUN_TEST(test_leak_suspect_ignores_memory_given_back);
  RUN_TEST(test_leak_suspect_ignores_one_step_then_flat);
  RUN_TEST(test_leak_suspect_ignores_growth_that_stopped);
  RUN_TEST(test_leak_suspect_ignores_boot_step_then_small_growth);
  RUN_TEST(test_leak_suspect_catches_staircase_leak);
  RUN_TEST(test_leak_suspect_ignores_setup_burst_right_after_boot);
  RUN_TEST(test_growth_track_reuses_and_claims_slots);
  RUN_TEST(test_growth_track_evicts_task_not_seen_last_sample);
  RUN_TEST(test_growth_clear_empties_history_and_asks_for_new_baseline);
  RUN_TEST(test_peak_since_clear);
  RUN_TEST(test_fail_copy_is_newest_first);
  RUN_TEST(test_fail_copy_after_wrap_and_limited);
  RUN_TEST(test_frag_is_zero_when_free_is_one_block);
  RUN_TEST(test_frag_counts_free_bytes_outside_largest_block);
  RUN_TEST(test_frag_is_zero_when_nothing_is_free);
  RUN_TEST(test_frag_clamps_when_largest_exceeds_free);
  RUN_TEST(test_pct10_basic_and_zero_whole);
  RUN_TEST(test_pct10_clamps_to_100_percent);
  RUN_TEST(test_pct10_handles_large_64bit_counters);
  RUN_TEST(test_busy_is_complement_of_idle);
  RUN_TEST(test_prev_find_returns_counter_of_matching_handle);
  RUN_TEST(test_prev_find_misses_unknown_handle);
  RUN_TEST(test_sort_by_cpu_descending_ties_keep_order);
  RUN_TEST(test_sort_by_stack_ascending);
  RUN_TEST(test_sort_by_heap_descending);
  RUN_TEST(test_sort_by_name_ignores_case);
  RUN_TEST(test_ring_copies_oldest_first_before_wrap);
  RUN_TEST(test_ring_keeps_newest_after_wrap);
  RUN_TEST(test_ring_copy_limits_to_newest_values);
  RUN_TEST(test_ring_empty_copies_nothing);
  return UNITY_END();
}
