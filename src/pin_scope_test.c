#include "pin_scope_test.h"

#include <stdbool.h>
#include <stddef.h>

#include "badge_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "register_gpio.h"

#define SCOPE_FREQUENCY_HZ 1000
#define SCOPE_HALF_PERIOD_US (1000000 / SCOPE_FREQUENCY_HZ / 2)
#define SCOPE_BURST_CYCLES 3000
#define SCOPE_MOVE_PROBE_MS 2000
#define IR_RX_INPUT_WINDOW_MS 5000

typedef struct {
  gpio_num_t pin;
  const char *name;
  bool open_drain;
  bool idle_high;
} scope_pin_t;

static const char *TAG = "pin_scope";

static const scope_pin_t scope_pins[] = {
    {.pin = BADGE_PIN_OLED_SDA,
     .name = "OLED SDA",
     .open_drain = true,
     .idle_high = true},
    {.pin = BADGE_PIN_OLED_SCL,
     .name = "OLED SCL",
     .open_drain = true,
     .idle_high = true},
    {.pin = BADGE_PIN_BUTTON_OK,
     .name = "BUTTON OK",
     .open_drain = true,
     .idle_high = true},
    {.pin = BADGE_PIN_BUTTON_DOWN,
     .name = "BUTTON DOWN",
     .open_drain = true,
     .idle_high = true},
    {.pin = BADGE_PIN_BUTTON_UP,
     .name = "BUTTON UP",
     .open_drain = true,
     .idle_high = true},
    {.pin = BADGE_PIN_BUTTON_BACK,
     .name = "BUTTON BACK",
     .open_drain = true,
     .idle_high = true},
    {.pin = BADGE_PIN_BUZZER,
     .name = "BUZZER",
     .open_drain = false,
     .idle_high = true},
    {.pin = BADGE_PIN_IR_TX,
     .name = "IR TX",
     .open_drain = false,
     .idle_high = false},
    {.pin = BADGE_PIN_RGB,
     .name = "RGB DATA",
     .open_drain = false,
     .idle_high = false},
};

static esp_err_t configure_output(const scope_pin_t *test_pin) {
  register_gpio_write(test_pin->pin, test_pin->idle_high);

  const gpio_config_t config = {
      .pin_bit_mask = UINT64_C(1) << test_pin->pin,
      .mode = test_pin->open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT,
      .pull_up_en = test_pin->open_drain ? GPIO_PULLUP_ENABLE
                                         : GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  return gpio_config(&config);
}

static esp_err_t configure_ir_rx_input(void) {
  const gpio_config_t config = {
      .pin_bit_mask = UINT64_C(1) << BADGE_PIN_IR_RX,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  return gpio_config(&config);
}

static void set_all_outputs_idle(void) {
  for (size_t i = 0; i < sizeof(scope_pins) / sizeof(scope_pins[0]); ++i) {
    register_gpio_write(scope_pins[i].pin, scope_pins[i].idle_high);
  }
}

static void output_scope_burst(const scope_pin_t *test_pin) {
  ESP_LOGI(TAG, "START GPIO%d %-12s: %d Hz for 3 seconds",
           (int)test_pin->pin, test_pin->name, SCOPE_FREQUENCY_HZ);

  for (uint32_t cycle = 0; cycle < SCOPE_BURST_CYCLES; ++cycle) {
    register_gpio_write(test_pin->pin, false);
    esp_rom_delay_us(SCOPE_HALF_PERIOD_US);
    register_gpio_write(test_pin->pin, true);
    esp_rom_delay_us(SCOPE_HALF_PERIOD_US);
  }

  register_gpio_write(test_pin->pin, test_pin->idle_high);
  ESP_LOGI(TAG, "STOP  GPIO%d %-12s; next pin in %d ms",
           (int)test_pin->pin, test_pin->name, SCOPE_MOVE_PROBE_MS);
}

static void pin_scope_task(void *context) {
  (void)context;

  ESP_LOGW(TAG, "Oscilloscope test mode is active; normal badge firmware is disabled");
  ESP_LOGI(TAG, "Probe ground -> ESP32 GND; probe tip -> GPIO under test");
  ESP_LOGI(TAG, "GPIO%d IR RX remains INPUT to avoid output contention",
           (int)BADGE_PIN_IR_RX);

  while (true) {
    for (size_t i = 0; i < sizeof(scope_pins) / sizeof(scope_pins[0]); ++i) {
      set_all_outputs_idle();
      vTaskDelay(pdMS_TO_TICKS(SCOPE_MOVE_PROBE_MS));
      output_scope_burst(&scope_pins[i]);
    }

    set_all_outputs_idle();
    ESP_LOGI(TAG,
             "GPIO%d IR RX input window: probe GPIO%d and use an IR remote for %d ms",
             (int)BADGE_PIN_IR_RX, (int)BADGE_PIN_IR_RX,
             IR_RX_INPUT_WINDOW_MS);
    vTaskDelay(pdMS_TO_TICKS(IR_RX_INPUT_WINDOW_MS));
    ESP_LOGI(TAG, "GPIO%d idle sample: %s", (int)BADGE_PIN_IR_RX,
             register_gpio_read(BADGE_PIN_IR_RX) ? "HIGH" : "LOW");
    ESP_LOGI(TAG, "Output test sequence restarting");
  }
}

esp_err_t pin_scope_test_start(void) {
  for (size_t i = 0; i < sizeof(scope_pins) / sizeof(scope_pins[0]); ++i) {
    ESP_RETURN_ON_ERROR(configure_output(&scope_pins[i]), TAG,
                        "GPIO%d configuration failed", (int)scope_pins[i].pin);
  }
  ESP_RETURN_ON_ERROR(configure_ir_rx_input(), TAG,
                      "GPIO%d input configuration failed", BADGE_PIN_IR_RX);
  set_all_outputs_idle();

  if (xTaskCreate(pin_scope_task, "pin_scope", 3072, NULL, 5, NULL) !=
      pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
