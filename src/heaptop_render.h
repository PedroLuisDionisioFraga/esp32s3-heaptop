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

/** @brief Region table: total, free, min free, largest block, fragmentation, blocks. */
void heaptop_render_heap(heaptop_buf_t *b, const heaptop_snapshot_t *s);

/** @brief Task table sorted by @p key. */
void heaptop_render_tasks(heaptop_buf_t *b, const heaptop_snapshot_t *s, heaptop_sort_t key);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_RENDER_H
