/**
 * @file heaptop_console.c
 * @brief The `ht` console command and its subcommands.
 *
 * Subcommands run in the console task, one at a time, so they share one text
 * buffer and one snapshot copy allocated at registration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "heaptop.h"
#include "heaptop_priv.h"
#include "heaptop_render.h"
#include "linenoise/linenoise.h"

#define HEAPTOP_OUT_SIZE       6144
#define HEAPTOP_REFRESH_MIN_MS 100
#define HEAPTOP_REFRESH_MAX_MS 10000
#define HEAPTOP_KEY_CTRL_C     0x03

/* ANSI: alternate screen + hidden cursor while `ht top` owns the terminal. */
#define HEAPTOP_ANSI_ENTER "\x1b[?1049h\x1b[?25l"
#define HEAPTOP_ANSI_LEAVE "\x1b[?25h\x1b[?1049l"
#define HEAPTOP_ANSI_HOME  "\x1b[H"
#define HEAPTOP_ANSI_EOL   "\x1b[K"
#define HEAPTOP_ANSI_EOS   "\x1b[J"

static const char *TAG = "HEAPTOP_CON";

typedef int (*heaptop_sub_fn_t)(int argc, char **argv);

typedef struct heaptop_sub
{
  const char *name;
  const char *args;
  const char *help;
  heaptop_sub_fn_t fn;
} heaptop_sub_t;

#if CONFIG_HEAPTOP_FAILED_ALLOC_CALLBACK
#define HEAPTOP_CON_FAILS CONFIG_HEAPTOP_FAIL_RING_LEN
#else
#define HEAPTOP_CON_FAILS 1
#endif

typedef struct heaptop_console_priv
{
  /* --- Allocated once at registration; used only from the console task. --- */
  char *out;
  heaptop_snapshot_t *snap;

  /* --- Console-task scratch. --- */
  heaptop_frag_hist_t hist;
  heaptop_fail_t fails[HEAPTOP_CON_FAILS];
} heaptop_console_priv_t;

static heaptop_console_priv_t s_con;

static void _flush(const heaptop_buf_t *b)
{
  fwrite(b->p, 1, b->len, stdout);
  if (b->truncated)
    fputs("... (output truncated)\n", stdout);
  fflush(stdout);
}

/* Latest snapshot into s_con.snap; false (with a message) when there is none yet. */
static bool _load_snapshot(void)
{
  esp_err_t err = heaptop_get_snapshot(s_con.snap);
  if (err != ESP_OK)
  {
    printf("heaptop is not running (%s): call heaptop_init() first\n", esp_err_to_name(err));
    return false;
  }
  if (s_con.snap->seq == 0)
  {
    printf("heaptop: no sample yet, try again in a moment\n");
    return false;
  }
  return true;
}

static int _cmd_heap(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (!_load_snapshot())
    return 1;
  heaptop_buf_t b;
  heaptop_buf_init(&b, s_con.out, HEAPTOP_OUT_SIZE);
  heaptop_render_heap(&b, s_con.snap);
  _flush(&b);
  return 0;
}

