/**
 * @file heaptop.h
 * @brief htop-like heap and task monitor for ESP-IDF, driven over the serial console.
 */

#ifndef HEAPTOP_H
#define HEAPTOP_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct heaptop_config
{
  uint32_t sample_period_ms;
} heaptop_config_t;

/**
 * @brief Start heaptop.
 *
 * @param config NULL uses the Kconfig defaults.
 * @return ESP_OK, including a second call while already running.
 */
esp_err_t heaptop_init(const heaptop_config_t *config);

#ifdef __cplusplus
}
#endif

#endif  // HEAPTOP_H
