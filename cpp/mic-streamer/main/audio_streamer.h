/*
 * Audio Streamer - ESP32S3 Audio Recording and Streaming Application
 * Records audio from PDM microphone, streams to TCP server, and saves to SD card
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2s_pdm.h"
#include "esp_err.h"
#include "network.h"

// Hardware Configuration - M5 Cardputer v1.0
#define BUTTON_GPIO             0
#define BUTTON_GPIO_2           2
#define PDM_CLK_GPIO            43
#define PDM_DATA_GPIO           46
#define SD_CLK_GPIO             40
#define SD_MISO_GPIO            39
#define SD_MOSI_GPIO            14
#define SD_CS_GPIO              12
#define BATTERY_ADC_GPIO        10

// Audio Configuration
#define AUDIO_SAMPLE_RATE       48000
#define AUDIO_BIT_WIDTH         16
#define AUDIO_CHANNELS          1  // Mono

// Buffer Configuration
#define AUDIO_BUFFER_SIZE       4096   // Bytes per I2S read
#define AUDIO_BUFFER_COUNT      16      // Number of buffers in ring buffer
#define AUDIO_QUEUE_SIZE        32     // Queue depth for audio buffers

// SD Card Configuration
#define SD_MOUNT_POINT          "/sdcard"
#define AUDIO_FILE_PREFIX       "audio_"
#define AUDIO_FILE_SUFFIX       ".raw"

// Application State
typedef enum {
    APP_STATE_IDLE,
    APP_STATE_STARTING,
    APP_STATE_RECORDING,
    APP_STATE_STOPPING
} app_state_t;

// Audio Buffer Structure
typedef struct {
    uint8_t data[AUDIO_BUFFER_SIZE];
    size_t size;
    uint32_t timestamp;
} audio_buffer_t;

// Application Context
typedef struct {
    app_state_t state;
    SemaphoreHandle_t state_mutex;

    // I2S
    i2s_chan_handle_t i2s_rx_chan;

    // Audio buffers
    QueueHandle_t audio_queue;
    audio_buffer_t audio_buffers[AUDIO_BUFFER_COUNT];

    // SD Card
    FILE *sd_file;
    char current_filename[64];
    bool sd_card_available;

    // Network
    network_context_t network;

    // Statistics
    uint32_t bytes_recorded;
    uint32_t bytes_sent_tcp;
    uint32_t bytes_written_sd;

    // Display
    uint64_t sd_total_bytes;
    uint64_t sd_free_bytes;

    // Battery monitoring
    float battery_voltage;
} app_context_t;

// Function declarations
void audio_streamer_init(void);
void audio_streamer_run(void);

// Component initialization
esp_err_t config_init(app_context_t *ctx);
esp_err_t button_init(void);
esp_err_t i2s_pdm_init(app_context_t *ctx);
esp_err_t sd_card_init(void);

// State management
void set_app_state(app_context_t *ctx, app_state_t new_state);
app_state_t get_app_state(app_context_t *ctx);

// Recording control
esp_err_t start_recording(app_context_t *ctx);
esp_err_t stop_recording(app_context_t *ctx);

// Tasks
void audio_capture_task(void *arg);
void audio_writer_task(void *arg);

#ifdef __cplusplus
}
#endif
