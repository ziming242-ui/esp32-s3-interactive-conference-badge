#include "badge_state.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "badge_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "badge_state";
static SemaphoreHandle_t state_mutex;
static badge_state_snapshot_t state;

static const char *page_name(badge_page_t page) {
  switch (page) {
    case BADGE_PAGE_PROFILE:
      return "PROFILE";
    case BADGE_PAGE_CONTACTS:
      return "CONTACTS";
    case BADGE_PAGE_IR_STATUS:
      return "IR STATUS";
    case BADGE_PAGE_SYSTEM:
      return "SYSTEM";
    default:
      return "UNKNOWN";
  }
}

const char *badge_state_mode_name(badge_mode_t mode) {
  switch (mode) {
    case BADGE_MODE_AUTO:
      return "AUTO";
    case BADGE_MODE_MANUAL:
      return "MANUAL";
    case BADGE_MODE_RECEIVE_ONLY:
      return "RX ONLY";
    default:
      return "UNKNOWN";
  }
}

static void set_event_locked(const char *format, va_list args) {
  vsnprintf(state.last_event, sizeof(state.last_event), format, args);
}

esp_err_t badge_state_init(void) {
  state_mutex = xSemaphoreCreateMutex();
  if (state_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  memset(&state, 0, sizeof(state));
  state.mode = BADGE_MODE_AUTO;
  state.page = BADGE_PAGE_PROFILE;
  strlcpy(state.last_event, "BOOT", sizeof(state.last_event));
  return ESP_OK;
}

void badge_state_set_event(const char *format, ...) {
  va_list args;
  va_start(args, format);
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  set_event_locked(format, args);
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  va_end(args);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_set_palette(uint8_t palette_index, const char *direction) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.palette_index = palette_index;
  snprintf(state.last_event, sizeof(state.last_event), "%s COLOR %u", direction,
           palette_index);
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_set_ir_tx(uint32_t count, const char *reason,
                           uint8_t sequence) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.ir_tx_frames = count;
  snprintf(state.last_event, sizeof(state.last_event), "IR TX %lu %s S%u",
           (unsigned long)count, reason, (unsigned)sequence);
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_set_ir_rx(uint32_t count) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.ir_rx_pulses = count;
  snprintf(state.last_event, sizeof(state.last_event), "IR RX PULSE %lu",
           (unsigned long)count);
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_set_mode(badge_mode_t mode) {
  if (mode >= BADGE_MODE_COUNT) {
    return;
  }

  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.mode = mode;
  snprintf(state.last_event, sizeof(state.last_event), "MODE %s",
           badge_state_mode_name(mode));
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_set_contact(uint16_t badge_id, uint16_t contact_count,
                             const char *result) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.contact_count = contact_count;
  state.last_contact_id = badge_id;
  state.has_last_contact = true;
  state.contact_view_offset = 0;
  snprintf(state.last_event, sizeof(state.last_event), "ID %03u %s",
           (unsigned)badge_id, result);
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_clear_contacts(void) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  state.contact_count = 0;
  state.last_contact_id = 0;
  state.has_last_contact = false;
  state.contact_view_offset = 0;
  strlcpy(state.last_event, "CONTACTS CLEARED", sizeof(state.last_event));
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_advance_contact_window(uint16_t contact_count) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  if (contact_count <= BADGE_CONTACTS_PER_SCREEN) {
    state.contact_view_offset = 0;
  } else {
    const uint16_t next =
        state.contact_view_offset + BADGE_CONTACTS_PER_SCREEN;
    state.contact_view_offset =
        next >= contact_count ? 0 : (uint8_t)next;
  }
  snprintf(state.last_event, sizeof(state.last_event), "CONTACTS %u-%u",
           (unsigned)(contact_count == 0 ? 0
                                        : state.contact_view_offset + 1),
           (unsigned)(state.contact_view_offset +
                      BADGE_CONTACTS_PER_SCREEN > contact_count
                          ? contact_count
                          : state.contact_view_offset +
                                BADGE_CONTACTS_PER_SCREEN));
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_change_page(int8_t delta) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  int next_page = (int)state.page + delta;
  while (next_page < 0) {
    next_page += BADGE_PAGE_COUNT;
  }
  state.page = (badge_page_t)(next_page % BADGE_PAGE_COUNT);
  snprintf(state.last_event, sizeof(state.last_event), "PAGE %s",
           page_name(state.page));
  char message[sizeof(state.last_event)];
  strlcpy(message, state.last_event, sizeof(message));
  xSemaphoreGive(state_mutex);
  ESP_LOGI(TAG, "%s", message);
}

void badge_state_snapshot(badge_state_snapshot_t *snapshot) {
  if (snapshot == NULL) {
    return;
  }

  xSemaphoreTake(state_mutex, portMAX_DELAY);
  memcpy(snapshot, &state, sizeof(*snapshot));
  xSemaphoreGive(state_mutex);
}
