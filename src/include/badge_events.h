#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  BADGE_BUTTON_BACK,
  BADGE_BUTTON_OK,
  BADGE_BUTTON_DOWN,
  BADGE_BUTTON_UP,
} badge_button_id_t;

typedef enum {
  BADGE_EVENT_BUTTON,
  BADGE_EVENT_IR_TX,
  BADGE_EVENT_IR_RX,
  BADGE_EVENT_IR_CONTACT,
  BADGE_EVENT_IR_MODE_CHANGED,
} badge_event_type_t;

typedef enum {
  BADGE_IR_REASON_AUTO,
  BADGE_IR_REASON_OK,
} badge_ir_reason_t;

typedef enum {
  BADGE_MODE_AUTO = 0,
  BADGE_MODE_MANUAL,
  BADGE_MODE_RECEIVE_ONLY,
  BADGE_MODE_COUNT,
} badge_mode_t;

typedef struct {
  badge_event_type_t type;
  union {
    struct {
      badge_button_id_t id;
      bool pressed;
    } button;
    struct {
      uint32_t count;
      badge_ir_reason_t reason;
      uint8_t sequence;
    } ir_tx;
    struct {
      uint32_t count;
    } ir_rx;
    struct {
      uint16_t badge_id;
      uint8_t sequence;
    } ir_contact;
    struct {
      badge_mode_t mode;
    } ir_mode;
  } data;
} badge_event_t;
