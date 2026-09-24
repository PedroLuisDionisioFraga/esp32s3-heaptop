/**
 * @file heaptop_stream.h
 * @brief JSON Lines serializer for the machine-readable stream (pure, host-testable).
 *
 * Every line is one JSON object that starts with {"ht":2, so a host script can
 * pick heaptop lines out of ordinary log output. Sizes are bytes, percentages
 * are x10 integers, times are milliseconds of uptime.
 */

#ifndef HEAPTOP_STREAM_H
#define HEAPTOP_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include "heaptop_render.h"
#include "heaptop_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Protocol version carried in every line as "ht". */
#define HEAPTOP_STREAM_VERSION 2

/** @brief {"ht":2,"type":"sample",...}: regions, cores, failures, alerts. */
void heaptop_stream_sample(heaptop_buf_t *b, const heaptop_snapshot_t *s);

/** @brief One {"ht":2,"type":"task",...} line per task. */
void heaptop_stream_tasks(heaptop_buf_t *b, const heaptop_snapshot_t *s);

/** @brief The task line of s->tasks[@p i] only, so callers can write line by line. */
void heaptop_stream_task(heaptop_buf_t *b, const heaptop_snapshot_t *s, uint16_t i);

/** @brief {"ht":2,"type":"alert",...} for an alert that turned on or off. */
void heaptop_stream_alert(heaptop_buf_t *b, const heaptop_snapshot_t *s, uint32_t alert, bool active, const char *msg);

/** @brief {"ht":2,"type":"fail",...} for one failed allocation. */
void heaptop_stream_fail(heaptop_buf_t *b, const heaptop_snapshot_t *s, const heaptop_fail_t *f);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_STREAM_H
