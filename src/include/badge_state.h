#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "badge_events.h"
#include "esp_err.h"

typedef enum {
  BADGE_PAGE_PROFILE = 0,
  BADGE_PAGE_CONTACTS,
  BADGE_PAGE_IR_STATUS,
  BADGE_PAGE_SYSTEM,
  BADGE_PAGE_COUNT,
} badge_page_t;

typedef struct {
  uint32_t ir_tx_frames;
  uint32_t ir_rx_pulses;
  badge_mode_t mode;
  uint8_t palette_index;
  badge_page_t page;
  uint16_t contact_count;
  uint16_t last_contact_id;
  bool has_last_contact;
  uint8_t contact_view_offset;
  char last_event[64];
} badge_state_snapshot_t;

esp_err_t badge_state_init(void);
void badge_state_set_event(const char *format, ...);
void badge_state_set_palette(uint8_t palette_index, const char *direction);
void badge_state_set_ir_tx(uint32_t count, const char *reason,
                           uint8_t sequence);
void badge_state_set_ir_rx(uint32_t count);
void badge_state_set_mode(badge_mode_t mode);
void badge_state_set_contact(uint16_t badge_id, uint16_t contact_count,
                             const char *result);
void badge_state_clear_contacts(void);
void badge_state_advance_contact_window(uint16_t contact_count);
void badge_state_change_page(int8_t delta);
void badge_state_snapshot(badge_state_snapshot_t *snapshot);
const char *badge_state_mode_name(badge_mode_t mode);
