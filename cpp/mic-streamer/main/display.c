/*
 * Display Module - ST7789V2 LCD Driver with LVGL UI
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "ff.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "audio_streamer.h"
#include "display.h"

static const char *TAG = "display";

// PWM configuration for backlight
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_8_BIT  // 8-bit resolution (0-255)
#define LEDC_FREQUENCY          10000              // 10kHz PWM frequency

// External function for battery voltage reading
extern float battery_read_voltage(void);

// LVGL objects
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_title_label = NULL;
static lv_obj_t *g_wifi_label = NULL;
static lv_obj_t *g_sd_label = NULL;
static lv_obj_t *g_status_label = NULL;
static lv_obj_t *g_time_label = NULL;
static lv_obj_t *g_size_label = NULL;
static lv_obj_t *g_tcp_label = NULL;
static lv_obj_t *g_server_label = NULL;
static lv_obj_t *g_battery_label = NULL;

// Display handle
static lv_display_t *g_lvgl_disp = NULL;

// ============================================================================
// SD Card Space Query
// ============================================================================

static void update_sd_card_space(app_context_t *ctx)
{
    DWORD free_clusters;
    FATFS *fs;

    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK) {
        ESP_LOGW(TAG, "Failed to get SD card free space: %d", res);
        return;
    }

    // Calculate total and free space
    DWORD total_clusters = fs->n_fatent - 2;

    // Get sector size
    #if FF_MAX_SS != FF_MIN_SS
    DWORD sector_size = fs->ssize;
    #else
    DWORD sector_size = FF_MAX_SS;
    #endif

    // Calculate in bytes
    uint64_t cluster_size = (uint64_t)fs->csize * sector_size;
    ctx->sd_total_bytes = total_clusters * cluster_size;
    ctx->sd_free_bytes = free_clusters * cluster_size;
}

// ============================================================================
// Display Initialization
// ============================================================================

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST7789V2 display with LVGL");

    gpio_config_t backlight_conf = {
        .pin_bit_mask = (1ULL << LCD_BL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&backlight_conf));

    display_set_backlight(true);

    ESP_LOGI(TAG, "Backlight PWM initialized on GPIO %d", LCD_BL_GPIO);

    // Configure SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Configure LCD panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC_GPIO,
        .cs_gpio_num = LCD_CS_GPIO,
        .pclk_hz = 40 * 1000 * 1000, // 40MHz
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &io_handle));

    // Configure LCD panel
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    // Initialize panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Initialize LVGL port
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Add LCD screen
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * LCD_HEIGHT / 10,
        .double_buffer = true,
        //Swapped to deal with swap X/Y
        .hres = LCD_HEIGHT,
        .vres = LCD_WIDTH,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
    };
    g_lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    //Happens after display setup to
    lv_display_set_offset(g_lvgl_disp, LCD_OFFSET_Y, LCD_OFFSET_X);

    // Lock LVGL for UI creation
    lvgl_port_lock(0);

    // Create screen
    g_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);

    // Title label
    g_title_label = lv_label_create(g_screen);
    lv_label_set_text(g_title_label, "ESP32 Audio Streamer");
    lv_obj_set_style_text_color(g_title_label, lv_color_hex(LCD_COLOR_TITLE), 0);
    lv_obj_align(g_title_label, LV_ALIGN_TOP_LEFT, 5, 5);

    // WiFi status label
    g_wifi_label = lv_label_create(g_screen);
    lv_label_set_text(g_wifi_label, "WiFi: Disconnected");
    lv_obj_set_style_text_color(g_wifi_label, lv_color_hex(LCD_COLOR_LABEL), 0);
    lv_obj_align(g_wifi_label, LV_ALIGN_TOP_LEFT, 5, 20);

    // SD card label
    g_sd_label = lv_label_create(g_screen);
    lv_label_set_text(g_sd_label, "SD: Not Available");
    lv_obj_set_style_text_color(g_sd_label, lv_color_hex(LCD_COLOR_LABEL), 0);
    lv_obj_align(g_sd_label, LV_ALIGN_TOP_LEFT, 5, 35);

    // Status label
    g_status_label = lv_label_create(g_screen);
    lv_label_set_text(g_status_label, "Status: Idle");
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(LCD_COLOR_LABEL), 0);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 5, 50);

    // Time label (for recording)
    g_time_label = lv_label_create(g_screen);
    lv_label_set_text(g_time_label, "");
    lv_obj_set_style_text_color(g_time_label, lv_color_hex(LCD_COLOR_LABEL), 0);
    lv_obj_align(g_time_label, LV_ALIGN_TOP_LEFT, 5, 65);

    // Size label (for recording)
    g_size_label = lv_label_create(g_screen);
    lv_label_set_text(g_size_label, "");
    lv_obj_set_style_text_color(g_size_label, lv_color_hex(LCD_COLOR_LABEL), 0);
    lv_obj_align(g_size_label, LV_ALIGN_TOP_LEFT, 5, 80);

    // TCP label
    g_tcp_label = lv_label_create(g_screen);
    lv_label_set_text(g_tcp_label, "");
    lv_obj_set_style_text_color(g_tcp_label, lv_color_hex(LCD_COLOR_LABEL), 0);
    lv_obj_align(g_tcp_label, LV_ALIGN_TOP_LEFT, 5, 95);

    // Server info label
    g_server_label = lv_label_create(g_screen);
    lv_label_set_text(g_server_label, "");
    lv_obj_set_style_text_color(g_server_label, lv_color_hex(LCD_COLOR_LABEL), 0);
    lv_obj_align(g_server_label, LV_ALIGN_TOP_LEFT, 5, 110);

    // Battery voltage label
    g_battery_label = lv_label_create(g_screen);
    lv_label_set_text(g_battery_label, "Battery: --");
    lv_obj_set_style_text_color(g_battery_label, lv_color_hex(LCD_COLOR_LABEL), 0);
    lv_obj_align(g_battery_label, LV_ALIGN_TOP_LEFT, 5, 125);

    // Load screen
    lv_scr_load(g_screen);

    // Unlock LVGL
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;
}

// ============================================================================
// Display Update Function
// ============================================================================

void display_update_status(app_context_t *ctx)
{
    if (g_screen == NULL) return;

    // Lock LVGL
    if (!lvgl_port_lock(10)) {
        return;
    }

    char buf[96];  // Increased buffer size to accommodate longer server addresses
    app_state_t state = get_app_state(ctx);

    // Update WiFi status
    if (ctx->network.wifi_connected) {
        lv_label_set_text(g_wifi_label, "WiFi: #" STR(LCD_COLOR_POSITIVE) " Connected#");
        lv_label_set_recolor(g_wifi_label, true);
    } else {
        lv_label_set_text(g_wifi_label, "WiFi: #" STR(LCD_COLOR_NEGATIVE) " Disconnected#");
        lv_label_set_recolor(g_wifi_label, true);
    }

    // Update SD card status
    if (ctx->sd_card_available) {
        update_sd_card_space(ctx);
        uint32_t free_mb = ctx->sd_free_bytes / (1024 * 1024);
        uint32_t total_mb = ctx->sd_total_bytes / (1024 * 1024);
        snprintf(buf, sizeof(buf), "SD: #%06x %lu/%luMB#", LCD_COLOR_POSITIVE, free_mb, total_mb);
        lv_label_set_text(g_sd_label, buf);
        lv_label_set_recolor(g_sd_label, true);
    } else {
        lv_label_set_text(g_sd_label, "SD: #" STR(LCD_COLOR_NEGATIVE) " Not Available#");
        lv_label_set_recolor(g_sd_label, true);
    }

    // Update recording status
    if (state == APP_STATE_RECORDING) {
        lv_label_set_text(g_status_label, "Status: #ff0000 RECORDING#");
        lv_label_set_recolor(g_status_label, true);

        // Show recording time
        uint32_t duration_sec = ctx->bytes_recorded / (48000 * 2);
        snprintf(buf, sizeof(buf), "Time: %lu:%02lu", duration_sec / 60, duration_sec % 60);
        lv_label_set_text(g_time_label, buf);

        // Show recording size
        uint32_t size_kb = ctx->bytes_recorded / 1024;
        snprintf(buf, sizeof(buf), "Size: %luKB", size_kb);
        lv_label_set_text(g_size_label, buf);

        // Show TCP status
        if (ctx->network.tcp_socket >= 0) {
            lv_label_set_text(g_tcp_label, "TCP: Active");
        } else {
            lv_label_set_text(g_tcp_label, "");
        }
    } else {
        lv_label_set_text(g_status_label, "Status: #00ff00 Idle#");
        lv_label_set_recolor(g_status_label, true);
        lv_label_set_text(g_time_label, "Press BTN to record");
        lv_obj_set_style_text_color(g_time_label, lv_color_hex(LCD_COLOR_CORAL), 0); // Gray
        lv_label_set_text(g_size_label, "");
        lv_label_set_text(g_tcp_label, "");
    }

    // Update server info
    if (ctx->network.config.tcp_enabled && strlen(ctx->network.config.server_addr) > 0) {
        snprintf(buf, sizeof(buf), "%s:%d", ctx->network.config.server_addr, ctx->network.config.server_port);
        lv_label_set_text(g_server_label, buf);
    } else {
        lv_label_set_text(g_server_label, "");
    }

    // Update battery voltage
    if (ctx->battery_voltage > 0.0f) {
        // Color code based on voltage levels
        // Typical Li-ion: 4.2V = full, 3.7V = nominal, 3.3V = low
        uint32_t color;
        if (ctx->battery_voltage >= 3.7f) {
            color = LCD_COLOR_POSITIVE;  // Green/teal for good
        } else if (ctx->battery_voltage >= 3.4f) {
            color = LCD_COLOR_NEUTRAL;   // Yellow for medium
        } else {
            color = LCD_COLOR_NEGATIVE;  // Red/coral for low
        }
        snprintf(buf, sizeof(buf), "Battery: #%06lx %.2fV#", (unsigned long)color, ctx->battery_voltage);
        lv_label_set_text(g_battery_label, buf);
        lv_label_set_recolor(g_battery_label, true);
    } else {
        lv_label_set_text(g_battery_label, "Battery: --");
    }

    // Unlock LVGL
    lvgl_port_unlock();
}

void display_set_backlight(bool on)
{
    gpio_set_level(LCD_BL_GPIO, on ? 1 : 0);
}

// ============================================================================
// Display Task
// ============================================================================

void display_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;

    ESP_LOGI(TAG, "Display task started");

    uint32_t battery_read_counter = 0;

    while (1) {
        // Read battery voltage every 10 cycles (5 seconds)
        if (battery_read_counter % 10 == 0) {
            ctx->battery_voltage = battery_read_voltage();
        }
        battery_read_counter++;

        // Update display
        display_update_status(ctx);

        // Update every 500ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
