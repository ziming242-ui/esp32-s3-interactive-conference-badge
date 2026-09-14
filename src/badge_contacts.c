#include "badge_contacts.h"

#include <limits.h>
#include <string.h>

#include "badge_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
  badge_contact_t contact;
  uint32_t order;
  bool occupied;
} contact_slot_t;

static SemaphoreHandle_t contacts_mutex;
static contact_slot_t contacts[BADGE_CONTACT_CAPACITY];
static size_t contact_count;
static uint32_t next_order;

esp_err_t badge_contacts_init(void) {
  contacts_mutex = xSemaphoreCreateMutex();
  if (contacts_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  memset(contacts, 0, sizeof(contacts));
  contact_count = 0;
  next_order = 1;
  return ESP_OK;
}

void badge_contacts_clear(void) {
  if (contacts_mutex == NULL) {
    return;
  }

  xSemaphoreTake(contacts_mutex, portMAX_DELAY);
  memset(contacts, 0, sizeof(contacts));
  contact_count = 0;
  next_order = 1;
  xSemaphoreGive(contacts_mutex);
}

badge_contact_result_t badge_contacts_record(uint16_t badge_id,
                                             uint8_t sequence,
                                             uint32_t now_ms) {
  if (contacts_mutex == NULL || badge_id == 0) {
    return BADGE_CONTACT_ERROR;
  }

  xSemaphoreTake(contacts_mutex, portMAX_DELAY);

  size_t free_index = BADGE_CONTACT_CAPACITY;
  size_t oldest_index = 0;
  uint32_t oldest_order = UINT32_MAX;

  for (size_t index = 0; index < BADGE_CONTACT_CAPACITY; ++index) {
    contact_slot_t *slot = &contacts[index];
    if (slot->occupied && slot->contact.badge_id == badge_id) {
      const bool duplicate = slot->contact.last_sequence == sequence;
      slot->contact.last_sequence = sequence;
      slot->contact.last_seen_ms = now_ms;
      slot->order = next_order++;
      if (!duplicate) {
        ++slot->contact.seen_count;
      }
      xSemaphoreGive(contacts_mutex);
      return duplicate ? BADGE_CONTACT_DUPLICATE : BADGE_CONTACT_UPDATED;
    }

    if (!slot->occupied && free_index == BADGE_CONTACT_CAPACITY) {
      free_index = index;
    }
    if (slot->occupied && slot->order < oldest_order) {
      oldest_order = slot->order;
      oldest_index = index;
    }
  }

  const bool replacing = free_index == BADGE_CONTACT_CAPACITY;
  const size_t target_index = replacing ? oldest_index : free_index;
  contacts[target_index] = (contact_slot_t){
      .contact =
          {
              .badge_id = badge_id,
              .last_sequence = sequence,
              .seen_count = 1,
              .last_seen_ms = now_ms,
          },
      .order = next_order++,
      .occupied = true,
  };
  if (!replacing) {
    ++contact_count;
  }

  xSemaphoreGive(contacts_mutex);
  return replacing ? BADGE_CONTACT_REPLACED : BADGE_CONTACT_NEW;
}

size_t badge_contacts_count(void) {
  if (contacts_mutex == NULL) {
    return 0;
  }
  xSemaphoreTake(contacts_mutex, portMAX_DELAY);
  const size_t count = contact_count;
  xSemaphoreGive(contacts_mutex);
  return count;
}

bool badge_contacts_get_recent(size_t recent_index,
                               badge_contact_t *contact) {
  if (contacts_mutex == NULL || contact == NULL ||
      recent_index >= BADGE_CONTACT_CAPACITY) {
    return false;
  }

  xSemaphoreTake(contacts_mutex, portMAX_DELAY);
  if (recent_index >= contact_count) {
    xSemaphoreGive(contacts_mutex);
    return false;
  }

  uint32_t upper_order = UINT32_MAX;
  size_t selected_index = BADGE_CONTACT_CAPACITY;

  for (size_t rank = 0; rank <= recent_index; ++rank) {
    uint32_t best_order = 0;
    selected_index = BADGE_CONTACT_CAPACITY;
    for (size_t index = 0; index < BADGE_CONTACT_CAPACITY; ++index) {
      const contact_slot_t *slot = &contacts[index];
      if (slot->occupied && slot->order < upper_order &&
          slot->order > best_order) {
        best_order = slot->order;
        selected_index = index;
      }
    }
    if (selected_index == BADGE_CONTACT_CAPACITY) {
      xSemaphoreGive(contacts_mutex);
      return false;
    }
    upper_order = contacts[selected_index].order;
  }

  *contact = contacts[selected_index].contact;
  xSemaphoreGive(contacts_mutex);
  return true;
}
