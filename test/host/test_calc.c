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

int main(void)
{
  UNITY_BEGIN();
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
