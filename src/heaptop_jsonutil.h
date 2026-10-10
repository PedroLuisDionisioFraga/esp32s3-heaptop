/**
 * @file heaptop_jsonutil.h
 * @brief Pieces shared by the JSON Lines stream and the JSON document (pure, host-testable).
 *
 * Header-only on purpose: both writers use the same escaping and the same names, so they cannot drift apart.
 * Internal to heaptop; not part of the public API.
 */

#ifndef HEAPTOP_JSONUTIL_H
#define HEAPTOP_JSONUTIL_H

#include <stdint.h>

#include "heaptop_render.h"
#include "heaptop_types.h"

/** Append @p s as a quoted JSON string. Quotes, backslashes and control characters are escaped (up to 6 bytes each). */
static inline void heaptop_jsonutil_str(heaptop_buf_t *b, const char *s)
{
  heaptop_buf_printf(b, "\"");
  for (; s && *s; s++)
  {
    const unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\')
      heaptop_buf_printf(b, "\\%c", c);
    else if (c < 0x20)
      heaptop_buf_printf(b, "\\u%04x", c);
    else
      heaptop_buf_printf(b, "%c", c);
  }
  heaptop_buf_printf(b, "\"");
}

/** Task state as text: "running", "ready", "blocked", "suspended", "deleted", or "unknown" when out of range. */
static inline const char *heaptop_jsonutil_state_name(uint8_t state)
{
  static const char *const names[] = {"running", "ready", "blocked", "suspended", "deleted"};
  return state <= HEAPTOP_TASK_DELETED ? names[state] : "unknown";
}

/** Region key: "internal", "dma" or "psram", or "unknown" when out of range. */
static inline const char *heaptop_jsonutil_region_key(int region)
{
  static const char *const keys[HEAPTOP_REGION_COUNT] = {"internal", "dma", "psram"};
  return region >= 0 && region < HEAPTOP_REGION_COUNT ? keys[region] : "unknown";
}

/** Microseconds as whole milliseconds, widened for printf's %llu. */
static inline unsigned long long heaptop_jsonutil_ms(uint64_t us)
{
  return (unsigned long long)(us / 1000u);
}

#endif  // HEAPTOP_JSONUTIL_H
