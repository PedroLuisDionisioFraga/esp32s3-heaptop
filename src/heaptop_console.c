/**
 * @file heaptop_console.c
 * @brief The `ht` console command and its subcommands.
 *
 * Subcommands run in the console task, one at a time, so they share one text
 * buffer and one snapshot copy allocated at registration.
 */

#include <errno.h>
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
#define HEAPTOP_KEY_NONE       (-1)
#define HEAPTOP_KEY_BROKEN     (-2)
#define HEAPTOP_STREAM_POLL_MS 200

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

typedef struct heaptop_console_priv
{
  /* --- Allocated once at registration; used only from the console task. --- */
  char *out;
  heaptop_snapshot_t *snap;

  /* --- Console-task scratch. --- */
  heaptop_fail_t fails[HEAPTOP_FAIL_LEN];
  heaptop_emit_state_t emit;
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

static bool _parse_u32(const char *s, uint32_t *out)
{
  char *end = NULL;
  const unsigned long v = strtoul(s, &end, 10);
  if (end == s || *end != '\0')
    return false;
  *out = (uint32_t)v;
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

/* Wait up to @p ms for one byte on stdin. The wait doubles as the refresh tick.
 * Returns the byte, HEAPTOP_KEY_NONE on timeout, or HEAPTOP_KEY_BROKEN when stdin
 * cannot be waited on (for example a UART console without its driver installed):
 * callers must stop then, or they would spin without ever seeing `q`. */
static int _wait_key(uint32_t ms, int *err)
{
  const int fd = fileno(stdin);
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);
  struct timeval tv = {.tv_sec = ms / 1000u, .tv_usec = (ms % 1000u) * 1000u};
  const int r = select(fd + 1, &rfds, NULL, NULL, &tv);
  if (r < 0)
  {
    *err = errno;
    return HEAPTOP_KEY_BROKEN;
  }
  if (r == 0)
    return HEAPTOP_KEY_NONE;
  unsigned char c;
  const ssize_t n = read(fd, &c, 1);
  if (n == 1)
    return c;
  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
  {
    *err = errno;
    return HEAPTOP_KEY_BROKEN;
  }
  return HEAPTOP_KEY_NONE;
}

static void _report_broken(const char *cmd, int err)
{
  printf("%s: cannot wait for keys on stdin (%s); is the console driver installed?\n", cmd, strerror(err));
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
    uint32_t ms = 0;
    if (!_parse_u32(argv[1], &ms) || ms < HEAPTOP_REFRESH_MIN_MS || ms > HEAPTOP_REFRESH_MAX_MS)
    {
      printf("ht top: refresh must be %d..%d ms\n", HEAPTOP_REFRESH_MIN_MS, HEAPTOP_REFRESH_MAX_MS);
      return 1;
    }
    view.refresh_ms = ms;
  }
  if (!_load_snapshot())
    return 1;

  /* Alerts show in the banner instead of the log. ESP_LOG lines from other tasks
   * still land on screen; the next frame overwrites them. */
  const bool ansi = !linenoiseIsDumbMode();
  heaptop_alerts_set_quiet(true);
  if (ansi)
    fputs(HEAPTOP_ANSI_ENTER, stdout);

  int err = 0;
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

    const int key = _wait_key(view.refresh_ms, &err);
    if (key == HEAPTOP_KEY_BROKEN)
      break;
    if (key == HEAPTOP_KEY_NONE)
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
  heaptop_alerts_set_quiet(false);
  if (err)
  {
    _report_broken("ht top", err);
    return 1;
  }
  return 0;
}

static bool _is_quit(int key)
{
  return key == 'q' || key == 'Q' || key == HEAPTOP_KEY_CTRL_C;
}

static int _cmd_stream(int argc, char **argv)
{
  uint32_t every_ms = 0; /* 0 = every sample */
  if (argc > 1)
  {
    if (!_parse_u32(argv[1], &every_ms) || every_ms < HEAPTOP_REFRESH_MIN_MS)
    {
      printf("ht stream: interval must be >= %d ms\n", HEAPTOP_REFRESH_MIN_MS);
      return 1;
    }
  }
  if (!_load_snapshot())
    return 1;

  /* Alerts arrive as JSON lines instead of log lines while streaming. */
  heaptop_alerts_set_quiet(true);
  memset(&s_con.emit, 0, sizeof(s_con.emit));
  int err = 0;
  uint32_t last_seq = 0;
  uint64_t last_emit_us = 0;
  for (;;)
  {
    if (heaptop_get_snapshot(s_con.snap) == ESP_OK && s_con.snap->seq != last_seq &&
        (last_emit_us == 0 || s_con.snap->uptime_us - last_emit_us >= (uint64_t)every_ms * 1000u))
    {
      heaptop_emit(stdout, s_con.snap, &s_con.emit, s_con.out, HEAPTOP_OUT_SIZE);
      last_seq = s_con.snap->seq;
      last_emit_us = s_con.snap->uptime_us;
    }
    const int key = _wait_key(HEAPTOP_STREAM_POLL_MS, &err);
    if (key == HEAPTOP_KEY_BROKEN || _is_quit(key))
      break;
  }
  heaptop_alerts_set_quiet(false);
  if (err)
  {
    _report_broken("ht stream", err);
    return 1;
  }
  return 0;
}

