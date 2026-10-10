#include <stdint.h>
#include <string.h>

#include "heaptop_json.h"
#include "heaptop_stream.h"
#include "unity.h"

#define OUT_BYTES  16384
#define CHUNK_MAX  512 /* the writer's buffer */
#define REGION_MAX UINT32_MAX

typedef struct sink_state
{
  char out[OUT_BYTES];
  size_t len;
  size_t max_chunk;
  int calls;     /* sink calls, including one that was refused */
  int chunks;    /* chunks accepted */
  int refuse_at; /* refuse the call with this number (1-based); 0 = never */
} sink_state_t;

static sink_state_t s_sink;
static heaptop_snapshot_t s_snap;
static heaptop_thresholds_t s_limits;

void setUp(void)
{
  memset(&s_sink, 0, sizeof(s_sink));
  memset(&s_snap, 0, sizeof(s_snap));
  s_limits = (heaptop_thresholds_t){.dram_free_min = 20480,
                                    .dram_largest_min = 8192,
                                    .frag_pct_max = 80,
                                    .psram_free_min = 65536,
                                    .stack_hwm_min = 256,
                                    .task_growth = 4096};
}

void tearDown(void)
{
}

static bool _sink(const char *data, size_t len, void *ctx)
{
  sink_state_t *st = ctx;
  st->calls++;
  if (st->refuse_at != 0 && st->calls == st->refuse_at)
    return false;
  TEST_ASSERT_TRUE_MESSAGE(len > 0, "empty chunk");
  TEST_ASSERT_TRUE_MESSAGE(len <= CHUNK_MAX, "chunk larger than the writer's buffer");
  TEST_ASSERT_TRUE_MESSAGE(st->len + len < OUT_BYTES, "test buffer too small");
  memcpy(st->out + st->len, data, len);
  st->len += len;
  st->out[st->len] = '\0';
  st->chunks++;
  if (len > st->max_chunk)
    st->max_chunk = len;
  return true;
}

static bool _write(uint32_t flags, const heaptop_thresholds_t *limits)
{
  return heaptop_json_snapshot(&s_snap, limits, flags, _sink, &s_sink);
}

static int _count(const char *text, const char *needle)
{
  int n = 0;
  for (const char *p = strstr(text, needle); p != NULL; p = strstr(p + strlen(needle), needle)) n++;
  return n;
}

/* Structural check: one object on one line, brackets balanced outside strings, nothing after the last brace. */
static void _assert_json_doc(const char *text, size_t len)
{
  TEST_ASSERT_TRUE(len > 2);
  TEST_ASSERT_EQUAL_INT(0, strncmp(text, "{\"ht\":2,", 8));
  TEST_ASSERT_EQUAL_CHAR('}', text[len - 1]);
  TEST_ASSERT_NULL_MESSAGE(memchr(text, '\n', len), "a document is one line without a newline");
  int depth = 0;
  bool in_str = false;
  for (size_t i = 0; i < len; i++)
  {
    const char c = text[i];
    if (in_str)
    {
      if (c == '\\')
        i++;
      else if (c == '"')
        in_str = false;
      continue;
    }
    if (c == '"')
    {
      in_str = true;
    }
    else if (c == '{' || c == '[')
    {
      depth++;
    }
    else if (c == '}' || c == ']')
    {
      depth--;
      TEST_ASSERT_TRUE_MESSAGE(depth >= 0, "unbalanced brackets");
      TEST_ASSERT_TRUE_MESSAGE(depth > 0 || i == len - 1, "document ends before its last byte");
    }
  }
  TEST_ASSERT_FALSE_MESSAGE(in_str, "unterminated string");
  TEST_ASSERT_EQUAL_INT(0, depth);
}