static bool _parse_sort(const char *arg, heaptop_sort_t *key)
{
  static const char *const names[] = {"cpu", "heap", "stack", "name"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
  {
    if (strcmp(arg, names[i]) == 0)
    {
      *key = (heaptop_sort_t)i;
      return true;
    }
  }
  return false;
}

static int _cmd_tasks(int argc, char **argv)
{
  heaptop_sort_t key = HEAPTOP_SORT_CPU;
  if (argc > 1 && !_parse_sort(argv[1], &key))
  {
    printf("ht tasks: unknown sort key '%s' (use cpu, heap, stack or name)\n", argv[1]);
    return 1;
  }
  if (!_load_snapshot())
    return 1;
  heaptop_buf_t b;
  heaptop_buf_init(&b, s_con.out, HEAPTOP_OUT_SIZE);
  heaptop_render_tasks(&b, s_con.snap, key);
  _flush(&b);
  return 0;
}

static int _cmd_frag(int argc, char **argv)
{
  static const char *const names[HEAPTOP_REGION_COUNT] = {"internal", "dma", "psram"};
  heaptop_region_t region = HEAPTOP_REGION_INTERNAL;
  if (argc > 1)
  {
    region = HEAPTOP_REGION_COUNT;
    for (int r = 0; r < HEAPTOP_REGION_COUNT; r++)
    {
      if (strcmp(argv[1], names[r]) == 0)
        region = (heaptop_region_t)r;
    }
    if (region == HEAPTOP_REGION_COUNT)
    {
      printf("ht frag: unknown region '%s' (use internal, dma or psram)\n", argv[1]);
      return 1;
    }
  }
  /* Walked here, in the console task: the sampler never pays for it. */
  heaptop_heap_histogram(region, &s_con.hist);
  heaptop_buf_t b;
  heaptop_buf_init(&b, s_con.out, HEAPTOP_OUT_SIZE);
  heaptop_render_frag(&b, names[region], &s_con.hist);
  _flush(&b);
  return 0;
}

static int _cmd_allocs(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (!_load_snapshot())
    return 1;
  const uint16_t n = heaptop_hooks_failures(s_con.fails, HEAPTOP_CON_FAILS);
  heaptop_buf_t b;
  heaptop_buf_init(&b, s_con.out, HEAPTOP_OUT_SIZE);
  heaptop_render_allocs(&b, s_con.snap, s_con.fails, n);
  _flush(&b);
  return 0;
}

#define HEAPTOP_LEAK_ROWS_DEFAULT 10

static int _leaks_err(const char *what, esp_err_t err)
{
  if (err == ESP_ERR_NOT_SUPPORTED)
    printf("ht leaks: heap tracing is off: enable CONFIG_HEAP_TRACING_STANDALONE\n");
  else if (err == ESP_ERR_INVALID_STATE)
    printf("ht leaks %s: %s\n", what, strcmp(what, "start") == 0 ? "already running" : "not running");
  else
    printf("ht leaks %s: %s\n", what, esp_err_to_name(err));
  return 1;
}

static int _cmd_leaks(int argc, char **argv)
{
  const char *action = argc > 1 ? argv[1] : "report";
  if (strcmp(action, "start") == 0)
  {
    esp_err_t err = heaptop_leaks_start();
    if (err != ESP_OK)
      return _leaks_err("start", err);
    printf("leak capture running: exercise the code, then `ht leaks stop` and `ht leaks report`\n");
    return 0;
  }
  if (strcmp(action, "stop") == 0)
  {
    esp_err_t err = heaptop_leaks_stop();
    if (err != ESP_OK)
      return _leaks_err("stop", err);
    return heaptop_leaks_report(stdout, HEAPTOP_LEAK_ROWS_DEFAULT) == ESP_OK ? 0 : 1;
  }
  if (strcmp(action, "status") == 0)
    return heaptop_leaks_report(stdout, 0) == ESP_OK ? 0 : 1;
  if (strcmp(action, "report") == 0)
  {
    unsigned long rows = HEAPTOP_LEAK_ROWS_DEFAULT;
    if (argc > 2)
    {
      char *end = NULL;
      rows = strtoul(argv[2], &end, 10);
      if (end == argv[2] || *end != '\0' || rows == 0)
      {
        printf("ht leaks report: rows must be a positive number\n");
        return 1;
      }
    }
    return heaptop_leaks_report(stdout, rows) == ESP_OK ? 0 : 1;
  }
  printf("ht leaks: unknown action '%s' (use start, stop, report [rows] or status)\n", action);
  return 1;
}

static int _cmd_mark(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (!_load_snapshot())
    return 1;
  if (heaptop_mark() != ESP_OK)
    return 1;
  printf("mark set at sample #%lu; run `ht diff` later to see what changed\n", (unsigned long)s_con.snap->seq);
  return 0;
}

static int _cmd_diff(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  esp_err_t err = heaptop_diff(stdout);
  if (err != ESP_OK)
  {
    printf("ht diff: %s\n", esp_err_to_name(err));
    return 1;
  }
  return 0;
}

/* Wait up to @p ms for one byte on stdin; -1 on timeout. The wait doubles as the refresh tick. */
static int _wait_key(uint32_t ms)
{
  const int fd = fileno(stdin);
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);
  struct timeval tv = {.tv_sec = ms / 1000u, .tv_usec = (ms % 1000u) * 1000u};
  if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
    return -1;
  unsigned char c;
  return read(fd, &c, 1) == 1 ? c : -1;
}

/* Frame in place: home, each line cleared to its end, rest of the screen cleared. */
static void _draw_frame(const heaptop_buf_t *b, bool ansi)
{
  if (!ansi)
  {
    _flush(b);
    fputs("----\n", stdout);
    fflush(stdout);
    return;
  }
  fputs(HEAPTOP_ANSI_HOME, stdout);
  const char *line = b->p;
  const char *end = b->p + b->len;
  while (line < end)
  {
    const char *nl = memchr(line, '\n', (size_t)(end - line));
    const size_t n = nl ? (size_t)(nl - line) : (size_t)(end - line);
    fwrite(line, 1, n, stdout);
    fputs(HEAPTOP_ANSI_EOL "\n", stdout);
    line += n + 1;
  }
  fputs(HEAPTOP_ANSI_EOS, stdout);
  fflush(stdout);
}

