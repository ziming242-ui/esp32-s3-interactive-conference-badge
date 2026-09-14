#include "modules/infrared.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "badge_config.h"
#include "badge_diagnostics.h"
#include "badge_events.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/task.h"

_Static_assert(BADGE_OWNER_ID > 0 && BADGE_OWNER_ID <= UINT16_MAX,
               "BADGE_OWNER_ID must fit in a non-zero 16-bit ID");
_Static_assert(BADGE_IR_FRAME_BYTES == 5,
               "The badge protocol currently requires five bytes");

typedef enum {
  IR_COMMAND_SEND_MANUAL,
  IR_COMMAND_CYCLE_MODE,
} ir_command_t;

typedef enum {
  RX_WAIT_PREAMBLE_MARK,
  RX_WAIT_PREAMBLE_SPACE,
  RX_WAIT_BIT_MARK,
  RX_WAIT_BIT_SPACE,
} rx_state_t;

typedef struct {
  uint8_t bytes[BADGE_IR_FRAME_BYTES];
} ir_raw_frame_t;

typedef struct {
  int64_t last_edge_us;
  int previous_level;
  rx_state_t state;
  uint8_t bytes[BADGE_IR_FRAME_BYTES];
  uint8_t bit_index;
} ir_decoder_t;

static const char *TAG = "infrared";
static QueueHandle_t app_event_queue;
static QueueHandle_t command_queue;
static QueueHandle_t frame_queue;
static portMUX_TYPE receive_count_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t receive_pulse_count;
static ir_decoder_t decoder;

static bool IRAM_ATTR duration_matches(uint32_t duration_us,
                                       uint32_t target_us) {
  const uint32_t tolerance = target_us / 3;
  return duration_us >= target_us - tolerance &&
         duration_us <= target_us + tolerance;
}

static void IRAM_ATTR reset_decoder(void) {
  decoder.state = RX_WAIT_PREAMBLE_MARK;
  decoder.bit_index = 0;
  memset(decoder.bytes, 0, sizeof(decoder.bytes));
}

static void IRAM_ATTR infrared_receive_isr(void *context) {
  (void)context;
  const int64_t now_us = esp_timer_get_time();
  const int current_level = gpio_get_level(BADGE_PIN_IR_RX);

  portENTER_CRITICAL_ISR(&receive_count_lock);
  ++receive_pulse_count;
  portEXIT_CRITICAL_ISR(&receive_count_lock);

  if (decoder.last_edge_us == 0) {
    decoder.last_edge_us = now_us;
    decoder.previous_level = current_level;
    return;
  }

  const uint32_t duration_us =
      (uint32_t)(now_us - decoder.last_edge_us);
  const int previous_level = decoder.previous_level;
  decoder.last_edge_us = now_us;
  decoder.previous_level = current_level;

  switch (decoder.state) {
    case RX_WAIT_PREAMBLE_MARK:
      if (previous_level == 0 &&
          duration_matches(duration_us, BADGE_IR_PREAMBLE_MARK_US)) {
        decoder.state = RX_WAIT_PREAMBLE_SPACE;
      }
      break;

    case RX_WAIT_PREAMBLE_SPACE:
      if (previous_level == 1 &&
          duration_matches(duration_us, BADGE_IR_PREAMBLE_SPACE_US)) {
        decoder.state = RX_WAIT_BIT_MARK;
        decoder.bit_index = 0;
        memset(decoder.bytes, 0, sizeof(decoder.bytes));
      } else {
        reset_decoder();
      }
      break;

    case RX_WAIT_BIT_MARK:
      if (previous_level == 0 &&
          duration_matches(duration_us, BADGE_IR_BIT_MARK_US)) {
        decoder.state = RX_WAIT_BIT_SPACE;
      } else {
        reset_decoder();
      }
      break;

    case RX_WAIT_BIT_SPACE: {
      if (previous_level != 1) {
        reset_decoder();
        break;
      }

      bool bit_is_one;
      if (duration_matches(duration_us, BADGE_IR_ZERO_SPACE_US)) {
        bit_is_one = false;
      } else if (duration_matches(duration_us, BADGE_IR_ONE_SPACE_US)) {
        bit_is_one = true;
      } else {
        reset_decoder();
        break;
      }

      if (bit_is_one) {
        decoder.bytes[decoder.bit_index / 8] |=
            UINT8_C(1) << (decoder.bit_index % 8);
      }
      ++decoder.bit_index;

      if (decoder.bit_index == BADGE_IR_FRAME_BITS) {
        ir_raw_frame_t frame;
        memcpy(frame.bytes, decoder.bytes, sizeof(frame.bytes));
        BaseType_t higher_priority_task_woken = pdFALSE;
        if (xQueueSendFromISR(frame_queue, &frame,
                              &higher_priority_task_woken) != pdTRUE) {
          badge_diagnostics_queue_drop_from_isr(BADGE_QUEUE_IR_FRAME);
        }
        reset_decoder();
        if (higher_priority_task_woken == pdTRUE) {
          portYIELD_FROM_ISR();
        }
      } else {
        decoder.state = RX_WAIT_BIT_MARK;
      }
      break;
    }
  }
}

