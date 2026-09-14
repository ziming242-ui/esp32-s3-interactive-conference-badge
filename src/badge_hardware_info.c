#include "badge_hardware_info.h"

#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "sdkconfig.h"

static const char *TAG = "badge_hardware";

void badge_hardware_info_log(void) {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  uint32_t flash_bytes = 0;
  if (esp_flash_get_size(NULL, &flash_bytes) != ESP_OK) {
    ESP_LOGW(TAG, "Unable to read flash size");
  }

  size_t psram_bytes = 0;
#if CONFIG_SPIRAM
  psram_bytes = esp_psram_get_size();
#endif

  ESP_LOGI(TAG, "Board profile: ESP32-S3-WROOM-1-N16R8");
  ESP_LOGI(TAG, "Detected: %d cores, chip revision %d", chip_info.cores,
           chip_info.revision);
  ESP_LOGI(TAG, "Memory: flash=%" PRIu32 " MB, PSRAM=%u MB",
           flash_bytes / (1024U * 1024U),
           (unsigned)(psram_bytes / (1024U * 1024U)));
  ESP_LOGI(TAG, "PSRAM heap: total=%u bytes, free=%u bytes",
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  if (flash_bytes != 16U * 1024U * 1024U) {
    ESP_LOGW(TAG, "Expected 16 MB flash for N16R8");
  }
  if (psram_bytes != 8U * 1024U * 1024U) {
    ESP_LOGW(TAG, "Expected 8 MB PSRAM for N16R8");
  }
}