static int _cmd_top(int argc, char **argv)
{
  heaptop_top_view_t view = {.sort = HEAPTOP_SORT_CPU, .paused = false, .refresh_ms = 1000};
  if (argc > 1)
  {
    char *end = NULL;
    unsigned long ms = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || ms < HEAPTOP_REFRESH_MIN_MS || ms > HEAPTOP_REFRESH_MAX_MS)
    {
      printf("ht top: refresh must be %d..%d ms\n", HEAPTOP_REFRESH_MIN_MS, HEAPTOP_REFRESH_MAX_MS);
      return 1;
    }
    view.refresh_ms = (uint32_t)ms;
  }
  if (!_load_snapshot())
    return 1;

  /* ESP_LOG lines from other tasks still land on screen; the next frame overwrites them. */
  const bool ansi = !linenoiseIsDumbMode();
  if (ansi)
    fputs(HEAPTOP_ANSI_ENTER, stdout);

  uint32_t shown_seq = 0;
  bool redraw = true;
  for (;;)
  {
    if (!view.paused && heaptop_get_snapshot(s_con.snap) == ESP_OK && s_con.snap->seq != shown_seq)
      redraw = true;
    if (redraw)
    {
      heaptop_buf_t b;
      heaptop_buf_init(&b, s_con.out, HEAPTOP_OUT_SIZE);
      heaptop_render_top(&b, s_con.snap, &view);
      _draw_frame(&b, ansi);
      shown_seq = s_con.snap->seq;
      redraw = false;
    }

    const int key = _wait_key(view.refresh_ms);
    if (key < 0)
      continue;
    redraw = true;
    if (key == 'q' || key == 'Q' || key == HEAPTOP_KEY_CTRL_C)
      break;
    switch (key)
    {
      case 'c':
        view.sort = HEAPTOP_SORT_CPU;
        break;
      case 'm':
        view.sort = HEAPTOP_SORT_HEAP;
        break;
      case 's':
        view.sort = HEAPTOP_SORT_STACK;
        break;
      case 'n':
        view.sort = HEAPTOP_SORT_NAME;
        break;
      case 'p':
        view.paused = !view.paused;
        break;
      case '+':
        view.refresh_ms = view.refresh_ms * 2 > HEAPTOP_REFRESH_MAX_MS ? HEAPTOP_REFRESH_MAX_MS : view.refresh_ms * 2;
        break;
      case '-':
        view.refresh_ms = view.refresh_ms / 2 < HEAPTOP_REFRESH_MIN_MS ? HEAPTOP_REFRESH_MIN_MS : view.refresh_ms / 2;
        break;
      default:
        redraw = false; /* arrow keys and other escape bytes */
        break;
    }
  }

  if (ansi)
    fputs(HEAPTOP_ANSI_LEAVE, stdout);
  fflush(stdout);
  return 0;
}

static const heaptop_sub_t s_subs[] = {
  {"top", "[refresh_ms]", "Live view; q quits, c/m/s/n sort, +/- refresh, p pause", _cmd_top},
  {"frag", "[internal|dma|psram]", "Free-block size histogram of a region", _cmd_frag},
  {"allocs", "", "Allocation rates and the last allocation failures", _cmd_allocs},
  {"leaks", "start|stop|report [rows]|status", "Capture allocations never freed, grouped by call stack", _cmd_leaks},
  {"mark", "", "Remember the current state as a baseline", _cmd_mark},
  {"diff", "", "What changed since `ht mark`: region free bytes and task heap", _cmd_diff},
  {"heap", "", "Heap regions: free, min free, largest block, fragmentation", _cmd_heap},
  {"tasks", "[cpu|heap|stack|name]", "Tasks: state, CPU %, stack high-water mark, heap held", _cmd_tasks},
};

static void _usage(void)
{
  printf("usage: ht <subcommand> [args]\n");
  for (size_t i = 0; i < sizeof(s_subs) / sizeof(s_subs[0]); i++)
    printf("  ht %-6s %-32s %s\n", s_subs[i].name, s_subs[i].args, s_subs[i].help);
}

static int _cmd_ht(int argc, char **argv)
{
  if (argc < 2 || strcmp(argv[1], "help") == 0)
  {
    _usage();
    return 0;
  }
  for (size_t i = 0; i < sizeof(s_subs) / sizeof(s_subs[0]); i++)
  {
    if (strcmp(argv[1], s_subs[i].name) == 0)
      return s_subs[i].fn(argc - 1, argv + 1);
  }
  printf("ht: unknown subcommand '%s'\n", argv[1]);
  _usage();
  return 1;
}

esp_err_t heaptop_console_register(void)
{
  if (s_con.out == NULL)
  {
    const uint32_t caps = heaptop_buffer_caps();
    s_con.out = heap_caps_malloc(HEAPTOP_OUT_SIZE, caps);
    s_con.snap = heap_caps_malloc(sizeof(heaptop_snapshot_t), caps);
    if (s_con.out == NULL || s_con.snap == NULL)
    {
      heap_caps_free(s_con.out);
      heap_caps_free(s_con.snap);
      s_con.out = NULL;
      s_con.snap = NULL;
      ESP_LOGE(TAG, "no memory for console buffers");
      return ESP_ERR_NO_MEM;
    }
  }

  const esp_console_cmd_t cmd = {
    .command = "ht",
    .help = "heaptop: heap, fragmentation, leak and task monitor. 'ht help' lists subcommands",
    .hint = "<subcommand> [args]",
    .func = &_cmd_ht,
  };
  return esp_console_cmd_register(&cmd);
}
