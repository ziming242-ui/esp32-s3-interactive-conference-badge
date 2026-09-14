#pragma once

#include "driver/gpio.h"

// Diagnostic modes are mutually exclusive. Set both to 0 for normal firmware.
#define BADGE_SCOPE_TEST_MODE 0
#define BADGE_OLED_BUTTON_TEST_MODE 0

// Set to 1 only for single-board IR loopback testing. Final firmware must use 0.
#define BADGE_IR_SELF_TEST_MODE 0

#define BADGE_PIN_OLED_SDA GPIO_NUM_8
#define BADGE_PIN_OLED_SCL GPIO_NUM_9
#define BADGE_PIN_BUTTON_BACK GPIO_NUM_13
#define BADGE_PIN_BUTTON_OK GPIO_NUM_10
#define BADGE_PIN_BUTTON_DOWN GPIO_NUM_11
#define BADGE_PIN_BUTTON_UP GPIO_NUM_12
#define BADGE_PIN_BUZZER GPIO_NUM_15
#define BADGE_PIN_IR_TX GPIO_NUM_17
#define BADGE_PIN_IR_RX GPIO_NUM_20
#define BADGE_PIN_RGB GPIO_NUM_38

#define BADGE_BUTTON_DEBOUNCE_MS 25
#define BADGE_DISPLAY_REFRESH_MS 250
#define BADGE_IR_RX_REPORT_MS 250
#define BADGE_IR_AUTO_INTERVAL_MIN_MS 5000
#define BADGE_IR_AUTO_INTERVAL_MAX_MS 10000

#define BADGE_IR_CARRIER_HZ 38000
#define BADGE_IR_PROTOCOL_VERSION 1
#define BADGE_IR_PREAMBLE_MARK_US 9000
#define BADGE_IR_PREAMBLE_SPACE_US 4500
#define BADGE_IR_BIT_MARK_US 560
#define BADGE_IR_ZERO_SPACE_US 560
#define BADGE_IR_ONE_SPACE_US 1690
#define BADGE_IR_FRAME_BYTES 5
#define BADGE_IR_FRAME_BITS (BADGE_IR_FRAME_BYTES * 8)

#define BADGE_OLED_WIDTH 128
#define BADGE_OLED_HEIGHT 64
#define BADGE_CONTACT_CAPACITY 64
#define BADGE_CONTACTS_PER_SCREEN 4

// Conference attendee profile. Keep display text within 21 ASCII characters.
#define BADGE_OWNER_NAME "DemoUser"
#define BADGE_OWNER_ORG "DemoLab"
#define BADGE_OWNER_ID 1
#define BADGE_OWNER_ROLE "STUDENT"

// Add one X(...) line per attendee before compiling the conference image.
#define BADGE_DIRECTORY_ENTRIES(X)                                      \
  X(BADGE_OWNER_ID, BADGE_OWNER_NAME, BADGE_OWNER_ORG, BADGE_OWNER_ROLE)

// Palette: 0 blue, 1 green, 2 red, 3 yellow, 4 purple, 5 white.
#define BADGE_OWNER_ROLE_COLOR_INDEX 0
#define BADGE_MODE_COLOR_AUTO 1
#define BADGE_MODE_COLOR_MANUAL 0
#define BADGE_MODE_COLOR_RECEIVE_ONLY 2
