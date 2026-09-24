#include <string.h>

#include "heaptop_render.h"
#include "unity.h"

static char s_mem[4096];
static heaptop_buf_t s_buf;
static heaptop_snapshot_t s_snap;

void setUp(void)
{
  memset(s_mem, 0x7E, sizeof(s_mem));
  memset(&s_snap, 0, sizeof(s_snap));
  heaptop_buf_init(&s_buf, s_mem, sizeof(s_mem));
}

void tearDown(void)
{
}

/* Line of the rendered output that contains needle, copied into out. */
static const char *_line_with(const char *text, const char *needle, char *out, size_t len)
{
  const char *hit = strstr(text, needle);
  if (hit == NULL)
    return NULL;
  const char *start = hit;
  while (start > text && start[-1] != '\n') start--;
  const char *end = strchr(hit, '\n');
  size_t n = end ? (size_t)(end - start) : strlen(start);
  if (n >= len)
    n = len - 1;
  memcpy(out, start, n);
  out[n] = '\0';
  return out;
}

static void test_fmt_bytes_units(void)
{
  char out[16];
  heaptop_fmt_bytes(out, sizeof(out), 0);
  TEST_ASSERT_EQUAL_STRING("0", out);
  heaptop_fmt_bytes(out, sizeof(out), 1023);
  TEST_ASSERT_EQUAL_STRING("1023", out);
  heaptop_fmt_bytes(out, sizeof(out), 1024);
  TEST_ASSERT_EQUAL_STRING("1.0K", out);
  heaptop_fmt_bytes(out, sizeof(out), 1536);
  TEST_ASSERT_EQUAL_STRING("1.5K", out);
  heaptop_fmt_bytes(out, sizeof(out), 327680);
  TEST_ASSERT_EQUAL_STRING("320.0K", out);
  heaptop_fmt_bytes(out, sizeof(out), 8388608);
  TEST_ASSERT_EQUAL_STRING("8.0M", out);
}

static void test_buf_appends_formatted_text(void)
{
  heaptop_buf_printf(&s_buf, "a=%d ", 1);
  heaptop_buf_printf(&s_buf, "b=%s", "two");
  TEST_ASSERT_EQUAL_STRING("a=1 b=two", s_mem);
  TEST_ASSERT_EQUAL_size_t(9, s_buf.len);
  TEST_ASSERT_FALSE(s_buf.truncated);
}

static void test_buf_truncates_without_overflow(void)
{
  char small[8];
  memset(small, 0x7E, sizeof(small));
  heaptop_buf_t b;
  heaptop_buf_init(&b, small, 6);
  heaptop_buf_printf(&b, "hello world");
  heaptop_buf_printf(&b, "more");
  TEST_ASSERT_EQUAL_STRING("hello", small);
  TEST_ASSERT_EQUAL_size_t(5, b.len);
  TEST_ASSERT_TRUE(b.truncated);
  TEST_ASSERT_EQUAL_HEX8(0x7E, (uint8_t)small[6]);
}

static void _fill_regions(void)
{
  heaptop_region_stats_t *in = &s_snap.region[HEAPTOP_REGION_INTERNAL];
  in->present = true;
  in->total = 327680;
  in->free = 204800;
  in->min_free = 184320;
  in->largest = 110592;
  in->used_blocks = 412;
  in->free_blocks = 23;
  in->frag_pct10 = 460;
  s_snap.region[HEAPTOP_REGION_DMA].present = true;
  s_snap.region[HEAPTOP_REGION_PSRAM].present = false;
}

static void test_heap_table_shows_region_values(void)
{
  char line[256];
  _fill_regions();
  heaptop_render_heap(&s_buf, &s_snap);
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "internal", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, "320.0K"));
  TEST_ASSERT_NOT_NULL(strstr(line, "200.0K"));
  TEST_ASSERT_NOT_NULL(strstr(line, "180.0K"));
  TEST_ASSERT_NOT_NULL(strstr(line, "108.0K"));
  TEST_ASSERT_NOT_NULL(strstr(line, "46.0%"));
  TEST_ASSERT_NOT_NULL(strstr(line, "412/23"));
}

static void test_heap_table_marks_missing_region(void)
{
  char line[256];
  _fill_regions();
  heaptop_render_heap(&s_buf, &s_snap);
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "psram", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, "not present"));
}

static void _add_task(const char *name, uint16_t cpu, uint32_t hwm, uint32_t heap, uint8_t state)
{
  heaptop_task_stats_t *t = &s_snap.tasks[s_snap.task_count++];
  strcpy(t->name, name);
  t->cpu_pct10 = cpu;
  t->stack_hwm = hwm;
  t->heap_cur = heap;
  t->heap_peak = heap;
  t->state = state;
  t->core = -1;
}