static int _cmd_health(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (!_load_snapshot())
    return 1;
  heaptop_thresholds_t th;
  heaptop_alerts_thresholds(&th);
  const uint16_t n = heaptop_fails_copy(s_con.fails, HEAPTOP_FAIL_LEN);
  heaptop_buf_t b;
  heaptop_buf_init(&b, s_con.out, HEAPTOP_OUT_SIZE);
  heaptop_render_health(&b, s_con.snap, &th, s_con.fails, n);
  _flush(&b);
  return 0;
}

static int _cmd_clear(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  esp_err_t err = heaptop_clear();
  if (err == ESP_ERR_INVALID_STATE)
  {
    printf("heaptop is not running: call heaptop_init() first\n");
    return 1;
  }
  if (err != ESP_OK)
  {
    printf("ht clear: %s\n", esp_err_to_name(err));
    return 1;
  }
  printf("stats cleared; stack high-water marks keep their since-boot minimum\n");
  return 0;
}

static int _stress_status(void)
{
  heaptop_stress_status_t st;
  heaptop_stress_status(&st);
  if (!st.running)
    printf("cpu stress: idle\n");
  else if (st.left_s)
    printf("cpu stress: %u%% on %u core%s, %lu s left\n",
           st.pct,
           st.workers,
           st.workers == 1 ? "" : "s",
           (unsigned long)st.left_s);
  else
    printf("cpu stress: %u%% on %u core%s until `ht stress stop`\n", st.pct, st.workers, st.workers == 1 ? "" : "s");
  return 0;
}

static int _cmd_stress(int argc, char **argv)
{
  if (argc < 2)
    return _stress_status();
  if (strcmp(argv[1], "stop") == 0)
  {
    heaptop_stress_stop();
    printf("cpu stress stopped\n");
    return 0;
  }
  uint32_t pct = 0, seconds = 0;
  if (strcmp(argv[1], "cpu") != 0 || argc < 3 || argc > 4 || !_parse_u32(argv[2], &pct) ||
      (argc == 4 && !_parse_u32(argv[3], &seconds)))
  {
    printf("usage: ht stress cpu <pct> [seconds] | ht stress stop | ht stress\n");
    return 1;
  }
  if (pct < 1 || pct > HEAPTOP_STRESS_MAX_PCT)
  {
    printf("ht stress cpu: pct must be 1..%d (the rest keeps the idle task alive)\n", HEAPTOP_STRESS_MAX_PCT);
    return 1;
  }
  esp_err_t err = heaptop_stress_cpu((uint8_t)pct, seconds);
  if (err != ESP_OK)
  {
    printf("ht stress cpu: %s\n", esp_err_to_name(err));
    return 1;
  }
  _stress_status();
  printf("watch it with `ht top`\n");
  return 0;
}

static const heaptop_sub_t s_subs[] = {
  {"top", "[refresh_ms]", "Live view; q quits, c/m/s/n sort, +/- refresh, p pause", _cmd_top},
  {"heap", "", "What is used and free: regions, min free, largest block, frag", _cmd_heap},
  {"tasks", "[cpu|heap|stack|name]", "Who uses what: CPU %, stack high-water mark, heap", _cmd_tasks},
  {"health", "", "Every check with its value and limit, and the last failed allocations", _cmd_health},
  {"clear", "", "Reset min free, peaks, failures and trends: a fresh window", _cmd_clear},
  {"stress", "cpu <pct> [s] | stop", "Load every core to pct% (1..90) for s seconds, 0 = until stop", _cmd_stress},
  {"stream", "[every_ms]", "JSON Lines for host tools, one sample per line group; q stops", _cmd_stream},
};

static void _usage(void)
{
  printf("usage: ht <subcommand> [args]\n");
  for (size_t i = 0; i < sizeof(s_subs) / sizeof(s_subs[0]); i++)
    printf("  ht %-6s %-24s %s\n", s_subs[i].name, s_subs[i].args, s_subs[i].help);
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
    .help = "heaptop: memory, health and CPU monitor. 'ht help' lists subcommands",
    .hint = "<subcommand> [args]",
    .func = &_cmd_ht,
  };
  return esp_console_cmd_register(&cmd);
}
