/**
 * @file heaptop_json.h
 * @brief One snapshot as a single JSON document, for a web UI or any HTTP/MQTT client (pure, host-testable).
 *
 * heaptop_get_snapshot() copies the latest sample; heaptop_json_snapshot() writes that copy out. The field names
 * are those of the stream protocol (see the README), so one parser serves both. Sizes are bytes, `*10` fields are
 * percent x 10, times are milliseconds of uptime, and `null` means the data source is disabled or the region does
 * not exist.
 *
 * Nothing is allocated. The text is built in a small buffer on the caller's stack and handed to a sink in chunks,
 * so it suits a task with a small stack and does not disturb the heap it reports on.
 *
 * Document layout (keys in this order):
 *   ht, seq, t_ms, since_ms, period_ms, dt_ms, self_us, features,
 *   cpu10 (per core, or null without run-time stats),
 *   regions {internal, dma, psram}: total, free, min, largest, frag10, used_blocks, free_blocks (or null),
 *   failures (or null without the failed-allocation callback), alerts (names of the active alerts),
 *   limits {dram_free_min, dram_largest_min, frag_pct_max, psram_free_min, stack_hwm_min, task_growth}
 *     when @p limits is given,
 *   trend {len, internal_free[], internal_largest[], psram_free[] when PSRAM exists} with HEAPTOP_JSON_TRENDS,
 *   tasks [{name, state, prio, core, cpu10, hwm, and heap, peak, psram, growth, leak only with task tracking}]
 *     and tasks_truncated with HEAPTOP_JSON_TASKS.
 */

#ifndef HEAPTOP_JSON_H
#define HEAPTOP_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "heaptop_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Include "tasks" and "tasks_truncated". */
#define HEAPTOP_JSON_TASKS (1u << 0)
/** Include "trend": the 40-sample series, oldest first. */
#define HEAPTOP_JSON_TRENDS (1u << 1)
/** Everything the document can carry. */
#define HEAPTOP_JSON_ALL (HEAPTOP_JSON_TASKS | HEAPTOP_JSON_TRENDS)

/**
 * @brief Receives one chunk of the document, in order.
 *
 * @param data Not NUL-terminated, valid only during the call.
 * @param len Bytes in @p data, at most 512.
 * @return false to stop, for example when the HTTP connection is gone; no more chunks follow.
 */
typedef bool (*heaptop_json_sink_t)(const char *data, size_t len, void *ctx);

/**
 * @brief Write @p s as one JSON object, without a trailing newline.
 *
 * Uses about 600 bytes of stack and allocates nothing.
 *
 * @param s A copy from heaptop_get_snapshot(). Serializing the live one would hold its lock while the sink runs.
 * @param limits The alert limits to report, or NULL to leave "limits" out. Pass the ones given to heaptop_init().
 * @param flags HEAPTOP_JSON_* bits; 0 writes the sample without tasks and trends.
 * @param sink Called for every chunk.
 * @param ctx Passed to @p sink.
 * @return true if the whole document went out; false for a NULL @p s or @p sink, or when @p sink stopped it.
 */
bool heaptop_json_snapshot(const heaptop_snapshot_t *s, const heaptop_thresholds_t *limits, uint32_t flags,
                           heaptop_json_sink_t sink, void *ctx);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_JSON_H