static void test_task_table_orders_by_key(void)
{
  s_snap.features = HEAPTOP_FEAT_RUNTIME_STATS | HEAPTOP_FEAT_TASK_HEAP;
  _add_task("slow", 10, 3000, 100, HEAPTOP_TASK_BLOCKED);
  _add_task("busy", 873, 2000, 5000, HEAPTOP_TASK_RUNNING);
  heaptop_render_tasks(&s_buf, &s_snap, HEAPTOP_SORT_CPU);
  const char *busy = strstr(s_mem, "busy");
  const char *slow = strstr(s_mem, "slow");
  TEST_ASSERT_NOT_NULL(busy);
  TEST_ASSERT_NOT_NULL(slow);
  TEST_ASSERT_TRUE(busy < slow);
  char line[256];
  _line_with(s_mem, "busy", line, sizeof(line));
  TEST_ASSERT_NOT_NULL(strstr(line, "87.3"));
  TEST_ASSERT_NOT_NULL(strstr(line, "4.8K"));
}

static void test_task_table_shows_deleted_task_holding_heap(void)
{
  s_snap.features = HEAPTOP_FEAT_TASK_HEAP;
  _add_task("gone", 0, 0, 2048, HEAPTOP_TASK_DELETED);
  heaptop_render_tasks(&s_buf, &s_snap, HEAPTOP_SORT_HEAP);
  char line[256];
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "gone", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, " X "));
}

static void test_task_table_dashes_unavailable_columns(void)
{
  s_snap.features = 0;
  _add_task("solo", 500, 1000, 0, HEAPTOP_TASK_READY);
  heaptop_render_tasks(&s_buf, &s_snap, HEAPTOP_SORT_NAME);
  char line[256];
  _line_with(s_mem, "solo", line, sizeof(line));
  TEST_ASSERT_NULL(strstr(line, "50.0"));
  TEST_ASSERT_NOT_NULL(strstr(line, "-"));
}

static void test_task_table_flags_leak_suspect(void)
{
  s_snap.features = HEAPTOP_FEAT_TASK_HEAP;
  _add_task("leaky", 0, 1000, 9000, HEAPTOP_TASK_BLOCKED);
  s_snap.tasks[0].leak_suspect = true;
  heaptop_render_tasks(&s_buf, &s_snap, HEAPTOP_SORT_HEAP);
  char line[256];
  _line_with(s_mem, "leaky", line, sizeof(line));
  TEST_ASSERT_NOT_NULL(strstr(line, "LEAK?"));
}

static void test_fmt_uptime(void)
{
  char out[24];
  heaptop_fmt_uptime(out, sizeof(out), 59ULL * 1000000u);
  TEST_ASSERT_EQUAL_STRING("59s", out);
  heaptop_fmt_uptime(out, sizeof(out), 125ULL * 1000000u);
  TEST_ASSERT_EQUAL_STRING("2m05s", out);
  heaptop_fmt_uptime(out, sizeof(out), 3723ULL * 1000000u);
  TEST_ASSERT_EQUAL_STRING("1h02m03s", out);
  heaptop_fmt_uptime(out, sizeof(out), 90061ULL * 1000000u);
  TEST_ASSERT_EQUAL_STRING("1d01h01m", out);
}

static void test_sparkline_rises_with_values(void)
{
  const uint32_t v[] = {10, 20, 30, 40, 50};
  char out[16];
  heaptop_render_sparkline(out, sizeof(out), v, 5);
  TEST_ASSERT_EQUAL_size_t(5, strlen(out));
  for (int i = 1; i < 5; i++)
    TEST_ASSERT_TRUE(strchr(HEAPTOP_SPARK_LEVELS, out[i]) >= strchr(HEAPTOP_SPARK_LEVELS, out[i - 1]));
  TEST_ASSERT_EQUAL_CHAR(HEAPTOP_SPARK_LEVELS[0], out[0]);
  TEST_ASSERT_EQUAL_CHAR(HEAPTOP_SPARK_LEVELS[sizeof(HEAPTOP_SPARK_LEVELS) - 2], out[4]);
}

static void test_sparkline_flat_series_is_uniform(void)
{
  const uint32_t v[] = {7, 7, 7};
  char out[8];
  heaptop_render_sparkline(out, sizeof(out), v, 3);
  TEST_ASSERT_EQUAL_size_t(3, strlen(out));
  TEST_ASSERT_EQUAL_CHAR(out[0], out[1]);
  TEST_ASSERT_EQUAL_CHAR(out[1], out[2]);
}

