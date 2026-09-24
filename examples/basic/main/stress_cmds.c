/**
 * @file stress_cmds.c
 * @brief Misbehaviour on demand for the heaptop demo.
 *
 * Each workload runs in its own named task so heaptop attributes CPU, stack
 * and heap to it. `stress stop` ends every task and frees what they kept.
 */

#include "stress_cmds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STRESS_MAX_BLOCKS 512
#define STRESS_TASK_STACK 4096
#define STRESS_TASK_PRIO  1
/* Leaves 640 bytes for the task's own frames (they take about 450 on an ESP32-S3):
 * safe, yet the maximum trips the default 256-byte stack alert. */
#define STRESS_STACK_TASK_MAX (STRESS_TASK_STACK - 640)
#define STRESS_CPU_WINDOW_MS  100
#define STRESS_CPU_MAX_PCT    90 /* keep the idle task (and its watchdog) alive */
#define STRESS_FRAG_SMALL     32
#define STRESS_FRAG_LARGE     512
#define STRESS_BURST_SIZE     128
#define STRESS_EXIT_POLL_MS   20
#define STRESS_EXIT_TRIES     100

typedef struct stress_priv
{
  /* --- Worker handles: set by `stress <x>`, cleared by `stress stop`; console task only. --- */
  TaskHandle_t leak_task;
  TaskHandle_t cpu_task;
  TaskHandle_t stack_task;

  /* --- Worker parameters: written before the worker starts. --- */
  uint32_t leak_bytes;
  uint32_t leak_ms;
  uint32_t cpu_pct;
  uint32_t stack_bytes;

  /* --- Blocks kept alive on purpose; freed by `stress stop` once workers are gone. --- */
  void *leaked[STRESS_MAX_BLOCKS];
  volatile uint16_t leaked_n; /* written by the leak worker only */
  void *frag[STRESS_MAX_BLOCKS];
  uint16_t frag_n;
  void *holes[STRESS_MAX_BLOCKS]; /* console-task scratch for `stress frag` */

  volatile bool stop;
} stress_priv_t;

static stress_priv_t s_stress;

/* Workers never delete themselves: they suspend, and `stress stop` deletes them
 * (see _reap). */
static void _leak_task(void *arg)
{
  (void)arg;
  while (!s_stress.stop && s_stress.leaked_n < STRESS_MAX_BLOCKS)
  {
    void *p = malloc(s_stress.leak_bytes);
    if (p == NULL)
      break;
    memset(p, 0xA5, s_stress.leak_bytes);
    s_stress.leaked[s_stress.leaked_n] = p;
    s_stress.leaked_n++;
    vTaskDelay(pdMS_TO_TICKS(s_stress.leak_ms));
  }
  /* Stay alive so the leaked heap belongs to a live task until `stress stop`. */
  vTaskSuspend(NULL);
}

static void _cpu_task(void *arg)
{
  (void)arg;
  while (!s_stress.stop)
  {
    const int64_t busy_until = esp_timer_get_time() + (int64_t)s_stress.cpu_pct * STRESS_CPU_WINDOW_MS * 10;
    while (esp_timer_get_time() < busy_until)
    {
    }
    vTaskDelay(pdMS_TO_TICKS(STRESS_CPU_WINDOW_MS * (100 - s_stress.cpu_pct) / 100));
  }
  vTaskSuspend(NULL);
}

/* Touches exactly @p bytes of stack in one frame, so the high-water mark drops
 * by that much plus a small, fixed frame (recursion would add per-level frames). */
static uint32_t _burn_stack(uint32_t bytes)
{
  volatile uint8_t *buf = __builtin_alloca(bytes);
  memset((void *)buf, 0x5A, bytes);
  return buf[0] + buf[bytes - 1];
}

static void _stack_task(void *arg)
{
  (void)arg;
  (void)_burn_stack(s_stress.stack_bytes);
  vTaskSuspend(NULL);
}

static bool _parse_u32(const char *s, uint32_t *out)
{
  char *end = NULL;
  unsigned long v = strtoul(s, &end, 10);
  if (end == s || *end != '\0')
    return false;
  *out = (uint32_t)v;
  return true;
}

static bool _start(TaskFunction_t fn, const char *name, TaskHandle_t *handle)
{
  if (*handle != NULL)
  {
    printf("%s is already running; `stress stop` first\n", name);
    return false;
  }
  TaskHandle_t h = NULL;
  if (xTaskCreate(fn, name, STRESS_TASK_STACK, NULL, STRESS_TASK_PRIO, &h) != pdPASS)
  {
    printf("could not create %s\n", name);
    return false;
  }
  *handle = h;
  return true;
}

/* Deletes a worker once it has suspended itself. A task that deletes itself is
 * freed later by the idle task, and with heap task tracking that free waits on
 * a mutex: while another task holds it (the frees in _stop, say), the idle task
 * blocks, its core has nothing left to run, and FreeRTOS asserts in
 * prvSelectHighestPriorityTaskSMP. A suspended task is freed right here instead. */
static bool _reap(TaskHandle_t *handle)
{
  if (*handle == NULL)
    return true;
  if (eTaskGetState(*handle) != eSuspended)
    return false;
  vTaskDelete(*handle);
  *handle = NULL;
  return true;
}

