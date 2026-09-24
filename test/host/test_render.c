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

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_fmt_bytes_units);
  RUN_TEST(test_buf_appends_formatted_text);
  RUN_TEST(test_buf_truncates_without_overflow);
  RUN_TEST(test_heap_table_shows_region_values);
  RUN_TEST(test_heap_table_marks_missing_region);
  RUN_TEST(test_task_table_orders_by_key);
  RUN_TEST(test_task_table_shows_deleted_task_holding_heap);
  RUN_TEST(test_task_table_dashes_unavailable_columns);
  RUN_TEST(test_task_table_flags_leak_suspect);
  return UNITY_END();
}
