#include "modules/rgb.h"

#include "badge_config.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"

#define RGB_RMT_RESOLUTION_HZ 10000000

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} rgb_color_t;

static const rgb_color_t palette[] = {
    {.red = 0, .green = 0, .blue = 255},
    {.red = 0, .green = 255, .blue = 0},
    {.red = 255, .green = 0, .blue = 0},
    {.red = 255, .green = 255, .blue = 0},
    {.red = 128, .green = 0, .blue = 128},
    {.red = 255, .green = 255, .blue = 255},
};

static rmt_channel_handle_t tx_channel;
static rmt_encoder_handle_t bytes_encoder;
static uint8_t current_palette_index;

static uint8_t palette_count(void) {
  return (uint8_t)(sizeof(palette) / sizeof(palette[0]));
}

static esp_err_t show_current_color(void) {
  const rgb_color_t color = palette[current_palette_index];
  const uint8_t grb[] = {color.green, color.red, color.blue};
  const rmt_transmit_config_t transmit_config = {.loop_count = 0};

  ESP_RETURN_ON_ERROR(rmt_transmit(tx_channel, bytes_encoder, grb, sizeof(grb),
                                   &transmit_config),
                      "rgb", "RMT transmit failed");
  ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(tx_channel, pdMS_TO_TICKS(20)),
                      "rgb", "RMT wait failed");
  esp_rom_delay_us(80);
  return ESP_OK;
}

esp_err_t rgb_init(void) {
  const rmt_tx_channel_config_t channel_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .gpio_num = BADGE_PIN_RGB,
      .mem_block_symbols = 64,
      .resolution_hz = RGB_RMT_RESOLUTION_HZ,
      .trans_queue_depth = 4,
  };
  ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&channel_config, &tx_channel), "rgb",
                      "RMT channel setup failed");

  const rmt_bytes_encoder_config_t encoder_config = {
      .bit0 =
          {
              .duration0 = 3,
              .level0 = 1,
              .duration1 = 9,
              .level1 = 0,
          },
      .bit1 =
          {
              .duration0 = 9,
              .level0 = 1,
              .duration1 = 3,
              .level1 = 0,
          },
      .flags.msb_first = 1,
  };
  ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&encoder_config, &bytes_encoder),
                      "rgb", "RMT encoder setup failed");
  ESP_RETURN_ON_ERROR(rmt_enable(tx_channel), "rgb",
                      "RMT channel enable failed");

  current_palette_index = BADGE_OWNER_ROLE_COLOR_INDEX % palette_count();
  return show_current_color();
}

esp_err_t rgb_set_palette(uint8_t palette_index) {
  if (palette_index >= palette_count()) {
    return ESP_ERR_INVALID_ARG;
  }
  current_palette_index = palette_index;
  return show_current_color();
}

uint8_t rgb_next(void) {
  current_palette_index = (current_palette_index + 1) % palette_count();
  show_current_color();
  return current_palette_index;
}

uint8_t rgb_previous(void) {
  const uint8_t count = palette_count();
  current_palette_index = (current_palette_index + count - 1) % count;
  show_current_color();
  return current_palette_index;
}

uint8_t rgb_palette_index(void) { return current_palette_index; }