static void test_sparkline_fits_buffer(void)
{
  const uint32_t v[] = {1, 2, 3, 4, 5, 6};
  char out[4];
  heaptop_render_sparkline(out, sizeof(out), v, 6);
  TEST_ASSERT_EQUAL_size_t(3, strlen(out));
  heaptop_render_sparkline(out, sizeof(out), v, 0);
  TEST_ASSERT_EQUAL_STRING("", out);
}

static void _fill_top(void)
{
  _fill_regions();
  s_snap.seq = 42;
  s_snap.uptime_us = 3723ULL * 1000000u;
  s_snap.self_us = 850;
  s_snap.period_ms = 1000;
  s_snap.num_cores = 2;
  s_snap.core_load_pct10[0] = 523;
  s_snap.core_load_pct10[1] = 140;
  s_snap.features = HEAPTOP_FEAT_RUNTIME_STATS | HEAPTOP_FEAT_TASK_HEAP;
  _add_task("main", 10, 3000, 100, HEAPTOP_TASK_RUNNING);
}

static void test_top_header_shows_uptime_sample_and_cost(void)
{
  const heaptop_top_view_t view = {.sort = HEAPTOP_SORT_CPU, .paused = false, .refresh_ms = 500};
  _fill_top();
  heaptop_render_top(&s_buf, &s_snap, &view);
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "refresh 500ms"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "+/-"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "up 1h02m03s"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "#42"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "850us"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "sort cpu"));
  TEST_ASSERT_NULL(strstr(s_mem, "PAUSED"));
}

static void test_top_shows_one_bar_per_core(void)
{
  const heaptop_top_view_t view = {.sort = HEAPTOP_SORT_CPU};
  char line[256];
  _fill_top();
  heaptop_render_top(&s_buf, &s_snap, &view);
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "cpu0", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, "52.3%"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "cpu1"));

  heaptop_buf_init(&s_buf, s_mem, sizeof(s_mem));
  s_snap.num_cores = 1;
  heaptop_render_top(&s_buf, &s_snap, &view);
  TEST_ASSERT_NULL(strstr(s_mem, "cpu1"));
}

static void test_top_omits_cpu_bars_without_runtime_stats(void)
{
  const heaptop_top_view_t view = {.sort = HEAPTOP_SORT_HEAP};
  _fill_top();
  s_snap.features = HEAPTOP_FEAT_TASK_HEAP;
  heaptop_render_top(&s_buf, &s_snap, &view);
  TEST_ASSERT_NULL(strstr(s_mem, "cpu0"));
}

static void test_top_shows_regions_trend_and_tasks(void)
{
  const heaptop_top_view_t view = {.sort = HEAPTOP_SORT_CPU};
  char line[256];
  _fill_top();
  s_snap.trend_len = 3;
  s_snap.trend[HEAPTOP_TREND_INTERNAL_FREE][0] = 100;
  s_snap.trend[HEAPTOP_TREND_INTERNAL_FREE][1] = 200;
  s_snap.trend[HEAPTOP_TREND_INTERNAL_FREE][2] = 300;
  heaptop_render_top(&s_buf, &s_snap, &view);
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "internal", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, "200.0K"));
  TEST_ASSERT_NOT_NULL(strstr(line, "46.0%"));
  TEST_ASSERT_NULL(strstr(s_mem, "psram"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "NAME"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "main"));
}

static void test_top_marks_paused(void)
{
  const heaptop_top_view_t view = {.sort = HEAPTOP_SORT_STACK, .paused = true};
  _fill_top();
  heaptop_render_top(&s_buf, &s_snap, &view);
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "PAUSED"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "sort stack"));
}

static void test_top_shows_alloc_rates_when_hooks_on(void)
{
  const heaptop_top_view_t view = {.sort = HEAPTOP_SORT_CPU};
  _fill_top();
  s_snap.features |= HEAPTOP_FEAT_ALLOC_HOOKS;
  s_snap.alloc.allocs_per_s = 120;
  heaptop_render_top(&s_buf, &s_snap, &view);
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "allocs/s 120"));

  heaptop_buf_init(&s_buf, s_mem, sizeof(s_mem));
  s_snap.features &= ~HEAPTOP_FEAT_ALLOC_HOOKS;
  heaptop_render_top(&s_buf, &s_snap, &view);
  TEST_ASSERT_NULL(strstr(s_mem, "allocs/s"));
}

static void test_frag_histogram_rows(void)
{
  heaptop_frag_hist_t h;
  memset(&h, 0, sizeof(h));
  h.count[0] = 5;
  h.bytes[0] = 200;
  h.count[6] = 1;
  h.bytes[6] = 110592;
  h.free_blocks = 6;
  h.free_bytes = 110792;
  h.largest = 110592;
  char line[256];
  heaptop_render_frag(&s_buf, "internal", &h);
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "internal"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "6 free blocks"));
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "<64 ", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, " 5 "));
  TEST_ASSERT_NOT_NULL(strstr(line, "200"));
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, ">=64K", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, "108.0K"));
  TEST_ASSERT_NOT_NULL(strstr(line, "#"));
}

