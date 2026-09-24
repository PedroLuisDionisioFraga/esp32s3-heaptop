#include "heaptop.h"

#include "esp_log.h"

static const char *TAG = "HEAPTOP";

esp_err_t heaptop_init(const heaptop_config_t *config)
{
  (void)config;
  ESP_LOGI(TAG, "heaptop initialised");
  return ESP_OK;
}
