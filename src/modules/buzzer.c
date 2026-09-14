#include "modules/buzzer.h"

#include "badge_config.h"
#include "badge_diagnostics.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "register_gpio.h"

static QueueHandle_t command_queue;

static void buzzer_set_enabled(bool enabled) {
  // The module is active-low. Runtime writes go directly to W1TS/W1TC.
  register_gpio_write(BADGE_PIN_BUZZER, !enabled);
}

static void buzzer_task(void *context) {
  (void)context;
  uint16_t duration_ms;

  while (true) {
    if (xQueueReceive(command_queue, &duration_ms, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    buzzer_set_enabled(true);
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);

    while (true) {
      const TickType_t now = xTaskGetTickCount();
      if ((int32_t)(now - deadline) >= 0) {
        break;
      }

      const TickType_t remaining = deadline - now;
      uint16_t replacement_ms;
      if (xQueueReceive(command_queue, &replacement_ms, remaining) == pdTRUE) {
        deadline = xTaskGetTickCount() + pdMS_TO_TICKS(replacement_ms);
      } else {
        break;
      }
    }

    buzzer_set_enabled(false);
    badge_diagnostics_sample_stack(BADGE_TASK_BUZZER);
  }
}

esp_err_t buzzer_start(void) {
  // Preload the inactive level before enabling output to avoid a startup chirp.
  register_gpio_write(BADGE_PIN_BUZZER, true);
  const gpio_config_t config = {
      .pin_bit_mask = UINT64_C(1) << BADGE_PIN_BUZZER,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&config), "buzzer", "GPIO config failed");
  buzzer_set_enabled(false);

  command_queue = xQueueCreate(4, sizeof(uint16_t));
  if (command_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }
  if (xTaskCreate(buzzer_task, "buzzer", 2048, NULL, 6, NULL) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool buzzer_beep(uint16_t duration_ms) {
  if (command_queue == NULL || duration_ms == 0) {
    return false;
  }
  if (xQueueSend(command_queue, &duration_ms, 0) == pdTRUE) {
    return true;
  }
  badge_diagnostics_queue_drop(BADGE_QUEUE_BUZZER_COMMAND);
  return false;
}
