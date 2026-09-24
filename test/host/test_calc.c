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

static void test_rate_scales_to_per_second(void)
{
  TEST_ASSERT_EQUAL_UINT32(100, heaptop_calc_rate_per_s(50, 500));
  TEST_ASSERT_EQUAL_UINT32(0, heaptop_calc_rate_per_s(50, 0));
}

static void test_rate_does_not_overflow(void)
{
  TEST_ASSERT_EQUAL_UINT32(4000000000u, heaptop_calc_rate_per_s(4000000000u, 1000));
}

static void test_delta_u32_survives_counter_wrap(void)
{
  TEST_ASSERT_EQUAL_UINT32(10, heaptop_calc_delta_u32(5, 0xFFFFFFFBu));
  TEST_ASSERT_EQUAL_UINT32(7, heaptop_calc_delta_u32(17, 10));
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

static void test_bucket_boundaries(void)
{
  TEST_ASSERT_EQUAL_UINT8(0, heaptop_calc_bucket(0));
  TEST_ASSERT_EQUAL_UINT8(0, heaptop_calc_bucket(63));
  TEST_ASSERT_EQUAL_UINT8(1, heaptop_calc_bucket(64));
  TEST_ASSERT_EQUAL_UINT8(1, heaptop_calc_bucket(255));
  TEST_ASSERT_EQUAL_UINT8(2, heaptop_calc_bucket(256));
  TEST_ASSERT_EQUAL_UINT8(3, heaptop_calc_bucket(1024));
  TEST_ASSERT_EQUAL_UINT8(4, heaptop_calc_bucket(4096));
  TEST_ASSERT_EQUAL_UINT8(5, heaptop_calc_bucket(16384));
  TEST_ASSERT_EQUAL_UINT8(5, heaptop_calc_bucket(65535));
  TEST_ASSERT_EQUAL_UINT8(6, heaptop_calc_bucket(65536));
  TEST_ASSERT_EQUAL_UINT8(6, heaptop_calc_bucket(8u * 1024u * 1024u));
}

static void test_hist_add_counts_bytes_and_largest(void)
{
  heaptop_frag_hist_t h;
  memset(&h, 0, sizeof(h));
  heaptop_calc_hist_add(&h, 32);
  heaptop_calc_hist_add(&h, 48);
  heaptop_calc_hist_add(&h, 70000);
  TEST_ASSERT_EQUAL_UINT32(2, h.count[0]);
  TEST_ASSERT_EQUAL_UINT32(80, h.bytes[0]);
  TEST_ASSERT_EQUAL_UINT32(1, h.count[6]);
  TEST_ASSERT_EQUAL_UINT32(3, h.free_blocks);
  TEST_ASSERT_EQUAL_UINT32(70080, h.free_bytes);
  TEST_ASSERT_EQUAL_UINT32(70000, h.largest);
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

static void test_leak_add_groups_same_call_stack(void)
{
  heaptop_leak_group_t g[4];
  size_t n = 0;
  const uintptr_t a[] = {0x42001000, 0x42002000, 0x42003000, 0x42004000};
  TEST_ASSERT_TRUE(heaptop_calc_leak_add(g, 4, &n, a, 4, 256));
  TEST_ASSERT_TRUE(heaptop_calc_leak_add(g, 4, &n, a, 4, 16));
  TEST_ASSERT_EQUAL_size_t(1, n);
  TEST_ASSERT_EQUAL_UINT32(2, g[0].count);
  TEST_ASSERT_EQUAL_UINT32(272, g[0].bytes);
  TEST_ASSERT_EQUAL_UINT32(16, g[0].min_size);
  TEST_ASSERT_EQUAL_UINT32(256, g[0].max_size);
  TEST_ASSERT_EQUAL_HEX32(0x42003000, g[0].pc[2]);
}

static void test_leak_add_separates_call_stacks_and_zero_fills(void)
{
  heaptop_leak_group_t g[4];
  size_t n = 0;
  const uintptr_t a[] = {0x42001000, 0x42002000};
  const uintptr_t b[] = {0x42001000, 0x42009000};
  TEST_ASSERT_TRUE(heaptop_calc_leak_add(g, 4, &n, a, 2, 8));
  TEST_ASSERT_TRUE(heaptop_calc_leak_add(g, 4, &n, b, 2, 8));
  TEST_ASSERT_EQUAL_size_t(2, n);
  TEST_ASSERT_EQUAL_HEX32(0, g[0].pc[2]);
  TEST_ASSERT_EQUAL_HEX32(0, g[1].pc[3]);
}

static void test_leak_add_reports_full_table(void)
{
  heaptop_leak_group_t g[1];
  size_t n = 0;
  const uintptr_t a[] = {1};
  const uintptr_t b[] = {2};
  TEST_ASSERT_TRUE(heaptop_calc_leak_add(g, 1, &n, a, 1, 8));
  TEST_ASSERT_FALSE(heaptop_calc_leak_add(g, 1, &n, b, 1, 8));
  TEST_ASSERT_TRUE(heaptop_calc_leak_add(g, 1, &n, a, 1, 8));
  TEST_ASSERT_EQUAL_size_t(1, n);
  TEST_ASSERT_EQUAL_UINT32(2, g[0].count);
}

static void test_leak_sort_by_bytes_descending(void)
{
  heaptop_leak_group_t g[3];
  memset(g, 0, sizeof(g));
  g[0].bytes = 10;
  g[1].bytes = 300;
  g[2].bytes = 20;
  heaptop_calc_leak_sort(g, 3);
  TEST_ASSERT_EQUAL_UINT32(300, g[0].bytes);
  TEST_ASSERT_EQUAL_UINT32(20, g[1].bytes);
  TEST_ASSERT_EQUAL_UINT32(10, g[2].bytes);
}

static void test_leak_suspect_steady_growth(void)
{
  const uint32_t v[] = {100, 200, 300, 400, 500, 600, 700, 800, 900};
  int32_t growth = 0;
  TEST_ASSERT_TRUE(heaptop_calc_leak_suspect(v, 9, 500, &growth));
  TEST_ASSERT_EQUAL_INT32(800, growth);
}

static void test_leak_suspect_needs_enough_samples_and_growth(void)
{
  const uint32_t v[] = {100, 2000, 4000, 6000, 8000, 9000, 9500, 9900, 10000};
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(v, 5, 500, NULL));
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(v, 9, 20000, NULL));
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(v, 9, 0, NULL));
}

