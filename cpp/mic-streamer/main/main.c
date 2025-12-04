/*
 * ESP32S3 Audio Streamer Main Entry Point
 * M5 Cardputer v1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "config_tool.h"
#include "audio_streamer.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32S3 Audio Streamer Starting");
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "Hardware: M5 Cardputer v1.0");
    ESP_LOGI(TAG, "Audio: 48kHz 16-bit Mono PDM");
    ESP_LOGI(TAG, "================================================");

        // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    config_tool_start_background();
    audio_streamer_init();
    audio_streamer_run();
}