static void _fill(void)
{
  s_snap.seq = 42;
  s_snap.uptime_us = 3723000000ULL;
  s_snap.since_us = 3000000000ULL;
  s_snap.period_ms = 1000;
  s_snap.dt_ms = 1000;
  s_snap.self_us = 850;
  s_snap.num_cores = 2;
  s_snap.core_load_pct10[0] = 523;
  s_snap.core_load_pct10[1] = 140;
  s_snap.features = HEAPTOP_FEAT_RUNTIME_STATS | HEAPTOP_FEAT_TASK_HEAP | HEAPTOP_FEAT_FAIL_CB;
  s_snap.region[HEAPTOP_REGION_INTERNAL] = (heaptop_region_stats_t){.present = true,
                                                                    .total = 327680,
                                                                    .free = 204800,
                                                                    .min_free = 184320,
                                                                    .largest = 110592,
                                                                    .used_blocks = 412,
                                                                    .free_blocks = 23,
                                                                    .frag_pct10 = 460};
  s_snap.failures = 3;
  s_snap.alerts = HEAPTOP_ALERT_LEAK;

  s_snap.trend_len = 3;
  const uint32_t free_series[3] = {204800, 200000, 198000};
  const uint32_t largest_series[3] = {110592, 110000, 108000};
  memcpy(s_snap.trend[HEAPTOP_TREND_INTERNAL_FREE], free_series, sizeof(free_series));
  memcpy(s_snap.trend[HEAPTOP_TREND_INTERNAL_LARGEST], largest_series, sizeof(largest_series));

  s_snap.task_count = 2;
  strcpy(s_snap.tasks[0].name, "main");
  s_snap.tasks[0].state = HEAPTOP_TASK_BLOCKED;
  s_snap.tasks[0].prio = 1;
  s_snap.tasks[0].core = -1;
  s_snap.tasks[0].cpu_pct10 = 10;
  s_snap.tasks[0].stack_hwm = 3000;
  s_snap.tasks[0].heap_cur = 100;
  s_snap.tasks[0].heap_peak = 120;
  s_snap.tasks[0].heap_growth = -5;
  strcpy(s_snap.tasks[1].name, "worker");
  s_snap.tasks[1].state = HEAPTOP_TASK_READY;
  s_snap.tasks[1].prio = 5;
  s_snap.tasks[1].core = 0;
  s_snap.tasks[1].cpu_pct10 = 523;
  s_snap.tasks[1].stack_hwm = 900;
  s_snap.tasks[1].leak_suspect = true;
}

static void test_document_fields(void)
{
  _fill();
  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_ALL, &s_limits));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "{\"ht\":2,\"seq\":42,\"t_ms\":3723000,\"since_ms\":3000000,"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"period_ms\":1000,\"dt_ms\":1000,\"self_us\":850,\"features\":7,"));
  TEST_ASSERT_NOT_NULL(
    strstr(s_sink.out, "\"cpu10\":[523,140],\"regions\":{\"internal\":{\"total\":327680,\"free\":204800"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"frag10\":460"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, ",\"dma\":null,\"psram\":null},\"failures\":3,\"alerts\":[\"leak\"]"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out,
                              ",\"limits\":{\"dram_free_min\":20480,\"dram_largest_min\":8192,\"frag_pct_max\":80,"
                              "\"psram_free_min\":65536,\"stack_hwm_min\":256,\"task_growth\":4096}"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out,
                              ",\"trend\":{\"len\":3,\"internal_free\":[204800,200000,198000],"
                              "\"internal_largest\":[110592,110000,108000]}"));
  TEST_ASSERT_NULL_MESSAGE(strstr(s_sink.out, "psram_free\":["), "no PSRAM, so no PSRAM series");
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out,
                              "\"tasks\":[{\"name\":\"main\",\"state\":\"blocked\",\"prio\":1,\"core\":null,"
                              "\"cpu10\":10,\"hwm\":3000,\"heap\":100,\"peak\":120,\"psram\":0,\"growth\":-5,"
                              "\"leak\":false},"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out,
                              "{\"name\":\"worker\",\"state\":\"ready\",\"prio\":5,\"core\":0,\"cpu10\":523,"
                              "\"hwm\":900,"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"leak\":true}],\"tasks_truncated\":false}"));
}

static void test_missing_sources_are_null_and_task_heap_is_left_out(void)
{
  _fill();
  s_snap.features = 0;
  s_snap.alerts = 0;
  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_ALL, &s_limits));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"features\":0,\"cpu10\":null,"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"failures\":null,\"alerts\":[]"));
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, _count(s_sink.out, "\"cpu10\":null,\"hwm\":"), "one per task");
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"core\":null,\"cpu10\":null,\"hwm\":3000}"));
  TEST_ASSERT_NULL_MESSAGE(strstr(s_sink.out, "\"heap\""), "per-task heap needs task tracking");
  TEST_ASSERT_NULL(strstr(s_sink.out, "\"leak\""));
}

