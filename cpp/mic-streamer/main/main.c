/*
 * ESP32S3 Audio Streamer Main Entry Point
 * M5 Cardputer v1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "audio_streamer.h"

#ifdef CONFIG_AUDIO_STREAMER_CONFIG_MODE
extern void config_tool_run(void);
#endif

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32S3 Audio Streamer Starting");
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "Hardware: M5 Cardputer v1.0");
    ESP_LOGI(TAG, "Audio: 48kHz 16-bit Mono PDM");
    ESP_LOGI(TAG, "================================================");

#ifdef CONFIG_AUDIO_STREAMER_CONFIG_MODE
    ESP_LOGI(TAG, "Running in configuration mode");
    config_tool_run();
#else
    audio_streamer_init();
    audio_streamer_run();
#endif
}
