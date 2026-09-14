#include "modules/oled.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "badge_config.h"
#include "badge_contacts.h"
#include "badge_diagnostics.h"
#include "badge_directory.h"
#include "badge_state.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define OLED_I2C_PORT I2C_NUM_0
#define OLED_I2C_TIMEOUT_MS 100
#define OLED_RETRY_MS 2000
#define OLED_PAGE_COUNT (BADGE_OLED_HEIGHT / 8)
#define OLED_BUFFER_SIZE (BADGE_OLED_WIDTH * OLED_PAGE_COUNT)

static const char *TAG = "oled";
static uint8_t oled_address;
static uint8_t framebuffer[OLED_BUFFER_SIZE];
static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t oled_device;

static const char glyph_characters[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/()-. ";

static const uint8_t glyphs[][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x07, 0x08, 0x70, 0x08, 0x07},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // )
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
};

static esp_err_t i2c_probe(uint8_t address) {
  return i2c_master_probe(i2c_bus, address, OLED_I2C_TIMEOUT_MS);
}

static void release_oled_device(void) {
  if (oled_device == NULL) {
    return;
  }
  i2c_master_bus_rm_device(oled_device);
  oled_device = NULL;
}

static esp_err_t initialize_display(void) {
  const uint8_t commands[] = {
      0x00,        // Command stream control byte
      0xAE,        // Display off
      0xD5, 0x80,  // Clock divide
      0xA8, 0x3F,  // Multiplex ratio
      0xD3, 0x00,  // Display offset
      0x40,        // Start line
      0x8D, 0x14,  // Charge pump
      0x20, 0x02,  // Page addressing mode
      0xA1,        // Segment remap
      0xC8,        // COM scan direction
      0xDA, 0x12,  // COM pins
      0x81, 0xCF,  // Contrast
      0xD9, 0xF1,  // Pre-charge
      0xDB, 0x40,  // VCOM detect
      0xA4,        // Resume RAM display
      0xA6,        // Normal display
      0x2E,        // Disable scroll
      0xAF,        // Display on
  };

  return i2c_master_transmit(oled_device, commands, sizeof(commands),
                             OLED_I2C_TIMEOUT_MS);
}

static const uint8_t *find_glyph(char character) {
  character = (char)toupper((unsigned char)character);
  const char *match = strchr(glyph_characters, character);
  if (match == NULL) {
    match = strchr(glyph_characters, ' ');
  }
  return glyphs[match - glyph_characters];
}

static void draw_pixel(uint8_t x, uint8_t y) {
  if (x >= BADGE_OLED_WIDTH || y >= BADGE_OLED_HEIGHT) {
    return;
  }
  framebuffer[x + (y / 8) * BADGE_OLED_WIDTH] |= UINT8_C(1) << (y & 7);
}

static void draw_text(uint8_t x, uint8_t y, const char *text) {
  while (*text != '\0' && x + 5 <= BADGE_OLED_WIDTH) {
    const uint8_t *glyph = find_glyph(*text++);
    for (uint8_t column = 0; column < 5; ++column) {
      for (uint8_t row = 0; row < 7; ++row) {
        if ((glyph[column] & (UINT8_C(1) << row)) != 0) {
          draw_pixel(x + column, y + row);
        }
      }
    }
    x += 6;
  }
}

static esp_err_t flush_display(void) {
  uint8_t payload[BADGE_OLED_WIDTH + 1];
  payload[0] = 0x40;

  for (uint8_t page = 0; page < OLED_PAGE_COUNT; ++page) {
    const uint8_t position[] = {
        0x00,                         // Command stream control byte
        (uint8_t)(0xB0 | page),       // Page address
        0x00,                         // Low column
        0x10,                         // High column
    };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(
                            oled_device, position, sizeof(position),
                            OLED_I2C_TIMEOUT_MS),
                        TAG, "Set page %u failed", page);
    memcpy(&payload[1], &framebuffer[page * BADGE_OLED_WIDTH],
           BADGE_OLED_WIDTH);
    ESP_RETURN_ON_ERROR(i2c_master_transmit(oled_device, payload,
                                            sizeof(payload),
                                            OLED_I2C_TIMEOUT_MS),
                        TAG, "Frame data write failed");
  }
  return ESP_OK;
}

