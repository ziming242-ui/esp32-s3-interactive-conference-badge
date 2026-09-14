#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef enum {
  BADGE_QUEUE_APP_EVENT = 0,
  BADGE_QUEUE_BUZZER_COMMAND,
  BADGE_QUEUE_IR_COMMAND,
  BADGE_QUEUE_IR_FRAME,
  BADGE_QUEUE_COUNT,
} badge_queue_id_t;

typedef enum {
  BADGE_TASK_APP = 0,
  BADGE_TASK_BUTTONS,
  BADGE_TASK_BUZZER,
  BADGE_TASK_INFRARED,
  BADGE_TASK_OLED,
  BADGE_TASK_COUNT,
} badge_task_id_t;

typedef struct {
  uint32_t queue_drops[BADGE_QUEUE_COUNT];
  UBaseType_t stack_high_water[BADGE_TASK_COUNT];
  uint32_t ir_valid_frames;
  uint32_t ir_invalid_frames;
  uint32_t ir_self_frames;
  uint32_t ir_duplicate_frames;
} badge_diagnostics_snapshot_t;

void badge_diagnostics_init(void);
void badge_diagnostics_queue_drop(badge_queue_id_t queue_id);
void badge_diagnostics_queue_drop_from_isr(badge_queue_id_t queue_id);
void badge_diagnostics_sample_stack(badge_task_id_t task_id);
void badge_diagnostics_ir_valid(void);
void badge_diagnostics_ir_invalid(void);
void badge_diagnostics_ir_self(void);
void badge_diagnostics_ir_duplicate(void);
void badge_diagnostics_snapshot(badge_diagnostics_snapshot_t *snapshot);
void badge_diagnostics_log_snapshot(void);
uint32_t badge_diagnostics_total_queue_drops(
    const badge_diagnostics_snapshot_t *snapshot);
UBaseType_t badge_diagnostics_min_stack_high_water(
    const badge_diagnostics_snapshot_t *snapshot);
