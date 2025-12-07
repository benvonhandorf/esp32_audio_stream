/*
 * Audio Output Implementation - NS4168 I2S Mono Amplifier Driver
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "audio_output.h"

#include "../assets/chirp_up.h"
#include "../assets/chirp_down.h"

static const char *TAG = "audio_output";

// ============================================================================
// I2S TX Initialization for NS4168
// ============================================================================

esp_err_t audio_output_init(audio_output_context_t *ctx)
{
    ESP_LOGI(TAG, "Initializing NS4168 I2S audio output");

    if (ctx == NULL)
    {
        ESP_LOGE(TAG, "Context is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Create output queue
    ctx->output_queue = xQueueCreate(AUDIO_OUTPUT_QUEUE_SIZE, sizeof(audio_output_buffer_t *));
    if (ctx->output_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create output queue");
        return ESP_ERR_NO_MEM;
    }

    ctx->is_playing = false;
    ctx->bytes_played = 0;

    // Configure I2S TX channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear DMA buffer on underflow

    esp_err_t ret = i2s_new_channel(&chan_cfg, &ctx->i2s_tx_chan, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create I2S TX channel: %s", esp_err_to_name(ret));
        vQueueDelete(ctx->output_queue);
        return ret;
    }

    // Configure I2S standard mode for NS4168
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_OUTPUT_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_8BIT,
            I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_TX_BCLK_GPIO,
            .ws = I2S_TX_LRCLK_GPIO,
            .dout = I2S_TX_SDATA_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(ctx->i2s_tx_chan, &std_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(ctx->i2s_tx_chan);
        vQueueDelete(ctx->output_queue);
        return ret;
    }

    ESP_LOGI(TAG, "I2S TX initialized successfully");
    ESP_LOGI(TAG, "Sample rate: %d Hz, Bit width: %d, Channels: Mono",
             AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_OUTPUT_BIT_WIDTH);
    ESP_LOGI(TAG, "GPIO - BCLK: %d, LRCLK: %d, SDATA: %d",
             I2S_TX_BCLK_GPIO, I2S_TX_LRCLK_GPIO, I2S_TX_SDATA_GPIO);

    return ESP_OK;
}

// ============================================================================
// Playback Control
// ============================================================================

esp_err_t audio_output_start(audio_output_context_t *ctx)
{
    if (ctx == NULL || ctx->i2s_tx_chan == NULL)
    {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->is_playing)
    {
        ESP_LOGW(TAG, "Already playing");
        return ESP_OK;
    }

    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = I2S_TX_BCLK_GPIO,
        .ws = I2S_TX_LRCLK_GPIO,
        .dout = I2S_TX_SDATA_GPIO,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        }
    };

    ESP_ERROR_CHECK(i2s_channel_reconfig_std_gpio(ctx->i2s_tx_chan, &gpio_cfg));

    esp_err_t ret = i2s_channel_enable(ctx->i2s_tx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ctx->is_playing = true;
    ctx->bytes_played = 0;
    ESP_LOGI(TAG, "Audio playback started");

    return ESP_OK;
}

esp_err_t audio_output_stop(audio_output_context_t *ctx)
{
    if (ctx == NULL || ctx->i2s_tx_chan == NULL)
    {
        ESP_LOGE(TAG, "Invalid context");
        return ESP_ERR_INVALID_STATE;
    }

    if (!ctx->is_playing)
    {
        ESP_LOGW(TAG, "Not playing");
        return ESP_OK;
    }

    esp_err_t ret = i2s_channel_disable(ctx->i2s_tx_chan);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to disable I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ctx->is_playing = false;
    ESP_LOGI(TAG, "Audio playback stopped. Total bytes played: %lu", ctx->bytes_played);

    // Clear any remaining buffers from the queue
    audio_output_buffer_t *buf;
    while (xQueueReceive(ctx->output_queue, &buf, 0) == pdTRUE)
    {
        // Just drain the queue
    }

    return ESP_OK;
}

// ============================================================================
// Data Queue Management
// ============================================================================

esp_err_t audio_output_queue_data(audio_output_context_t *ctx, const uint8_t *data, size_t size)
{
    if (ctx == NULL || data == NULL || size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (size > AUDIO_OUTPUT_BUFFER_SIZE)
    {
        ESP_LOGE(TAG, "Data size %d exceeds buffer size %d", size, AUDIO_OUTPUT_BUFFER_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    // Allocate a buffer
    audio_output_buffer_t *buf = malloc(sizeof(audio_output_buffer_t));
    if (buf == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        return ESP_ERR_NO_MEM;
    }

    // Copy data
    memcpy(buf->data, data, size);
    buf->size = size;

    // Queue the buffer
    if (xQueueSend(ctx->output_queue, &buf, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Output queue full, dropping buffer");
        free(buf);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

// Audio playback

esp_err_t audio_output_chirp_up(audio_output_context_t *ctx)
{
    audio_output_start(ctx);

    esp_err_t res = audio_output_queue_data(ctx, chirp_up_raw, chirp_up_raw_len);

    return res;
}

esp_err_t audio_output_chirp_down(audio_output_context_t *ctx)
{
    audio_output_start(ctx);

    esp_err_t res = audio_output_queue_data(ctx, chirp_down_raw, chirp_down_raw_len);

    return res;
}

esp_err_t audio_output_status(audio_output_context_t *ctx, bool *playing)
{
    *playing = ctx->is_playing;

    return ESP_OK;
}

// ============================================================================
// Audio Output Task
// ============================================================================

void audio_output_task(void *arg)
{
    audio_output_context_t *ctx = (audio_output_context_t *)arg;
    audio_output_buffer_t *buf = NULL;
    size_t bytes_written = 0;

    ESP_LOGI(TAG, "Audio output task started");

    while (1)
    {
        // Wait for data in the queue
        if (xQueueReceive(ctx->output_queue, &buf, portMAX_DELAY) == pdTRUE)
        {
            if (ctx->is_playing && buf != NULL)
            {
                // Write data to I2S
                esp_err_t ret = i2s_channel_write(
                    ctx->i2s_tx_chan,
                    buf->data,
                    buf->size,
                    &bytes_written,
                    portMAX_DELAY);

                if (ret == ESP_OK)
                {
                    ctx->bytes_played += bytes_written;
                }
                else
                {
                    ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
                }

                if(xQueuePeek(ctx->output_queue, &buf, 0) == errQUEUE_EMPTY) {
                    audio_output_stop(ctx);
                }
            }

            // Free the buffer
            if (buf != NULL)
            {
                free(buf);
                buf = NULL;
            }
        }
    }
}