static void test_leak_suspect_ignores_dip_below_start(void)
{
  const uint32_t v[] = {500, 400, 600, 800, 1000, 1200, 1300, 1400, 1500};
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(v, 9, 500, NULL));
}

static void test_leak_suspect_ignores_memory_given_back(void)
{
  const uint32_t v[] = {100, 1000, 2000, 3000, 4000, 5000, 5000, 5000, 2000};
  int32_t growth = 0;
  TEST_ASSERT_FALSE(heaptop_calc_leak_suspect(v, 9, 1000, &growth));
  TEST_ASSERT_EQUAL_INT32(1900, growth);
}

static void test_growth_track_reuses_and_claims_slots(void)
{
  heaptop_growth_slot_t slots[2];
  uint32_t mem[2 * 4];
  heaptop_growth_t g;
  heaptop_growth_init(&g, slots, 2, mem, 4);
  heaptop_ring_t *a = heaptop_growth_track(&g, 0xA, 1);
  heaptop_ring_t *b = heaptop_growth_track(&g, 0xB, 1);
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
  heaptop_ring_push(heaptop_growth_track(&g, 0xA, 1), 111);
  heaptop_growth_track(&g, 0xB, 1);
  heaptop_growth_track(&g, 0xB, 2);
  heaptop_ring_t *c = heaptop_growth_track(&g, 0xC, 3);
  TEST_ASSERT_NOT_NULL(c);
  TEST_ASSERT_EQUAL_UINT16(0, c->count);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_leak_add_groups_same_call_stack);
  RUN_TEST(test_leak_add_separates_call_stacks_and_zero_fills);
  RUN_TEST(test_leak_add_reports_full_table);
  RUN_TEST(test_leak_sort_by_bytes_descending);
  RUN_TEST(test_leak_suspect_steady_growth);
  RUN_TEST(test_leak_suspect_needs_enough_samples_and_growth);
  RUN_TEST(test_leak_suspect_ignores_dip_below_start);
  RUN_TEST(test_leak_suspect_ignores_memory_given_back);
  RUN_TEST(test_growth_track_reuses_and_claims_slots);
  RUN_TEST(test_growth_track_evicts_task_not_seen_last_sample);
  RUN_TEST(test_bucket_boundaries);
  RUN_TEST(test_hist_add_counts_bytes_and_largest);
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
  RUN_TEST(test_rate_scales_to_per_second);
  RUN_TEST(test_rate_does_not_overflow);
  RUN_TEST(test_delta_u32_survives_counter_wrap);
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
