#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t rgb_init(void);
esp_err_t rgb_set_palette(uint8_t palette_index);
uint8_t rgb_next(void);
uint8_t rgb_previous(void);
uint8_t rgb_palette_index(void);
