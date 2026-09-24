/**
 * @file heaptop_console.c
 * @brief The `ht` console command and its subcommands.
 *
 * Subcommands run in the console task, one at a time, so they share one text
 * buffer and one snapshot copy allocated at registration.
 */

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "heaptop.h"
#include "heaptop_priv.h"
#include "heaptop_render.h"

#define HEAPTOP_OUT_SIZE 6144

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

static const heaptop_sub_t s_subs[] = {
  {"heap", "", "Heap regions: free, min free, largest block, fragmentation", _cmd_heap},
  {"tasks", "[cpu|heap|stack|name]", "Tasks: state, CPU %, stack high-water mark, heap held", _cmd_tasks},
};

static void _usage(void)
{
  printf("usage: ht <subcommand> [args]\n");
  for (size_t i = 0; i < sizeof(s_subs) / sizeof(s_subs[0]); i++)
    printf("  ht %-6s %-22s %s\n", s_subs[i].name, s_subs[i].args, s_subs[i].help);
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
