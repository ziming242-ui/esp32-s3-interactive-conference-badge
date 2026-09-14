#include "modules/buttons.h"

#include <stddef.h>

#include "badge_config.h"
#include "badge_diagnostics.h"
#include "badge_events.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "register_gpio.h"

typedef struct {
  badge_button_id_t id;
  gpio_num_t pin;
  bool stable_state;
  bool raw_state;
  int64_t last_change_ms;
} button_state_t;

static QueueHandle_t app_event_queue;

static button_state_t buttons[] = {
    {.id = BADGE_BUTTON_BACK, .pin = BADGE_PIN_BUTTON_BACK},
    {.id = BADGE_BUTTON_OK, .pin = BADGE_PIN_BUTTON_OK},
    {.id = BADGE_BUTTON_DOWN, .pin = BADGE_PIN_BUTTON_DOWN},
    {.id = BADGE_BUTTON_UP, .pin = BADGE_PIN_BUTTON_UP},
};

static void buttons_task(void *context) {
  (void)context;
  const size_t button_count = sizeof(buttons) / sizeof(buttons[0]);
  const int64_t initial_ms = esp_timer_get_time() / 1000;
  int64_t last_stack_sample_ms = initial_ms;

  for (size_t i = 0; i < button_count; ++i) {
    const bool state = register_gpio_read(buttons[i].pin);
    buttons[i].stable_state = state;
    buttons[i].raw_state = state;
    buttons[i].last_change_ms = initial_ms;
  }

  while (true) {
    const int64_t now_ms = esp_timer_get_time() / 1000;

    for (size_t i = 0; i < button_count; ++i) {
      button_state_t *button = &buttons[i];
      const bool raw_state = register_gpio_read(button->pin);

      if (raw_state != button->raw_state) {
        button->raw_state = raw_state;
        button->last_change_ms = now_ms;
      }

      if ((now_ms - button->last_change_ms) < BADGE_BUTTON_DEBOUNCE_MS ||
          raw_state == button->stable_state) {
        continue;
      }

      button->stable_state = raw_state;
      const badge_event_t event = {
          .type = BADGE_EVENT_BUTTON,
          .data.button = {
              .id = button->id,
              .pressed = !button->stable_state,
          },
      };
      if (xQueueSend(app_event_queue, &event, 0) != pdTRUE) {
        badge_diagnostics_queue_drop(BADGE_QUEUE_APP_EVENT);
      }
    }

    if ((now_ms - last_stack_sample_ms) >= 1000) {
      last_stack_sample_ms = now_ms;
      badge_diagnostics_sample_stack(BADGE_TASK_BUTTONS);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

esp_err_t buttons_start(QueueHandle_t event_queue) {
  if (event_queue == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  app_event_queue = event_queue;

  const uint64_t button_mask =
      (UINT64_C(1) << BADGE_PIN_BUTTON_BACK) |
      (UINT64_C(1) << BADGE_PIN_BUTTON_OK) |
      (UINT64_C(1) << BADGE_PIN_BUTTON_DOWN) |
      (UINT64_C(1) << BADGE_PIN_BUTTON_UP);
  const gpio_config_t config = {
      .pin_bit_mask = button_mask,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&config), "buttons", "GPIO config failed");

  if (xTaskCreate(buttons_task, "buttons", 3072, NULL, 5, NULL) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
