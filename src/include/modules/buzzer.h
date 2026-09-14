#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t buzzer_start(void);
bool buzzer_beep(uint16_t duration_ms);
