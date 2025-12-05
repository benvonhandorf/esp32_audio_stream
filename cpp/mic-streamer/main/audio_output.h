/*
 * Audio Output Module - NS4168 I2S Mono Amplifier Driver
 * Handles audio playback through I2S TX
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/i2s_common.h"

// Hardware Configuration - NS4168 I2S Amplifier
#define I2S_TX_BCLK_GPIO        41
#define I2S_TX_SDATA_GPIO       42
#define I2S_TX_LRCLK_GPIO       43

// Audio Output Configuration
#define AUDIO_OUTPUT_SAMPLE_RATE    8000
#define AUDIO_OUTPUT_BIT_WIDTH      8
#define AUDIO_OUTPUT_CHANNELS       1      // Mono

// Playback Buffer Configuration
#define AUDIO_OUTPUT_BUFFER_SIZE    4096   // Bytes per buffer
#define AUDIO_OUTPUT_QUEUE_SIZE     4     // Queue depth

// Audio Output Buffer Structure
typedef struct {
    uint8_t data[AUDIO_OUTPUT_BUFFER_SIZE];
    size_t size;
} audio_output_buffer_t;

// Audio Output Context
typedef struct {
    i2s_chan_handle_t i2s_tx_chan;
    QueueHandle_t output_queue;
    bool is_playing;
    uint32_t bytes_played;
} audio_output_context_t;

// Function declarations
esp_err_t audio_output_init(audio_output_context_t *ctx);

esp_err_t audio_output_chirp_up(audio_output_context_t *ctx);
esp_err_t audio_output_chirp_down(audio_output_context_t *ctx);

esp_err_t audio_output_start(audio_output_context_t *ctx);
esp_err_t audio_output_stop(audio_output_context_t *ctx);
esp_err_t audio_output_queue_data(audio_output_context_t *ctx, const uint8_t *data, size_t size);

void audio_output_task(void *arg);

#ifdef __cplusplus
}
#endif
