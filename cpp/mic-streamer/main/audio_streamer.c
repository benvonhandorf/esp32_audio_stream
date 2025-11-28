/*
 * Audio Streamer Implementation
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "audio_streamer.h"

static const char *TAG = "audio_streamer";

static app_context_t g_app_ctx = {0};
static sdmmc_card_t *g_sd_card = NULL;

// ============================================================================
// Configuration Management
// ============================================================================

esp_err_t config_init(app_context_t *ctx)
{
    ESP_LOGI(TAG, "Initializing configuration");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t nvs_handle;
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Load WiFi SSID
    size_t ssid_len = MAX_SSID_LEN;
    err = nvs_get_str(nvs_handle, "wifi_ssid", ctx->config.wifi_ssid, &ssid_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strcpy(ctx->config.wifi_ssid, "");
        ESP_LOGW(TAG, "WiFi SSID not configured");
    }

    // Load WiFi password
    size_t pass_len = MAX_PASSWORD_LEN;
    err = nvs_get_str(nvs_handle, "wifi_pass", ctx->config.wifi_password, &pass_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strcpy(ctx->config.wifi_password, "");
    }

    // Load server address
    size_t addr_len = MAX_SERVER_ADDR_LEN;
    err = nvs_get_str(nvs_handle, "server_addr", ctx->config.server_addr, &addr_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strcpy(ctx->config.server_addr, "");
        ESP_LOGW(TAG, "Server address not configured");
    }

    // Load server port
    uint16_t port;
    err = nvs_get_u16(nvs_handle, "server_port", &port);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ctx->config.server_port = DEFAULT_SERVER_PORT;
    } else {
        ctx->config.server_port = port;
    }

    // TCP enable flag
    uint8_t tcp_enabled;
    err = nvs_get_u8(nvs_handle, "tcp_enabled", &tcp_enabled);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ctx->config.tcp_enabled = false;
    } else {
        ctx->config.tcp_enabled = tcp_enabled;
    }

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Config: WiFi=%s, Server=%s:%d, TCP=%s",
             ctx->config.wifi_ssid,
             ctx->config.server_addr,
             ctx->config.server_port,
             ctx->config.tcp_enabled ? "enabled" : "disabled");

    return ESP_OK;
}

// ============================================================================
// Button Handler
// ============================================================================

static void IRAM_ATTR button_isr_handler(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    int button_state = gpio_get_level(BUTTON_GPIO);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (button_state == 0) {  // Button pressed (active low)
        app_state_t current_state = ctx->state;
        if (current_state == APP_STATE_IDLE) {
            ctx->state = APP_STATE_STARTING;
        }
    } else {  // Button released
        app_state_t current_state = ctx->state;
        if (current_state == APP_STATE_RECORDING) {
            ctx->state = APP_STATE_STOPPING;
        }
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

esp_err_t button_init(void)
{
    ESP_LOGI(TAG, "Initializing button on GPIO %d", BUTTON_GPIO);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, &g_app_ctx));

    return ESP_OK;
}

// ============================================================================
// I2S PDM Initialization
// ============================================================================

esp_err_t i2s_pdm_init(app_context_t *ctx)
{
    ESP_LOGI(TAG, "Initializing I2S PDM RX at %d Hz", AUDIO_SAMPLE_RATE);

    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    rx_chan_cfg.dma_desc_num = 8;
    rx_chan_cfg.dma_frame_num = 240;

    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &ctx->i2s_rx_chan));

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_MONO
        ),
        .gpio_cfg = {
            .clk = PDM_CLK_GPIO,
            .din = PDM_DATA_GPIO,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(ctx->i2s_rx_chan, &pdm_rx_cfg));
    ESP_LOGI(TAG, "I2S PDM initialized successfully");

    return ESP_OK;
}

// ============================================================================
// SD Card Initialization
// ============================================================================

esp_err_t sd_card_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card in SPI mode");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_GPIO,
        .miso_io_num = SD_MISO_GPIO,
        .sclk_io_num = SD_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.host_id = SPI2_HOST;

    sdmmc_host_t host_config_input = SDSPI_HOST_DEFAULT();
    host_config_input.slot = SPI2_HOST;

    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host_config_input, &slot_config, &mount_config, &g_sd_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    sdmmc_card_print_info(stdout, g_sd_card);
    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);

    return ESP_OK;
}

// ============================================================================
// WiFi Event Handler
// ============================================================================

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    app_context_t *ctx = (app_context_t *)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ctx->wifi_connected = false;
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ctx->wifi_connected = true;
    }
}

esp_err_t wifi_init(app_context_t *ctx)
{
    if (strlen(ctx->config.wifi_ssid) == 0) {
        ESP_LOGW(TAG, "WiFi SSID not configured, skipping WiFi init");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WiFi");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, ctx));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ctx->config.wifi_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, ctx->config.wifi_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization complete");

    return ESP_OK;
}

// ============================================================================
// TCP Client
// ============================================================================

esp_err_t tcp_client_init(app_context_t *ctx)
{
    if (!ctx->config.tcp_enabled || strlen(ctx->config.server_addr) == 0) {
        ESP_LOGI(TAG, "TCP client disabled or server not configured");
        ctx->tcp_socket = -1;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Resolving server address: %s", ctx->config.server_addr);

    // Resolve hostname or IP address using getaddrinfo
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", ctx->config.server_port);

    int err = getaddrinfo(ctx->config.server_addr, port_str, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "Failed to resolve hostname %s: errno %d", ctx->config.server_addr, err);
        return ESP_FAIL;
    }

    // Extract resolved IP address for logging
    struct sockaddr_in *addr_in = (struct sockaddr_in *)res->ai_addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "Resolved %s to %s:%d", ctx->config.server_addr, ip_str, ctx->config.server_port);

    ESP_LOGI(TAG, "Connecting to TCP server %s:%d", ip_str, ctx->config.server_port);

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        freeaddrinfo(res);
        return ESP_FAIL;
    }

    err = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (err != 0) {
        ESP_LOGE(TAG, "Socket connect failed errno=%d", errno);
        close(sock);
        ctx->tcp_socket = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully connected to TCP server");
    ctx->tcp_socket = sock;

    return ESP_OK;
}

// ============================================================================
// State Management
// ============================================================================

void set_app_state(app_context_t *ctx, app_state_t new_state)
{
    xSemaphoreTake(ctx->state_mutex, portMAX_DELAY);
    ctx->state = new_state;
    xSemaphoreGive(ctx->state_mutex);
}

app_state_t get_app_state(app_context_t *ctx)
{
    app_state_t state;
    xSemaphoreTake(ctx->state_mutex, portMAX_DELAY);
    state = ctx->state;
    xSemaphoreGive(ctx->state_mutex);
    return state;
}

// ============================================================================
// Recording Control
// ============================================================================

esp_err_t start_recording(app_context_t *ctx)
{
    ESP_LOGI(TAG, "Starting recording");

    // Reset statistics
    ctx->bytes_recorded = 0;
    ctx->bytes_sent_tcp = 0;
    ctx->bytes_written_sd = 0;

    // Check if SD card is available before attempting to open file
    if (ctx->sd_card_available) {
        // Verify SD card is still accessible
        struct stat st;
        if (stat(SD_MOUNT_POINT, &st) != 0) {
            ESP_LOGE(TAG, "SD card mount point not accessible: %s (errno=%d: %s)",
                     SD_MOUNT_POINT, errno, strerror(errno));
            ctx->sd_card_available = false;
            ctx->sd_file = NULL;
        } else {
            // Create unique filename with timestamp
            time_t now;
            time(&now);
            snprintf(ctx->current_filename, sizeof(ctx->current_filename),
                     "%s/%s%ld%s", SD_MOUNT_POINT, "", (long)now, AUDIO_FILE_SUFFIX);

            // Open SD card file
            ctx->sd_file = fopen(ctx->current_filename, "wb");
            if (ctx->sd_file == NULL) {
                ESP_LOGE(TAG, "Failed to open file for writing: %s (errno=%d: %s)",
                         ctx->current_filename, errno, strerror(errno));
                ESP_LOGW(TAG, "SD card may not be present or filesystem is full");
                ctx->sd_card_available = false;
            } else {
                ESP_LOGI(TAG, "Recording to file: %s", ctx->current_filename);
            }
        }
    } else {
        ESP_LOGW(TAG, "SD card not available, recording will be TCP-only");
        ctx->sd_file = NULL;
    }

    // Connect to TCP server if enabled and WiFi is connected
    if (ctx->config.tcp_enabled && ctx->wifi_connected) {
        tcp_client_init(ctx);
    }

    // Clear audio queue
    xQueueReset(ctx->audio_queue);

    // Enable I2S channel
    ESP_ERROR_CHECK(i2s_channel_enable(ctx->i2s_rx_chan));

    set_app_state(ctx, APP_STATE_RECORDING);
    ESP_LOGI(TAG, "Recording started");

    return ESP_OK;
}

esp_err_t stop_recording(app_context_t *ctx)
{
    ESP_LOGI(TAG, "Stopping recording");

    // Disable I2S channel
    i2s_channel_disable(ctx->i2s_rx_chan);

    // Close SD card file
    if (ctx->sd_file != NULL) {
        fclose(ctx->sd_file);
        ctx->sd_file = NULL;
        ESP_LOGI(TAG, "Closed file: %s (%lu bytes written)",
                 ctx->current_filename, ctx->bytes_written_sd);
    }

    // Close TCP socket
    if (ctx->tcp_socket >= 0) {
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
        ESP_LOGI(TAG, "TCP connection closed (%lu bytes sent)", ctx->bytes_sent_tcp);
    }

    ESP_LOGI(TAG, "Recording stopped. Total recorded: %lu bytes", ctx->bytes_recorded);

    set_app_state(ctx, APP_STATE_IDLE);

    return ESP_OK;
}

// ============================================================================
// Audio Capture Task
// ============================================================================

void audio_capture_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    static uint8_t buffer_index = 0;

    ESP_LOGI(TAG, "Audio capture task started");

    while (1) {
        app_state_t state = get_app_state(ctx);

        if (state == APP_STATE_RECORDING) {
            audio_buffer_t *buf = &ctx->audio_buffers[buffer_index];
            size_t bytes_read = 0;

            esp_err_t ret = i2s_channel_read(ctx->i2s_rx_chan, buf->data,
                                             AUDIO_BUFFER_SIZE, &bytes_read,
                                             portMAX_DELAY);

            if (ret == ESP_OK && bytes_read > 0) {
                buf->size = bytes_read;
                buf->timestamp = xTaskGetTickCount();
                ctx->bytes_recorded += bytes_read;

                // Send buffer to writer task
                if (xQueueSend(ctx->audio_queue, &buf, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Audio queue full, dropping buffer");
                }

                // Move to next buffer
                buffer_index = (buffer_index + 1) % AUDIO_BUFFER_COUNT;
            }
        } else {
            // Not recording, wait a bit
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

// ============================================================================
// Audio Writer Task
// ============================================================================

void audio_writer_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    audio_buffer_t *buf;

    ESP_LOGI(TAG, "Audio writer task started");

    while (1) {
        if (xQueueReceive(ctx->audio_queue, &buf, portMAX_DELAY) == pdTRUE) {
            // Write to SD card
            if (ctx->sd_file != NULL) {
                size_t written = fwrite(buf->data, 1, buf->size, ctx->sd_file);
                if (written == buf->size) {
                    ctx->bytes_written_sd += written;
                } else {
                    ESP_LOGE(TAG, "SD card write error: wrote %d/%d bytes (errno=%d: %s)",
                             written, buf->size, errno, strerror(errno));
                    // Close the file on write error
                    fclose(ctx->sd_file);
                    ctx->sd_file = NULL;
                    ctx->sd_card_available = false;
                }
            }

            // Send to TCP server
            if (ctx->tcp_socket >= 0) {
                int sent = send(ctx->tcp_socket, buf->data, buf->size, 0);
                if (sent < 0) {
                    ESP_LOGE(TAG, "TCP send error, closing connection");
                    close(ctx->tcp_socket);
                    ctx->tcp_socket = -1;
                } else {
                    ctx->bytes_sent_tcp += sent;
                }
            }
        }
    }
}

// ============================================================================
// Main Application
// ============================================================================

void audio_streamer_init(void)
{
    ESP_LOGI(TAG, "Initializing Audio Streamer");

    // Initialize context
    memset(&g_app_ctx, 0, sizeof(app_context_t));
    g_app_ctx.state = APP_STATE_IDLE;
    g_app_ctx.state_mutex = xSemaphoreCreateMutex();
    g_app_ctx.audio_queue = xQueueCreate(AUDIO_QUEUE_SIZE, sizeof(audio_buffer_t *));
    g_app_ctx.tcp_socket = -1;
    g_app_ctx.wifi_connected = false;

    // Initialize components
    ESP_ERROR_CHECK(config_init(&g_app_ctx));
    ESP_ERROR_CHECK(button_init());
    ESP_ERROR_CHECK(i2s_pdm_init(&g_app_ctx));

    esp_err_t ret = sd_card_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card initialization failed, continuing without SD card");
        g_app_ctx.sd_card_available = false;
    } else {
        g_app_ctx.sd_card_available = true;
        ESP_LOGI(TAG, "SD card is available for recording");
    }

    ret = wifi_init(&g_app_ctx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi initialization failed or skipped");
    }

    // Create tasks
    xTaskCreate(audio_capture_task, "audio_capture", 4096, &g_app_ctx, 10, NULL);
    xTaskCreate(audio_writer_task, "audio_writer", 4096, &g_app_ctx, 9, NULL);

    ESP_LOGI(TAG, "Audio Streamer initialized successfully");
}

void audio_streamer_run(void)
{
    ESP_LOGI(TAG, "Audio Streamer running. Press button to start recording.");

    while (1) {
        app_state_t state = get_app_state(&g_app_ctx);

        switch (state) {
            case APP_STATE_STARTING:
                start_recording(&g_app_ctx);
                break;

            case APP_STATE_STOPPING:
                stop_recording(&g_app_ctx);
                break;

            case APP_STATE_IDLE:
            case APP_STATE_RECORDING:
            default:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
