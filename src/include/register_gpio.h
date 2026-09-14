#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

static inline void register_gpio_write(gpio_num_t pin, bool high) {
  const uint32_t pin_number = (uint32_t)pin;
  if (pin_number < 32) {
    REG_WRITE(high ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG,
              UINT32_C(1) << pin_number);
    return;
  }

  REG_WRITE(high ? GPIO_OUT1_W1TS_REG : GPIO_OUT1_W1TC_REG,
            UINT32_C(1) << (pin_number - 32));
}

static inline bool register_gpio_read(gpio_num_t pin) {
  const uint32_t pin_number = (uint32_t)pin;
  if (pin_number < 32) {
    return (REG_READ(GPIO_IN_REG) & (UINT32_C(1) << pin_number)) != 0;
  }

  return (REG_READ(GPIO_IN1_REG) &
          (UINT32_C(1) << (pin_number - 32))) != 0;
}
