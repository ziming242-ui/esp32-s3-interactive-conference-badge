#include "badge_config.h"

#include "esp_check.h"

#if BADGE_SCOPE_TEST_MODE && BADGE_OLED_BUTTON_TEST_MODE
#error "Enable only one diagnostic mode"
#endif

#if BADGE_SCOPE_TEST_MODE
#include "pin_scope_test.h"
#elif BADGE_OLED_BUTTON_TEST_MODE
#include "oled_button_test.h"
#else
#include "badge_app.h"
#endif

void app_main(void) {
#if BADGE_SCOPE_TEST_MODE
  ESP_ERROR_CHECK(pin_scope_test_start());
#elif BADGE_OLED_BUTTON_TEST_MODE
  ESP_ERROR_CHECK(oled_button_test_start());
#else
  ESP_ERROR_CHECK(badge_app_start());
#endif
}
