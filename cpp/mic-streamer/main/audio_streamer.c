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
#include "esp_vfs.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "audio_streamer.h"
#include "audio_output.h"
#include "display.h"

static const char *TAG = "audio_streamer";

static app_context_t g_app_ctx = {0};
static sdmmc_card_t *g_sd_card = NULL;

// ADC for battery monitoring
static adc_oneshot_unit_handle_t g_adc_handle = NULL;
static adc_cali_handle_t g_adc_cali_handle = NULL;

// Audio output context
static audio_output_context_t g_audio_output_ctx = {0};

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
        .pin_bit_mask = (1ULL << BUTTON_GPIO)|(1ULL << BUTTON_GPIO_2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, &g_app_ctx));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO_2, button_isr_handler, &g_app_ctx));

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
    //Implicitly sets the frequency to 20 MHz
    //host_config_input.max_freq_khz = SDMMC_FREQ_DEFAULT;

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
                // Set larger buffer for better write performance (64KB)
                setvbuf(ctx->sd_file, NULL, _IOFBF, 65536);
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

    // Allow writer task to drain the queue
    vTaskDelay(pdMS_TO_TICKS(100));

    // Close SD card file
    if (ctx->sd_file != NULL) {
        // Flush and sync before closing to ensure all data is written
        fflush(ctx->sd_file);
        fsync(fileno(ctx->sd_file));
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

                // Send buffer to writer task with small wait time
                // Wait up to 100ms for queue space to prevent drops
                if (xQueueSend(ctx->audio_queue, &buf, pdMS_TO_TICKS(100)) != pdTRUE) {
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
    uint32_t write_count = 0;

    // Throughput tracking
    uint32_t throughput_bytes = 0;
    uint32_t throughput_start_time = 0;

    ESP_LOGI(TAG, "Audio writer task started");

    while (1) {
        if (xQueueReceive(ctx->audio_queue, &buf, portMAX_DELAY) == pdTRUE) {
            // Write to SD card
            if (ctx->sd_file != NULL) {
                uint32_t write_start = xTaskGetTickCount();
                size_t written = fwrite(buf->data, 1, buf->size, ctx->sd_file);

                if (written == buf->size) {
                    ctx->bytes_written_sd += written;
                    write_count++;

                    // Track throughput
                    if (throughput_start_time == 0) {
                        throughput_start_time = write_start;
                    }
                    throughput_bytes += written;

                    // Flush less frequently (every 256 writes = ~1MB) to avoid blocking
                    // The libc buffer and SD card controller will handle intermediate buffering
                    if (write_count >= 256) {
                        uint32_t flush_start = xTaskGetTickCount();
                        fflush(ctx->sd_file);
                        uint32_t flush_end = xTaskGetTickCount();
                        write_count = 0;

                        // Log throughput every flush (approximately every 1MB)
                        uint32_t elapsed_ms = (flush_end - throughput_start_time) * portTICK_PERIOD_MS;
                        if (elapsed_ms > 0) {
                            float throughput_kbps = (throughput_bytes * 8.0f) / elapsed_ms;  // kbit/s
                            float throughput_kBps = (throughput_bytes / 1024.0f) / (elapsed_ms / 1000.0f);  // KB/s
                            uint32_t flush_time_ms = (flush_end - flush_start) * portTICK_PERIOD_MS;
                            ESP_LOGI(TAG, "SD write: %.1f KB/s (%.0f kbps), flush took %lu ms",
                                     throughput_kBps, throughput_kbps, flush_time_ms);
                        }

                        // Reset throughput counters
                        throughput_bytes = 0;
                        throughput_start_time = flush_end;
                    }
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
// Battery Monitoring
// ============================================================================

esp_err_t battery_adc_init(void)
{
    ESP_LOGI(TAG, "Initializing battery ADC");

    // Configure ADC unit
    adc_oneshot_unit_init_cfg_t adc_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_config, &g_adc_handle));

    // Configure ADC channel for GPIO10 (ADC1_CHANNEL_9 on ESP32-S3)
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_12,  // 0-3.3V range
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(g_adc_handle, ADC_CHANNEL_9, &chan_config));

    // Initialize ADC calibration
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_9,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &g_adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration initialized");
    } else {
        ESP_LOGW(TAG, "ADC calibration failed, using raw values");
    }

    return ESP_OK;
}

float battery_read_voltage(void)
{
    int raw_value = 0;
    int voltage_mv = 0;

    // Read raw ADC value
    esp_err_t ret = adc_oneshot_read(g_adc_handle, ADC_CHANNEL_9, &raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return 0.0f;
    }

    // Convert to voltage using calibration if available
    if (g_adc_cali_handle != NULL) {
        ret = adc_cali_raw_to_voltage(g_adc_cali_handle, raw_value, &voltage_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to convert ADC to voltage: %s", esp_err_to_name(ret));
            return 0.0f;
        }
    } else {
        // Fallback: approximate conversion (3.3V / 4096 levels)
        voltage_mv = (raw_value * 3300) / 4096;
    }

    // M5 Cardputer has a voltage divider (typically 2:1 ratio)
    // So multiply by 2 to get actual battery voltage
    float battery_voltage = (voltage_mv / 1000.0f) * 2.0f;

    return battery_voltage;
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
    ESP_ERROR_CHECK(battery_adc_init());

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

    // Initialize display
    ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display initialization failed, continuing without display");
    }

    // Initialize audio output
    ret = audio_output_init(&g_audio_output_ctx);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio output initialization failed, continuing without playback");
    } else {
        ESP_LOGI(TAG, "Audio output initialized successfully");
    }

    // Create tasks with increased stack sizes for SD card operations
    // Writer task has higher priority to prevent queue overflow
    // Display task runs at lower priority (5) to not interfere with audio
    // Audio output task runs at priority 8 for smooth playback
    xTaskCreate(audio_capture_task, "audio_capture", 8192, &g_app_ctx, 9, NULL);
    xTaskCreate(audio_writer_task, "audio_writer", 8192, &g_app_ctx, 10, NULL);
    xTaskCreate(display_task, "display", 4096, &g_app_ctx, 5, NULL);
    // xTaskCreate(audio_output_task, "audio_output", 8192, &g_audio_output_ctx, 8, NULL);

    ESP_LOGI(TAG, "Audio Streamer initialized successfully");
    ESP_LOGI(TAG, "Configuration console available on USB serial port");
}

void audio_streamer_run(void)
{
    ESP_LOGI(TAG, "Audio Streamer running. Press button to start recording.");

    while (1) {
        app_state_t state = get_app_state(&g_app_ctx);

        switch (state) {
            case APP_STATE_STARTING:
                // audio_output_chirp_up(&g_audio_output_ctx);
                start_recording(&g_app_ctx);
                break;

            case APP_STATE_STOPPING:
                stop_recording(&g_app_ctx);
                // audio_output_chirp_down(&g_audio_output_ctx);
                break;

            case APP_STATE_IDLE:
            case APP_STATE_RECORDING:
            default:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
