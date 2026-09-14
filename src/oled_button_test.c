#include "oled_button_test.h"

#include "badge_events.h"
#include "badge_state.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "modules/buttons.h"
#include "modules/oled.h"

static const char *TAG = "oled_button_test";
static QueueHandle_t event_queue;

static const char *button_name(badge_button_id_t id) {
  switch (id) {
    case BADGE_BUTTON_BACK:
      return "BACK";
    case BADGE_BUTTON_OK:
      return "OK";
    case BADGE_BUTTON_DOWN:
      return "DOWN";
    case BADGE_BUTTON_UP:
      return "UP";
    default:
      return "UNKNOWN";
  }
}

static void button_event_task(void *context) {
  (void)context;
  badge_event_t event;

  while (true) {
    if (xQueueReceive(event_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if (event.type != BADGE_EVENT_BUTTON) {
      continue;
    }

    const char *name = button_name(event.data.button.id);
    const char *action = event.data.button.pressed ? "PRESSED" : "RELEASED";
    ESP_LOGI(TAG, "BUTTON %-4s %s", name, action);
    badge_state_set_event("%s %s", name, action);
  }
}

esp_err_t oled_button_test_start(void) {
  ESP_RETURN_ON_ERROR(badge_state_init(), TAG, "State init failed");

  event_queue = xQueueCreate(16, sizeof(badge_event_t));
  if (event_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  if (xTaskCreate(button_event_task, "button_test", 3072, NULL, 6, NULL) !=
      pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  ESP_RETURN_ON_ERROR(buttons_start(event_queue), TAG,
                      "Button test start failed");
  badge_state_set_event("OLED BUTTON TEST");
  ESP_RETURN_ON_ERROR(oled_start(), TAG, "OLED test start failed");

  ESP_LOGW(TAG, "OLED + button diagnostic mode is active");
  ESP_LOGI(TAG, "OLED SDA=GPIO8 SCL=GPIO9; retry interval=2000 ms");
  ESP_LOGI(TAG, "Buttons: BACK=13 OK=10 DOWN=11 UP=12, active-low");
  return ESP_OK;
}
