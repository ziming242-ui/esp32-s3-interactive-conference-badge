#include "badge_diagnostics.h"

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "badge_diagnostics";
static portMUX_TYPE diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static badge_diagnostics_snapshot_t diagnostics;

static uint32_t stack_log_value(UBaseType_t value) {
  return value == UINT_MAX ? 0U : (uint32_t)value;
}

void badge_diagnostics_init(void) {
  portENTER_CRITICAL(&diagnostics_lock);
  memset(&diagnostics, 0, sizeof(diagnostics));
  for (size_t index = 0; index < BADGE_TASK_COUNT; ++index) {
    diagnostics.stack_high_water[index] = UINT_MAX;
  }
  portEXIT_CRITICAL(&diagnostics_lock);
}

void badge_diagnostics_queue_drop(badge_queue_id_t queue_id) {
  if (queue_id >= BADGE_QUEUE_COUNT) {
    return;
  }
  portENTER_CRITICAL(&diagnostics_lock);
  ++diagnostics.queue_drops[queue_id];
  portEXIT_CRITICAL(&diagnostics_lock);
}

void badge_diagnostics_queue_drop_from_isr(badge_queue_id_t queue_id) {
  if (queue_id >= BADGE_QUEUE_COUNT) {
    return;
  }
  portENTER_CRITICAL_ISR(&diagnostics_lock);
  ++diagnostics.queue_drops[queue_id];
  portEXIT_CRITICAL_ISR(&diagnostics_lock);
}

void badge_diagnostics_sample_stack(badge_task_id_t task_id) {
  if (task_id >= BADGE_TASK_COUNT) {
    return;
  }
  const UBaseType_t high_water = uxTaskGetStackHighWaterMark(NULL);
  portENTER_CRITICAL(&diagnostics_lock);
  if (high_water < diagnostics.stack_high_water[task_id]) {
    diagnostics.stack_high_water[task_id] = high_water;
  }
  portEXIT_CRITICAL(&diagnostics_lock);
}

void badge_diagnostics_ir_valid(void) {
  portENTER_CRITICAL(&diagnostics_lock);
  ++diagnostics.ir_valid_frames;
  portEXIT_CRITICAL(&diagnostics_lock);
}

void badge_diagnostics_ir_invalid(void) {
  portENTER_CRITICAL(&diagnostics_lock);
  ++diagnostics.ir_invalid_frames;
  portEXIT_CRITICAL(&diagnostics_lock);
}

void badge_diagnostics_ir_self(void) {
  portENTER_CRITICAL(&diagnostics_lock);
  ++diagnostics.ir_self_frames;
  portEXIT_CRITICAL(&diagnostics_lock);
}

void badge_diagnostics_ir_duplicate(void) {
  portENTER_CRITICAL(&diagnostics_lock);
  ++diagnostics.ir_duplicate_frames;
  portEXIT_CRITICAL(&diagnostics_lock);
}

void badge_diagnostics_snapshot(badge_diagnostics_snapshot_t *snapshot) {
  if (snapshot == NULL) {
    return;
  }
  portENTER_CRITICAL(&diagnostics_lock);
  *snapshot = diagnostics;
  portEXIT_CRITICAL(&diagnostics_lock);
}

void badge_diagnostics_log_snapshot(void) {
  badge_diagnostics_snapshot_t snapshot;
  badge_diagnostics_snapshot(&snapshot);

  ESP_LOGI(TAG,
           "STACK HWM bytes (0=not sampled): app=%" PRIu32
           " buttons=%" PRIu32 " buzzer=%" PRIu32 " infrared=%" PRIu32
           " oled=%" PRIu32,
           stack_log_value(snapshot.stack_high_water[BADGE_TASK_APP]),
           stack_log_value(snapshot.stack_high_water[BADGE_TASK_BUTTONS]),
           stack_log_value(snapshot.stack_high_water[BADGE_TASK_BUZZER]),
           stack_log_value(snapshot.stack_high_water[BADGE_TASK_INFRARED]),
           stack_log_value(snapshot.stack_high_water[BADGE_TASK_OLED]));
  ESP_LOGI(TAG,
           "QUEUE DROPS: app=%" PRIu32 " buzzer=%" PRIu32
           " ir_command=%" PRIu32 " ir_frame=%" PRIu32,
           snapshot.queue_drops[BADGE_QUEUE_APP_EVENT],
           snapshot.queue_drops[BADGE_QUEUE_BUZZER_COMMAND],
           snapshot.queue_drops[BADGE_QUEUE_IR_COMMAND],
           snapshot.queue_drops[BADGE_QUEUE_IR_FRAME]);
}

uint32_t badge_diagnostics_total_queue_drops(
    const badge_diagnostics_snapshot_t *snapshot) {
  if (snapshot == NULL) {
    return 0;
  }
  uint32_t total = 0;
  for (size_t index = 0; index < BADGE_QUEUE_COUNT; ++index) {
    total += snapshot->queue_drops[index];
  }
  return total;
}

UBaseType_t badge_diagnostics_min_stack_high_water(
    const badge_diagnostics_snapshot_t *snapshot) {
  if (snapshot == NULL) {
    return 0;
  }
  UBaseType_t minimum = UINT_MAX;
  for (size_t index = 0; index < BADGE_TASK_COUNT; ++index) {
    if (snapshot->stack_high_water[index] < minimum) {
      minimum = snapshot->stack_high_water[index];
    }
  }
  return minimum == UINT_MAX ? 0 : minimum;
}
