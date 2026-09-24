#include <string.h>

#include "heaptop_stream.h"
#include "unity.h"

static char s_mem[4096];
static heaptop_buf_t s_buf;
static heaptop_snapshot_t s_snap;

void setUp(void)
{
  memset(&s_snap, 0, sizeof(s_snap));
  heaptop_buf_init(&s_buf, s_mem, sizeof(s_mem));
}

void tearDown(void)
{
}

static int _count_lines(const char *text)
{
  int n = 0;
  for (; *text; text++)
    if (*text == '\n')
      n++;
  return n;
}

/* Minimal structural check: every line is {...} with balanced braces/brackets outside strings. */
static void _assert_json_lines(const char *text)
{
  const char *line = text;
  while (*line)
  {
    const char *nl = strchr(line, '\n');
    TEST_ASSERT_NOT_NULL_MESSAGE(nl, "line not terminated");
    TEST_ASSERT_EQUAL_CHAR('{', line[0]);
    TEST_ASSERT_EQUAL_CHAR('}', nl[-1]);
    TEST_ASSERT_EQUAL_INT(0, strncmp(line, "{\"ht\":1,", 8));
    int depth = 0;
    bool in_str = false;
    for (const char *p = line; p < nl; p++)
    {
      if (in_str)
      {
        if (*p == '\\')
          p++;
        else if (*p == '"')
          in_str = false;
        continue;
      }
      if (*p == '"')
        in_str = true;
      else if (*p == '{' || *p == '[')
        depth++;
      else if (*p == '}' || *p == ']')
        depth--;
      TEST_ASSERT_TRUE(depth >= 0);
    }
    TEST_ASSERT_FALSE(in_str);
    TEST_ASSERT_EQUAL_INT(0, depth);
    line = nl + 1;
  }
}

static void _fill(void)
{
  s_snap.seq = 42;
  s_snap.uptime_us = 3723000000ULL;
  s_snap.dt_ms = 1000;
  s_snap.self_us = 850;
  s_snap.num_cores = 2;
  s_snap.core_load_pct10[0] = 523;
  s_snap.core_load_pct10[1] = 140;
  s_snap.features =
    HEAPTOP_FEAT_RUNTIME_STATS | HEAPTOP_FEAT_TASK_HEAP | HEAPTOP_FEAT_ALLOC_HOOKS | HEAPTOP_FEAT_FAIL_CB;
  s_snap.region[HEAPTOP_REGION_INTERNAL] = (heaptop_region_stats_t){.present = true,
                                                                    .total = 327680,
                                                                    .free = 204800,
                                                                    .min_free = 184320,
                                                                    .largest = 110592,
                                                                    .used_blocks = 412,
                                                                    .free_blocks = 23,
                                                                    .frag_pct10 = 460};
  s_snap.alloc = (heaptop_alloc_stats_t){.allocs_per_s = 120, .frees_per_s = 118, .bytes_per_s = 4608, .failures = 3};
  s_snap.alerts = HEAPTOP_ALERT_LEAK;
}

static void test_sample_line_fields(void)
{
  _fill();
  heaptop_stream_sample(&s_buf, &s_snap);
  _assert_json_lines(s_mem);
  TEST_ASSERT_EQUAL_INT(1, _count_lines(s_mem));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"type\":\"sample\""));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"seq\":42"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"t_ms\":3723000"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"cpu10\":[523,140]"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"internal\":{\"total\":327680,\"free\":204800"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"frag10\":460"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"psram\":null"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"allocs_s\":120"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"failures\":3"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"alerts\":[\"leak\"]"));
}

static void test_sample_line_nulls_missing_sources(void)
{
  _fill();
  s_snap.features = 0;
  s_snap.alerts = 0;
  heaptop_stream_sample(&s_buf, &s_snap);
  _assert_json_lines(s_mem);
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"cpu10\":null"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"allocs_s\":null"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"failures\":null"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"alerts\":[]"));
}

static void test_task_lines_escape_names(void)
{
  _fill();
  s_snap.task_count = 2;
  strcpy(s_snap.tasks[0].name, "main");
  s_snap.tasks[0].handle = 0x3FC90000u;
  s_snap.tasks[0].cpu_pct10 = 10;
  s_snap.tasks[0].stack_hwm = 3000;
  s_snap.tasks[0].heap_cur = 100;
  s_snap.tasks[0].core = -1;
  strcpy(s_snap.tasks[1].name, "we\"ird\\x");
  s_snap.tasks[1].state = HEAPTOP_TASK_DELETED;
  s_snap.tasks[1].leak_suspect = true;
  s_snap.tasks[1].heap_growth = -5;
  heaptop_stream_tasks(&s_buf, &s_snap);
  _assert_json_lines(s_mem);
  TEST_ASSERT_EQUAL_INT(2, _count_lines(s_mem));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"name\":\"main\""));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"core\":null"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"hwm\":3000"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"name\":\"we\\\"ird\\\\x\""));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"state\":\"deleted\""));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"leak\":true"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"growth\":-5"));
}

static void test_alert_and_fail_lines(void)
{
  _fill();
  heaptop_stream_alert(&s_buf, &s_snap, HEAPTOP_ALERT_DRAM_FREE, true, "internal RAM free 18.0K below 20.0K");
  const heaptop_fail_t f = {.t_us = 3722000000ULL,
                            .size = 4096,
                            .caps = 0x1800,
                            .func = "heap_caps_malloc",
                            .isr = true};
  heaptop_stream_fail(&s_buf, &s_snap, &f);
  _assert_json_lines(s_mem);
  TEST_ASSERT_EQUAL_INT(2, _count_lines(s_mem));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"type\":\"alert\",\"t_ms\":3723000,\"alert\":\"dram_free\",\"active\":true"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"msg\":\"internal RAM free 18.0K below 20.0K\""));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"type\":\"fail\",\"t_ms\":3722000,\"size\":4096,\"caps\":6144"));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"func\":\"heap_caps_malloc\""));
  TEST_ASSERT_NOT_NULL(strstr(s_mem, "\"isr\":true"));
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_sample_line_fields);
  RUN_TEST(test_sample_line_nulls_missing_sources);
  RUN_TEST(test_task_lines_escape_names);
  RUN_TEST(test_alert_and_fail_lines);
  return UNITY_END();
}
