#include "badge_app.h"

#include "badge_config.h"
#include "badge_contacts.h"
#include "badge_diagnostics.h"
#include "badge_events.h"
#include "badge_hardware_info.h"
#include "badge_state.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "modules/buttons.h"
#include "modules/buzzer.h"
#include "modules/infrared.h"
#include "modules/oled.h"
#include "modules/rgb.h"

static const char *TAG = "badge_app";
static QueueHandle_t event_queue;

/*
 * Startup order:
 * 1. Initialize the shared badge state with badge_state_init().
 * 2. Create the application event queue with 24 badge_event_t entries.
 * 3. Initialize the RGB module.
 * 4. Initialize the buzzer module.
 * 5. Start the button task and pass it the event queue.
 * 6. Start the infrared task and pass it the same event queue.
 * 7. Start the OLED display task.
 * 8. Create app_event_task last so it handles events after all producers
 *    and output modules are ready.
 */


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

static uint8_t mode_color(badge_mode_t mode) {
  switch (mode) {
    case BADGE_MODE_AUTO:
      return BADGE_MODE_COLOR_AUTO;
    case BADGE_MODE_MANUAL:
      return BADGE_MODE_COLOR_MANUAL;
    case BADGE_MODE_RECEIVE_ONLY:
      return BADGE_MODE_COLOR_RECEIVE_ONLY;
    default:
      return BADGE_OWNER_ROLE_COLOR_INDEX;
  }
}

static void apply_mode(badge_mode_t mode) {
  const uint8_t color = mode_color(mode);
  if (rgb_set_palette(color) == ESP_OK) {
    badge_state_set_palette(color, badge_state_mode_name(mode));
  }
  badge_state_set_mode(mode);
}

static void handle_button(const badge_event_t *event) {
  const badge_button_id_t id = event->data.button.id;
  if (!event->data.button.pressed) {
    badge_state_set_event("%s RELEASED", button_name(id));
    return;
  }

  switch (id) {
    case BADGE_BUTTON_UP:
      badge_state_change_page(-1);
      buzzer_beep(45);
      break;
    case BADGE_BUTTON_DOWN:
      badge_state_change_page(1);
      buzzer_beep(45);
      break;
    case BADGE_BUTTON_OK: {
      buzzer_beep(80);
      badge_state_snapshot_t state;
      badge_state_snapshot(&state);
      if (state.page == BADGE_PAGE_CONTACTS) {
        badge_state_advance_contact_window(
            (uint16_t)badge_contacts_count());
      } else if (state.mode == BADGE_MODE_RECEIVE_ONLY) {
        badge_state_set_event("TX BLOCKED RX ONLY");
      } else if (!infrared_send_manual()) {
        badge_state_set_event("IR COMMAND QUEUE FULL");
      }
      break;
    }
    case BADGE_BUTTON_BACK: {
      buzzer_beep(60);
      badge_state_snapshot_t state;
      badge_state_snapshot(&state);
      if (state.page == BADGE_PAGE_CONTACTS) {
        badge_contacts_clear();
        badge_state_clear_contacts();
      } else if (!infrared_cycle_mode()) {
        badge_state_set_event("IR COMMAND QUEUE FULL");
      }
      break;
    }
  }
}

static const char *contact_result_name(badge_contact_result_t result) {
  switch (result) {
    case BADGE_CONTACT_NEW:
      return "NEW";
    case BADGE_CONTACT_UPDATED:
      return "UPDATED";
    case BADGE_CONTACT_DUPLICATE:
      return "DUPLICATE";
    case BADGE_CONTACT_REPLACED:
      return "REPLACED";
    default:
      return "ERROR";
  }
}

static void handle_contact(const badge_event_t *event) {
  const uint16_t badge_id = event->data.ir_contact.badge_id;
  const badge_contact_result_t result = badge_contacts_record(
      badge_id, event->data.ir_contact.sequence,
      (uint32_t)(esp_timer_get_time() / 1000));
  if (result == BADGE_CONTACT_ERROR) {
    badge_state_set_event("CONTACT STORE ERROR");
    return;
  }
  if (result == BADGE_CONTACT_DUPLICATE) {
    badge_diagnostics_ir_duplicate();
  }
  badge_state_set_contact(badge_id, (uint16_t)badge_contacts_count(),
                          contact_result_name(result));
}

static void app_event_task(void *context) {
  (void)context;
  badge_event_t event;
  TickType_t last_diagnostics_log_tick = xTaskGetTickCount();

  while (true) {
    if (xQueueReceive(event_queue, &event, pdMS_TO_TICKS(1000)) == pdTRUE) {
      switch (event.type) {
        case BADGE_EVENT_BUTTON:
          handle_button(&event);
          break;
        case BADGE_EVENT_IR_TX:
          badge_state_set_ir_tx(
              event.data.ir_tx.count,
              event.data.ir_tx.reason == BADGE_IR_REASON_AUTO ? "AUTO" : "OK",
              event.data.ir_tx.sequence);
          break;
        case BADGE_EVENT_IR_RX:
          badge_state_set_ir_rx(event.data.ir_rx.count);
          break;
        case BADGE_EVENT_IR_CONTACT:
          handle_contact(&event);
          break;
        case BADGE_EVENT_IR_MODE_CHANGED:
          apply_mode(event.data.ir_mode.mode);
          break;
      }
    }
    badge_diagnostics_sample_stack(BADGE_TASK_APP);

    const TickType_t now = xTaskGetTickCount();
    if ((now - last_diagnostics_log_tick) >= pdMS_TO_TICKS(10000)) {
      last_diagnostics_log_tick = now;
      badge_diagnostics_log_snapshot();
    }
  }
}

esp_err_t badge_app_start(void) {
  badge_hardware_info_log();
  badge_diagnostics_init();
  ESP_RETURN_ON_ERROR(badge_contacts_init(), TAG, "Contact store init failed");
  ESP_RETURN_ON_ERROR(badge_state_init(), TAG, "State init failed");

  event_queue = xQueueCreate(24, sizeof(badge_event_t));
  if (event_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  ESP_RETURN_ON_ERROR(rgb_init(), TAG, "RGB init failed");
  ESP_RETURN_ON_ERROR(rgb_set_palette(BADGE_MODE_COLOR_AUTO), TAG,
                      "Initial mode color failed");
  badge_state_set_palette(rgb_palette_index(), "AUTO");
  ESP_RETURN_ON_ERROR(buzzer_start(), TAG, "Buzzer start failed");
  ESP_RETURN_ON_ERROR(buttons_start(event_queue), TAG,
                      "Buttons start failed");
  ESP_RETURN_ON_ERROR(infrared_start(event_queue), TAG,
                      "Infrared start failed");
  ESP_RETURN_ON_ERROR(oled_start(), TAG, "OLED start failed");

  if (xTaskCreate(app_event_task, "badge_app", 4096, NULL, 6, NULL) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }

  badge_state_set_event("BADGE FREERTOS READY");
  ESP_LOGI(TAG,
           "Pin map: OLED SDA8 SCL9, BACK13 OK10 DOWN11 UP12, BUZZ15, "
           "IR TX%d RX%d, RGB38",
           (int)BADGE_PIN_IR_TX, (int)BADGE_PIN_IR_RX);
  buzzer_beep(80);
  return ESP_OK;
}
