#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
  uint16_t badge_id;
  uint8_t last_sequence;
  uint32_t seen_count;
  uint32_t last_seen_ms;
} badge_contact_t;

typedef enum {
  BADGE_CONTACT_NEW,
  BADGE_CONTACT_UPDATED,
  BADGE_CONTACT_DUPLICATE,
  BADGE_CONTACT_REPLACED,
  BADGE_CONTACT_ERROR,
} badge_contact_result_t;

esp_err_t badge_contacts_init(void);
void badge_contacts_clear(void);
badge_contact_result_t badge_contacts_record(uint16_t badge_id,
                                             uint8_t sequence,
                                             uint32_t now_ms);
size_t badge_contacts_count(void);
bool badge_contacts_get_recent(size_t recent_index,
                               badge_contact_t *contact);