static void test_frag_histogram_empty_region(void)
{
  heaptop_frag_hist_t h;
  memset(&h, 0, sizeof(h));
  heaptop_render_frag(&s_buf, "psram", &h);
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "no free blocks"));
}

static void _fill_allocs(void)
{
  s_snap.features = HEAPTOP_FEAT_ALLOC_HOOKS | HEAPTOP_FEAT_FAIL_CB;
  s_snap.uptime_us = 20ULL * 1000000u;
  s_snap.alloc.allocs_per_s = 120;
  s_snap.alloc.frees_per_s = 118;
  s_snap.alloc.bytes_per_s = 4608;
  s_snap.alloc.failures = 3;
  _add_task("stress", 0, 1000, 0, HEAPTOP_TASK_BLOCKED);
  s_snap.tasks[0].handle = 0x3FC90000u;
}

static void test_allocs_rates_line(void)
{
  _fill_allocs();
  heaptop_render_allocs(&s_buf, &s_snap, NULL, 0);
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "allocs/s 120"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "frees/s 118"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "4.5K"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "failures 3"));
}

static void test_allocs_failure_rows_resolve_task_and_isr(void)
{
  heaptop_fail_t f[3];
  memset(f, 0, sizeof(f));
  f[0] = (heaptop_fail_t){.t_us = 8000000u,
                          .size = 10000000u,
                          .caps = 0x1800,
                          .func = "heap_caps_malloc",
                          .task = 0x3FC90000u};
  f[1] = (heaptop_fail_t){.t_us = 5000000u, .size = 64, .caps = 0x8, .func = "heap_caps_calloc", .isr = true};
  f[2] = (heaptop_fail_t){.t_us = 1000000u, .size = 128, .caps = 0x4, .func = "heap_caps_realloc", .task = 0x3FC9FFF0u};
  char line[256];
  _fill_allocs();
  heaptop_render_allocs(&s_buf, &s_snap, f, 3);
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "heap_caps_malloc", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, "stress"));
  TEST_ASSERT_NOT_NULL(strstr(line, "9.5M"));
  TEST_ASSERT_NOT_NULL(strstr(line, "12.0s"));
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "heap_caps_calloc", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, "(ISR)"));
  TEST_ASSERT_NOT_NULL(_line_with(s_mem, "heap_caps_realloc", line, sizeof(line)));
  TEST_ASSERT_NOT_NULL(strstr(line, "0x3fc9fff0"));
}

static void test_allocs_reports_disabled_sources(void)
{
  s_snap.features = 0;
  heaptop_render_allocs(&s_buf, &s_snap, NULL, 0);
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "CONFIG_HEAP_USE_HOOKS"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "CONFIG_HEAPTOP_FAILED_ALLOC_CALLBACK"));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_top_shows_alloc_rates_when_hooks_on);
  RUN_TEST(test_frag_histogram_rows);
  RUN_TEST(test_frag_histogram_empty_region);
  RUN_TEST(test_allocs_rates_line);
  RUN_TEST(test_allocs_failure_rows_resolve_task_and_isr);
  RUN_TEST(test_allocs_reports_disabled_sources);
  RUN_TEST(test_fmt_bytes_units);
  RUN_TEST(test_buf_appends_formatted_text);
  RUN_TEST(test_buf_truncates_without_overflow);
  RUN_TEST(test_heap_table_shows_region_values);
  RUN_TEST(test_heap_table_marks_missing_region);
  RUN_TEST(test_task_table_orders_by_key);
  RUN_TEST(test_task_table_shows_deleted_task_holding_heap);
  RUN_TEST(test_task_table_dashes_unavailable_columns);
  RUN_TEST(test_task_table_flags_leak_suspect);
  RUN_TEST(test_fmt_uptime);
  RUN_TEST(test_sparkline_rises_with_values);
  RUN_TEST(test_sparkline_flat_series_is_uniform);
  RUN_TEST(test_sparkline_fits_buffer);
  RUN_TEST(test_top_header_shows_uptime_sample_and_cost);
  RUN_TEST(test_top_shows_one_bar_per_core);
  RUN_TEST(test_top_omits_cpu_bars_without_runtime_stats);
  RUN_TEST(test_top_shows_regions_trend_and_tasks);
  RUN_TEST(test_top_marks_paused);
  return UNITY_END();
}