static void render_profile_page(const badge_state_snapshot_t *state) {
  char line[32];
  draw_text(0, 0, "CONFERENCE BADGE");
  draw_text(0, 8, BADGE_OWNER_NAME);
  draw_text(0, 16, BADGE_OWNER_ORG);
  snprintf(line, sizeof(line), "BADGE ID %03u", (unsigned)BADGE_OWNER_ID);
  draw_text(0, 24, line);
  snprintf(line, sizeof(line), "ROLE %s", BADGE_OWNER_ROLE);
  draw_text(0, 32, line);
  snprintf(line, sizeof(line), "MODE %s",
           badge_state_mode_name(state->mode));
  draw_text(0, 40, line);
  draw_text(0, 48, "UP/DOWN PAGES");
  draw_text(0, 56, state->last_event);
}

static void render_contacts_page(const badge_state_snapshot_t *state) {
  char line[32];
  snprintf(line, sizeof(line), "CONTACTS %u/64",
           (unsigned)state->contact_count);
  draw_text(0, 0, line);

  if (state->contact_count == 0) {
    draw_text(0, 16, "NO CONTACTS YET");
    draw_text(0, 32, "WAITING FOR IR ID");
    draw_text(0, 48, "BACK CLEAR");
    draw_text(0, 56, state->last_event);
    return;
  }

  const unsigned first = state->contact_view_offset + 1;
  unsigned last =
      state->contact_view_offset + BADGE_CONTACTS_PER_SCREEN;
  if (last > state->contact_count) {
    last = state->contact_count;
  }
  snprintf(line, sizeof(line), "RECENT %u-%u", first, last);
  draw_text(0, 8, line);

  for (size_t row = 0; row < BADGE_CONTACTS_PER_SCREEN; ++row) {
    badge_contact_t contact;
    const size_t recent_index = state->contact_view_offset + row;
    if (!badge_contacts_get_recent(recent_index, &contact)) {
      break;
    }
    const badge_directory_entry_t *entry =
        badge_directory_find(contact.badge_id);
    snprintf(line, sizeof(line), "ID%03u %.14s",
             (unsigned)contact.badge_id,
             entry == NULL ? "UNKNOWN" : entry->name);
    draw_text(0, (uint8_t)(16 + row * 8), line);
  }

  draw_text(0, 48, "OK NEXT BACK CLEAR");
  draw_text(0, 56, state->last_event);
}

static void render_ir_page(const badge_state_snapshot_t *state) {
  char line[32];
  draw_text(0, 0, "IR STATUS");
  snprintf(line, sizeof(line), "TX FRAMES %lu",
           (unsigned long)state->ir_tx_frames);
  draw_text(0, 8, line);
  snprintf(line, sizeof(line), "RX PULSES %lu",
           (unsigned long)state->ir_rx_pulses);
  draw_text(0, 16, line);
  snprintf(line, sizeof(line), "MODE %s",
           badge_state_mode_name(state->mode));
  draw_text(0, 24, line);
  draw_text(0, 32, "FRAME ID SEQ CRC8");
  if (state->has_last_contact) {
    snprintf(line, sizeof(line), "LAST ID %03u",
             (unsigned)state->last_contact_id);
  } else {
    strlcpy(line, "LAST ID NONE", sizeof(line));
  }
  draw_text(0, 40, line);
  draw_text(0, 48, "PAGE 3/4");
  draw_text(0, 56, state->last_event);
}