static void test_a_snapshot_with_no_sample_yet_is_still_a_document(void)
{
  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_ALL, &s_limits));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"seq\":0,"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"internal\":null,\"dma\":null,\"psram\":null"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"tasks\":[],\"tasks_truncated\":false}"));
}

static void test_flags_and_limits_are_optional(void)
{
  _fill();
  TEST_ASSERT_TRUE(_write(0, NULL));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NULL(strstr(s_sink.out, "\"limits\""));
  TEST_ASSERT_NULL(strstr(s_sink.out, "\"trend\""));
  TEST_ASSERT_NULL(strstr(s_sink.out, "\"tasks\""));
  TEST_ASSERT_NULL(strstr(s_sink.out, "\"tasks_truncated\""));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"alerts\":[\"leak\"]}"));

  memset(&s_sink, 0, sizeof(s_sink));
  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_TASKS, NULL));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"tasks\""));
  TEST_ASSERT_NULL(strstr(s_sink.out, "\"trend\""));

  memset(&s_sink, 0, sizeof(s_sink));
  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_TRENDS, NULL));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"trend\""));
  TEST_ASSERT_NULL(strstr(s_sink.out, "\"tasks\""));
}

static void test_psram_adds_its_region_and_series(void)
{
  _fill();
  s_snap.region[HEAPTOP_REGION_PSRAM] = (heaptop_region_stats_t){.present = true, .total = 8000000, .free = 7900000};
  const uint32_t psram_series[3] = {7900000, 7890000, 7880000};
  memcpy(s_snap.trend[HEAPTOP_TREND_PSRAM_FREE], psram_series, sizeof(psram_series));
  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_TRENDS, NULL));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"psram\":{\"total\":8000000,\"free\":7900000,"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, ",\"psram_free\":[7900000,7890000,7880000]}"));
}

static void test_task_names_are_escaped(void)
{
  _fill();
  strcpy(s_snap.tasks[0].name, "we\"ird\\x\t");
  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_TASKS, NULL));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"name\":\"we\\\"ird\\\\x\\u0009\""));
}

static void test_a_long_trend_length_is_clamped(void)
{
  _fill();
  s_snap.trend_len = 1000;
  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_TRENDS, NULL));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"trend\":{\"len\":40,"));
}

static void test_the_region_object_matches_the_stream_line(void)
{
  _fill();
  char mem[2048];
  heaptop_buf_t b;
  heaptop_buf_init(&b, mem, sizeof(mem));
  heaptop_stream_sample(&b, &s_snap);
  TEST_ASSERT_TRUE(_write(0, NULL));

  /* Same names and numbers in the stream line and in the document: one parser serves both. */
  const char *region =
    "\"internal\":{\"total\":327680,\"free\":204800,\"min\":184320,\"largest\":110592,\"frag10\":460,"
    "\"used_blocks\":412,\"free_blocks\":23}";
  TEST_ASSERT_NOT_NULL(strstr(mem, region));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, region));
  TEST_ASSERT_NOT_NULL(strstr(mem, "\"alerts\":[\"leak\"]"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"alerts\":[\"leak\"]"));
}

/* Every number at its widest, every region, all tasks, and names made only of escaped characters: the largest
 * document, and a check that no piece outgrows the reserve it was given. */