static int _stop(void)
{
  s_stress.stop = true;
  for (int i = 0; i < STRESS_EXIT_TRIES; i++)
  {
    const bool leak = _reap(&s_stress.leak_task);
    const bool cpu = _reap(&s_stress.cpu_task);
    const bool stack = _reap(&s_stress.stack_task);
    if (leak && cpu && stack)
      break;
    vTaskDelay(pdMS_TO_TICKS(STRESS_EXIT_POLL_MS));
  }
  if (s_stress.leak_task || s_stress.cpu_task || s_stress.stack_task)
  {
    printf("stress workers did not exit; blocks kept\n");
    return 1;
  }
  const uint16_t leaked = s_stress.leaked_n;
  for (uint16_t i = 0; i < leaked; i++) free(s_stress.leaked[i]);
  for (uint16_t i = 0; i < s_stress.frag_n; i++) heap_caps_free(s_stress.frag[i]);
  printf("stopped; freed %u leaked and %u fragmentation blocks\n", leaked, s_stress.frag_n);
  s_stress.leaked_n = 0;
  s_stress.frag_n = 0;
  s_stress.stop = false;
  return 0;
}

static int _frag(uint32_t n)
{
  if (n == 0 || n > STRESS_MAX_BLOCKS - s_stress.frag_n)
  {
    printf("stress frag: n must be 1..%u\n", STRESS_MAX_BLOCKS - s_stress.frag_n);
    return 1;
  }
  /* Lay down large/small pairs first, then free every large block: holes the
   * size of the large blocks, pinned apart by the small ones. Freeing inside
   * the loop would let the next large request reuse the same hole. */
  uint32_t holes = 0;
  for (uint32_t i = 0; i < n; i++)
  {
    s_stress.holes[i] = heap_caps_malloc(STRESS_FRAG_LARGE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *small = heap_caps_malloc(STRESS_FRAG_SMALL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (small)
      s_stress.frag[s_stress.frag_n++] = small;
    if (!s_stress.holes[i] || !small)
    {
      n = i + 1;
      break;
    }
  }
  for (uint32_t i = 0; i < n; i++)
  {
    if (s_stress.holes[i])
    {
      heap_caps_free(s_stress.holes[i]);
      holes++;
    }
  }
  printf("left %lu holes of %d bytes between %d-byte blocks (internal RAM)\n",
         (unsigned long)holes,
         STRESS_FRAG_LARGE,
         STRESS_FRAG_SMALL);
  return 0;
}

static int _burst(uint32_t n)
{
  const int64_t t0 = esp_timer_get_time();
  for (uint32_t i = 0; i < n; i++)
  {
    void *p = malloc(STRESS_BURST_SIZE);
    free(p);
  }
  printf("%lu malloc/free pairs in %lld us\n", (unsigned long)n, (long long)(esp_timer_get_time() - t0));
  return 0;
}

static int _fail(uint32_t bytes)
{
  void *p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (p != NULL)
  {
    heap_caps_free(p);
    printf("%lu bytes of internal RAM were available; ask for more to fail\n", (unsigned long)bytes);
    return 1;
  }
  printf("allocation of %lu bytes failed as intended; see `ht allocs`\n", (unsigned long)bytes);
  return 0;
}

static void _usage(void)
{
  printf("usage: stress <workload>\n"
         "  stress leak <bytes> <ms>  task stress_leak mallocs <bytes> every <ms> and never frees\n"
         "  stress frag <n>           leave n holes in internal RAM\n"
         "  stress burst <n>          n malloc/free pairs right now\n"
         "  stress stack <bytes>      task stress_stack uses <bytes> of its 4 KB stack\n"
         "  stress fail <bytes>       an internal RAM allocation that cannot succeed\n"
         "  stress cpu <pct>          task stress_cpu busy <pct>%% of the time (max %d)\n"
         "  stress stop               end every workload and free what it kept\n",
         STRESS_CPU_MAX_PCT);
}

static int _cmd_stress(int argc, char **argv)
{
  if (argc < 2)
  {
    _usage();
    return 0;
  }
  const char *w = argv[1];
  uint32_t a = 0, b = 0;
  if (strcmp(w, "stop") == 0)
    return _stop();
  if (argc < 3 || !_parse_u32(argv[2], &a))
  {
    _usage();
    return 1;
  }
  if (strcmp(w, "leak") == 0)
  {
    if (argc < 4 || !_parse_u32(argv[3], &b) || a == 0 || b == 0)
    {
      printf("stress leak: <bytes> and <ms> must be positive\n");
      return 1;
    }
    s_stress.leak_bytes = a;
    s_stress.leak_ms = b;
    return _start(_leak_task, "stress_leak", &s_stress.leak_task) ? 0 : 1;
  }
  if (strcmp(w, "frag") == 0)
    return _frag(a);
  if (strcmp(w, "burst") == 0)
    return _burst(a);
  if (strcmp(w, "fail") == 0)
    return _fail(a);
  if (strcmp(w, "stack") == 0)
  {
    if (a == 0 || a > STRESS_STACK_TASK_MAX)
    {
      printf("stress stack: bytes must be 1..%d\n", STRESS_STACK_TASK_MAX);
      return 1;
    }
    s_stress.stack_bytes = a;
    return _start(_stack_task, "stress_stack", &s_stress.stack_task) ? 0 : 1;
  }
  if (strcmp(w, "cpu") == 0)
  {
    if (a == 0 || a > STRESS_CPU_MAX_PCT)
    {
      printf("stress cpu: pct must be 1..%d\n", STRESS_CPU_MAX_PCT);
      return 1;
    }
    s_stress.cpu_pct = a;
    return _start(_cpu_task, "stress_cpu", &s_stress.cpu_task) ? 0 : 1;
  }
  _usage();
  return 1;
}

void register_stress_commands(void)
{
  const esp_console_cmd_t cmd = {
    .command = "stress",
    .help = "Create leaks, fragmentation, CPU load and allocation failures for heaptop to show",
    .hint = "<leak|frag|burst|stack|fail|cpu|stop> [args]",
    .func = &_cmd_stress,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
