/**
 * @file heaptop_render.h
 * @brief Text rendering of snapshots into caller-owned buffers (pure, host-testable).
 *
 * Renderers never allocate and never write past the buffer: output that does
 * not fit is cut and flagged in heaptop_buf_t::truncated.
 */

#ifndef HEAPTOP_RENDER_H
#define HEAPTOP_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "heaptop_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Append-only text buffer over caller memory; always NUL-terminated. */
typedef struct heaptop_buf
{
  char *p;
  size_t cap;
  size_t len;
  bool truncated;
} heaptop_buf_t;

void heaptop_buf_init(heaptop_buf_t *b, char *mem, size_t cap);

void heaptop_buf_printf(heaptop_buf_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/** @brief Human-readable size: "512", "1.5K", "7.8M". */
void heaptop_fmt_bytes(char *out, size_t len, uint32_t bytes);

/** ASCII levels for sparklines, lowest first (UTF-8 blocks garble on some serial monitors). */
#define HEAPTOP_SPARK_LEVELS "_.,-~=+*#@"

/** How the live view is currently shown. */
typedef struct heaptop_top_view
{
  heaptop_sort_t sort;
  bool paused;
  uint32_t refresh_ms; /**< fastest redraw; new samples still arrive once per period */
} heaptop_top_view_t;

/** @brief Uptime as "59s", "2m05s", "1h02m03s" or "1d01h01m". */
void heaptop_fmt_uptime(char *out, size_t len, uint64_t us);

/** @brief One character per value, scaled between the series min and max; cut to fit @p len. */
void heaptop_render_sparkline(char *out, size_t len, const uint32_t *v, size_t n);

/** @brief Full `ht top` frame: header, core bars, regions with trends, task table. */
void heaptop_render_top(heaptop_buf_t *b, const heaptop_snapshot_t *s, const heaptop_top_view_t *view);

/** @brief Region table: total, free, min free, largest block, fragmentation, blocks. */
void heaptop_render_heap(heaptop_buf_t *b, const heaptop_snapshot_t *s);

/** @brief Task table sorted by @p key. */
void heaptop_render_tasks(heaptop_buf_t *b, const heaptop_snapshot_t *s, heaptop_sort_t key);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_RENDER_H