static void test_the_widest_document_fits_the_reserves(void)
{
  s_snap.seq = UINT32_MAX;
  s_snap.uptime_us = UINT64_MAX;
  s_snap.since_us = UINT64_MAX;
  s_snap.period_ms = UINT32_MAX;
  s_snap.dt_ms = UINT32_MAX;
  s_snap.self_us = UINT32_MAX;
  s_snap.features = UINT32_MAX;
  s_snap.alerts = UINT32_MAX;
  s_snap.num_cores = 2;
  s_snap.core_load_pct10[0] = UINT16_MAX;
  s_snap.core_load_pct10[1] = UINT16_MAX;
  for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
  {
    s_snap.region[r] = (heaptop_region_stats_t){.present = true,
                                                .total = REGION_MAX,
                                                .free = REGION_MAX,
                                                .min_free = REGION_MAX,
                                                .largest = REGION_MAX,
                                                .used_blocks = REGION_MAX,
                                                .free_blocks = REGION_MAX,
                                                .frag_pct10 = UINT16_MAX};
  }
  s_snap.failures = UINT32_MAX;
  s_snap.trend_len = HEAPTOP_TREND_LEN;
  for (int k = 0; k < HEAPTOP_TREND_COUNT; k++)
  {
    for (int i = 0; i < HEAPTOP_TREND_LEN; i++) s_snap.trend[k][i] = UINT32_MAX;
  }
  s_snap.task_count = HEAPTOP_MAX_TASKS;
  s_snap.tasks_truncated = true;
  for (int i = 0; i < HEAPTOP_MAX_TASKS; i++)
  {
    heaptop_task_stats_t *t = &s_snap.tasks[i];
    memset(t->name, 0x01, HEAPTOP_TASK_NAME_LEN - 1); /* each one escapes to six bytes */
    t->name[HEAPTOP_TASK_NAME_LEN - 1] = '\0';
    t->state = HEAPTOP_TASK_SUSPENDED;
    t->prio = UINT8_MAX;
    t->core = INT8_MIN;
    t->cpu_pct10 = UINT16_MAX;
    t->stack_hwm = UINT32_MAX;
    t->heap_cur = UINT32_MAX;
    t->heap_peak = UINT32_MAX;
    t->heap_psram = UINT32_MAX;
    t->heap_growth = INT32_MIN;
    t->leak_suspect = false;
  }
  s_limits = (heaptop_thresholds_t){UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};

  TEST_ASSERT_TRUE(_write(HEAPTOP_JSON_ALL, &s_limits));
  _assert_json_doc(s_sink.out, s_sink.len);
  TEST_ASSERT_TRUE_MESSAGE(s_sink.chunks > 1, "a document this size comes in several chunks");
  TEST_ASSERT_EQUAL_INT(HEAPTOP_MAX_TASKS, _count(s_sink.out, "{\"name\":"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, ",\"psram_free\":[4294967295,"));
  TEST_ASSERT_NOT_NULL(strstr(s_sink.out, "\"tasks_truncated\":true}"));
  TEST_ASSERT_TRUE(s_sink.max_chunk <= CHUNK_MAX);
}

static void test_a_sink_that_stops_ends_the_document(void)
{
  _fill();
  for (int i = 0; i < HEAPTOP_MAX_TASKS; i++)
  {
    strcpy(s_snap.tasks[i].name, "a_task_name");
    s_snap.tasks[i].state = HEAPTOP_TASK_BLOCKED;
  }
  s_snap.task_count = HEAPTOP_MAX_TASKS;
  s_sink.refuse_at = 2;
  TEST_ASSERT_FALSE(_write(HEAPTOP_JSON_ALL, &s_limits));
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_sink.calls, "no call after the sink said stop");
  TEST_ASSERT_EQUAL_INT(1, s_sink.chunks);
}

static void test_null_arguments_fail_without_calling_the_sink(void)
{
  _fill();
  TEST_ASSERT_FALSE(heaptop_json_snapshot(NULL, NULL, HEAPTOP_JSON_ALL, _sink, &s_sink));
  TEST_ASSERT_FALSE(heaptop_json_snapshot(&s_snap, NULL, HEAPTOP_JSON_ALL, NULL, &s_sink));
  TEST_ASSERT_EQUAL_INT(0, s_sink.calls);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_document_fields);
  RUN_TEST(test_missing_sources_are_null_and_task_heap_is_left_out);
  RUN_TEST(test_a_snapshot_with_no_sample_yet_is_still_a_document);
  RUN_TEST(test_flags_and_limits_are_optional);
  RUN_TEST(test_psram_adds_its_region_and_series);
  RUN_TEST(test_task_names_are_escaped);
  RUN_TEST(test_a_long_trend_length_is_clamped);
  RUN_TEST(test_the_region_object_matches_the_stream_line);
  RUN_TEST(test_the_widest_document_fits_the_reserves);
  RUN_TEST(test_a_sink_that_stops_ends_the_document);
  RUN_TEST(test_null_arguments_fail_without_calling_the_sink);
  return UNITY_END();
}