static uint8_t crc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) != 0 ? (uint8_t)((crc << 1) ^ 0x07)
                              : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static uint32_t read_receive_count(void) {
  portENTER_CRITICAL(&receive_count_lock);
  const uint32_t count = receive_pulse_count;
  portEXIT_CRITICAL(&receive_count_lock);
  return count;
}

static void carrier_set(bool enabled) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, enabled ? 128 : 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static void send_mark(uint32_t duration_us) {
  carrier_set(true);
  esp_rom_delay_us(duration_us);
  carrier_set(false);
}

static void send_space(uint32_t duration_us) {
  esp_rom_delay_us(duration_us);
}

static void send_frame(uint8_t sequence) {
  const uint16_t badge_id = BADGE_OWNER_ID;
  uint8_t frame[BADGE_IR_FRAME_BYTES] = {
      BADGE_IR_PROTOCOL_VERSION,
      (uint8_t)(badge_id & 0xFF),
      (uint8_t)(badge_id >> 8),
      sequence,
      0,
  };
  frame[4] = crc8(frame, BADGE_IR_FRAME_BYTES - 1);

  send_mark(BADGE_IR_PREAMBLE_MARK_US);
  send_space(BADGE_IR_PREAMBLE_SPACE_US);
  for (uint8_t bit_index = 0; bit_index < BADGE_IR_FRAME_BITS; ++bit_index) {
    const bool bit_is_one =
        (frame[bit_index / 8] & (UINT8_C(1) << (bit_index % 8))) != 0;
    send_mark(BADGE_IR_BIT_MARK_US);
    send_space(bit_is_one ? BADGE_IR_ONE_SPACE_US
                          : BADGE_IR_ZERO_SPACE_US);
  }
  send_mark(BADGE_IR_BIT_MARK_US);
}

static bool post_event(const badge_event_t *event) {
  if (xQueueSend(app_event_queue, event, 0) == pdTRUE) {
    return true;
  }
  badge_diagnostics_queue_drop(BADGE_QUEUE_APP_EVENT);
  return false;
}

static void post_transmit_event(uint32_t count, badge_ir_reason_t reason,
                                uint8_t sequence) {
  const badge_event_t event = {
      .type = BADGE_EVENT_IR_TX,
      .data.ir_tx =
          {
              .count = count,
              .reason = reason,
              .sequence = sequence,
          },
  };
  post_event(&event);
}

static void post_mode_event(badge_mode_t mode) {
  const badge_event_t event = {
      .type = BADGE_EVENT_IR_MODE_CHANGED,
      .data.ir_mode = {.mode = mode},
  };
  post_event(&event);
}

static void process_received_frame(const ir_raw_frame_t *frame) {
  if (frame->bytes[0] != BADGE_IR_PROTOCOL_VERSION ||
      crc8(frame->bytes, BADGE_IR_FRAME_BYTES - 1) != frame->bytes[4]) {
    badge_diagnostics_ir_invalid();
    ESP_LOGW(TAG, "Rejected frame: version or CRC check failed");
    return;
  }

  const uint16_t badge_id =
      (uint16_t)frame->bytes[1] | ((uint16_t)frame->bytes[2] << 8);
  if (badge_id == 0) {
    badge_diagnostics_ir_invalid();
    ESP_LOGW(TAG, "Rejected frame: badge ID is zero");
    return;
  }
  if (badge_id == BADGE_OWNER_ID) {
    badge_diagnostics_ir_self();
#if BADGE_IR_SELF_TEST_MODE
    ESP_LOGI(TAG, "Accepted own ID %u in IR self-test mode",
             (unsigned)badge_id);
#else
    return;
#endif
  }

  badge_diagnostics_ir_valid();
  const badge_event_t event = {
      .type = BADGE_EVENT_IR_CONTACT,
      .data.ir_contact =
          {
              .badge_id = badge_id,
              .sequence = frame->bytes[3],
          },
  };
  post_event(&event);
}

static TickType_t automatic_interval_ticks(uint8_t sequence) {
  const uint32_t range =
      BADGE_IR_AUTO_INTERVAL_MAX_MS - BADGE_IR_AUTO_INTERVAL_MIN_MS + 1;
  const uint32_t offset =
      ((uint32_t)BADGE_OWNER_ID * 997U + (uint32_t)sequence * 421U) % range;
  return pdMS_TO_TICKS(BADGE_IR_AUTO_INTERVAL_MIN_MS + offset);
}

