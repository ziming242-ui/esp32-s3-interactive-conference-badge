#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

esp_err_t infrared_start(QueueHandle_t event_queue);
bool infrared_send_manual(void);
bool infrared_cycle_mode(void);