static void render_system_page(const badge_state_snapshot_t *state) {
  char line[32];
  badge_diagnostics_snapshot_t diagnostics;
  badge_diagnostics_snapshot(&diagnostics);

  draw_text(0, 0, "SYSTEM");
  snprintf(line, sizeof(line), "QUEUE DROPS %lu",
           (unsigned long)badge_diagnostics_total_queue_drops(&diagnostics));
  draw_text(0, 8, line);
  snprintf(line, sizeof(line), "STACK MIN %u",
           (unsigned)badge_diagnostics_min_stack_high_water(&diagnostics));
  draw_text(0, 16, line);
  snprintf(line, sizeof(line), "IR VALID %lu",
           (unsigned long)diagnostics.ir_valid_frames);
  draw_text(0, 24, line);
  snprintf(line, sizeof(line), "BAD %lu SELF %lu",
           (unsigned long)diagnostics.ir_invalid_frames,
           (unsigned long)diagnostics.ir_self_frames);
  draw_text(0, 32, line);
  snprintf(line, sizeof(line), "DUP %lu CONTACT %u",
           (unsigned long)diagnostics.ir_duplicate_frames,
           (unsigned)state->contact_count);
  draw_text(0, 40, line);
  draw_text(0, 48, "PAGE 4/4");
  draw_text(0, 56, state->last_event);
}

static esp_err_t render_status(void) {
  badge_state_snapshot_t state;
  badge_state_snapshot(&state);
  memset(framebuffer, 0, sizeof(framebuffer));

  switch (state.page) {
    case BADGE_PAGE_PROFILE:
      render_profile_page(&state);
      break;
    case BADGE_PAGE_CONTACTS:
      render_contacts_page(&state);
      break;
    case BADGE_PAGE_IR_STATUS:
      render_ir_page(&state);
      break;
    case BADGE_PAGE_SYSTEM:
    default:
      render_system_page(&state);
      break;
  }

  return flush_display();
}

static esp_err_t connect_display(void) {
  if (i2c_probe(0x3C) == ESP_OK) {
    oled_address = 0x3C;
  } else if (i2c_probe(0x3D) == ESP_OK) {
    oled_address = 0x3D;
  } else {
    return ESP_ERR_NOT_FOUND;
  }

  const i2c_device_config_t device_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = oled_address,
      .scl_speed_hz = 50000,
  };
  esp_err_t result =
      i2c_master_bus_add_device(i2c_bus, &device_config, &oled_device);
  if (result != ESP_OK) {
    oled_device = NULL;
    return result;
  }

  ESP_LOGI(TAG, "OLED found at 0x%02X; initializing at 50 kHz",
           oled_address);
  result = initialize_display();
  if (result == ESP_OK) {
    memset(framebuffer, 0, sizeof(framebuffer));
    result = flush_display();
  }
  if (result != ESP_OK) {
    release_oled_device();
  }
  return result;
}

static void oled_task(void *context) {
  (void)context;
  while (true) {
    if (oled_device == NULL) {
      const esp_err_t result = connect_display();
      if (result == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG,
                 "No ACK at 0x3C or 0x3D; retrying in %d ms",
                 OLED_RETRY_MS);
      } else if (result != ESP_OK) {
        ESP_LOGE(TAG, "OLED connection failed: %s; retrying in %d ms",
                 esp_err_to_name(result), OLED_RETRY_MS);
      }
      if (result != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(OLED_RETRY_MS));
        continue;
      }
    }

    const esp_err_t result = render_status();
    if (result != ESP_OK) {
      ESP_LOGE(TAG, "Display refresh failed: %s; reconnecting",
               esp_err_to_name(result));
      release_oled_device();
      vTaskDelay(pdMS_TO_TICKS(OLED_RETRY_MS));
      continue;
    }
    badge_diagnostics_sample_stack(BADGE_TASK_OLED);
    vTaskDelay(pdMS_TO_TICKS(BADGE_DISPLAY_REFRESH_MS));
  }
}

esp_err_t oled_start(void) {
  const i2c_master_bus_config_t bus_config = {
      .i2c_port = OLED_I2C_PORT,
      .sda_io_num = BADGE_PIN_OLED_SDA,
      .scl_io_num = BADGE_PIN_OLED_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &i2c_bus), TAG,
                      "I2C bus setup failed");

  if (xTaskCreate(oled_task, "oled", 4096, NULL, 3, NULL) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