static void transmit(uint32_t *transmit_count, uint8_t *sequence,
                     badge_ir_reason_t reason) {
  ++*sequence;
  send_frame(*sequence);
  ++*transmit_count;
  post_transmit_event(*transmit_count, reason, *sequence);
}

static void infrared_task(void *context) {
  (void)context;
  badge_mode_t mode = BADGE_MODE_AUTO;
  uint32_t transmit_count = 0;
  uint8_t sequence = 0;
  uint32_t last_reported_receive_count = 0;
  TickType_t now = xTaskGetTickCount();
  TickType_t next_auto_tick = now + automatic_interval_ticks(sequence);
  TickType_t last_receive_report_tick = now;
  TickType_t last_stack_sample_tick = now;

  while (true) {
    ir_command_t command;
    if (xQueueReceive(command_queue, &command, pdMS_TO_TICKS(20)) == pdTRUE) {
      if (command == IR_COMMAND_SEND_MANUAL) {
        if (mode != BADGE_MODE_RECEIVE_ONLY) {
          transmit(&transmit_count, &sequence, BADGE_IR_REASON_OK);
          next_auto_tick =
              xTaskGetTickCount() + automatic_interval_ticks(sequence);
        }
      } else {
        mode = (badge_mode_t)((mode + 1) % BADGE_MODE_COUNT);
        next_auto_tick =
            xTaskGetTickCount() + automatic_interval_ticks(sequence);
        post_mode_event(mode);
      }
    }

    ir_raw_frame_t frame;
    while (xQueueReceive(frame_queue, &frame, 0) == pdTRUE) {
      process_received_frame(&frame);
    }

    now = xTaskGetTickCount();
    if (mode == BADGE_MODE_AUTO &&
        (int32_t)(now - next_auto_tick) >= 0) {
      transmit(&transmit_count, &sequence, BADGE_IR_REASON_AUTO);
      now = xTaskGetTickCount();
      next_auto_tick = now + automatic_interval_ticks(sequence);
    }

    if ((now - last_receive_report_tick) >=
        pdMS_TO_TICKS(BADGE_IR_RX_REPORT_MS)) {
      last_receive_report_tick = now;
      const uint32_t count = read_receive_count();
      if (count != last_reported_receive_count) {
        last_reported_receive_count = count;
        const badge_event_t event = {
            .type = BADGE_EVENT_IR_RX,
            .data.ir_rx = {.count = count},
        };
        post_event(&event);
      }
    }

    if ((now - last_stack_sample_tick) >= pdMS_TO_TICKS(1000)) {
      last_stack_sample_tick = now;
      badge_diagnostics_sample_stack(BADGE_TASK_INFRARED);
    }
  }
}

esp_err_t infrared_start(QueueHandle_t event_queue) {
  if (event_queue == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  app_event_queue = event_queue;

  const ledc_timer_config_t timer_config = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = BADGE_IR_CARRIER_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG,
                      "LEDC timer config failed");

  const ledc_channel_config_t channel_config = {
      .gpio_num = BADGE_PIN_IR_TX,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LEDC_TIMER_0,
      .duty = 0,
      .hpoint = 0,
  };
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG,
                      "LEDC channel config failed");

  command_queue = xQueueCreate(4, sizeof(ir_command_t));
  frame_queue = xQueueCreate(8, sizeof(ir_raw_frame_t));
  if (command_queue == NULL || frame_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  memset(&decoder, 0, sizeof(decoder));
  reset_decoder();
  decoder.previous_level = gpio_get_level(BADGE_PIN_IR_RX);

  const gpio_config_t receive_config = {
      .pin_bit_mask = UINT64_C(1) << BADGE_PIN_IR_RX,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_ANYEDGE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&receive_config), TAG,
                      "RX GPIO config failed");

  esp_err_t result = gpio_install_isr_service(0);
  if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
    return result;
  }
  ESP_RETURN_ON_ERROR(
      gpio_isr_handler_add(BADGE_PIN_IR_RX, infrared_receive_isr, NULL), TAG,
      "RX ISR setup failed");

  if (xTaskCreate(infrared_task, "infrared", 4096, NULL, 7, NULL) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool infrared_send_manual(void) {
  const ir_command_t command = IR_COMMAND_SEND_MANUAL;
  if (command_queue != NULL &&
      xQueueSend(command_queue, &command, 0) == pdTRUE) {
    return true;
  }
  badge_diagnostics_queue_drop(BADGE_QUEUE_IR_COMMAND);
  return false;
}

bool infrared_cycle_mode(void) {
  const ir_command_t command = IR_COMMAND_CYCLE_MODE;
  if (command_queue != NULL &&
      xQueueSend(command_queue, &command, 0) == pdTRUE) {
    return true;
  }
  badge_diagnostics_queue_drop(BADGE_QUEUE_IR_COMMAND);
  return false;
}
