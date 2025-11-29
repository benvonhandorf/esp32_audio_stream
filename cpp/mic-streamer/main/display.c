/*
 * Display Module - ST7789V2 LCD Driver and UI
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "ff.h"
#include "audio_streamer.h"
#include "display.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t g_lcd_panel = NULL;
static uint16_t *g_lcd_buffer = NULL;

// ============================================================================
// Font Data - 5x7 Bitmap Font
// ============================================================================

// Simple 5x7 font (ASCII 32-122: space through 'z')
static const uint8_t font_5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32: space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33: !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34: "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35: #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36: $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37: %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38: &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39: '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40: (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41: )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42: *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43: +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44: ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45: -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46: .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47: /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48: 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49: 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50: 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51: 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52: 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53: 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54: 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55: 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56: 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57: 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58: :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59: ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // 60: <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61: =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 62: >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63: ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64: @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65: A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66: B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67: C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68: D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69: E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70: F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71: G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72: H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73: I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74: J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75: K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76: L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77: M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78: N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79: O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80: P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81: Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82: R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 83: S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84: T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85: U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86: V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87: W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88: X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 89: Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90: Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // 91: [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92: backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // 93: ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94: ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 95: _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // 96: `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // 97: a
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // 98: b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // 99: c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // 100: d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 101: e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // 102: f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 103: g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 104: h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 105: i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // 106: j
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 107: k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 108: l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // 109: m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 110: n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 111: o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // 112: p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // 113: q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 114: r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // 115: s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // 116: t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 117: u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 118: v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 119: w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 120: x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 121: y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 122: z
};

// ============================================================================
// Low-Level Drawing Functions
// ============================================================================

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (g_lcd_buffer == NULL) return;

    for (uint16_t i = 0; i < h; i++) {
        for (uint16_t j = 0; j < w; j++) {
            if ((x + j) < LCD_WIDTH && (y + i) < LCD_HEIGHT) {
                g_lcd_buffer[(y + i) * LCD_WIDTH + (x + j)] = color;
            }
        }
    }
}

static void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color)
{
    if (c < 32 || c > 122) return; // Only support space (32) to 'z' (122)

    const uint8_t *glyph = font_5x7[c - 32];

    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            uint16_t pixel_color = (glyph[col] & (1 << row)) ? color : bg_color;
            uint16_t px = x + col;
            uint16_t py = y + row;
            if (px < LCD_WIDTH && py < LCD_HEIGHT) {
                g_lcd_buffer[py * LCD_WIDTH + px] = pixel_color;
            }
        }
    }
}

static void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color)
{
    uint16_t cur_x = x;
    while (*str) {
        lcd_draw_char(cur_x, y, *str, color, bg_color);
        cur_x += 6; // 5 pixels + 1 space
        str++;
    }
}

static void lcd_update_display(void)
{
    if (g_lcd_panel && g_lcd_buffer) {
        esp_lcd_panel_draw_bitmap(g_lcd_panel, 0, 0, LCD_WIDTH, LCD_HEIGHT, g_lcd_buffer);
    }
}

// ============================================================================
// SD Card Space Query
// ============================================================================

static void update_sd_card_space(app_context_t *ctx)
{
    DWORD free_clusters;
    FATFS *fs;

    // Get free clusters on the SD card
    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res == FR_OK) {
        // Calculate total and free space
        // Total clusters = number of FAT entries - 2 (reserved clusters)
        DWORD total_clusters = fs->n_fatent - 2;

        // Get sector size (default 512 if not variable)
        #if FF_MAX_SS != FF_MIN_SS
        DWORD sector_size = fs->ssize;
        #else
        DWORD sector_size = FF_MAX_SS;
        #endif

        // Calculate bytes: clusters * sectors_per_cluster * bytes_per_sector
        uint64_t cluster_size = (uint64_t)fs->csize * sector_size;
        ctx->sd_total_bytes = total_clusters * cluster_size;
        ctx->sd_free_bytes = free_clusters * cluster_size;
    }
}

// ============================================================================
// Display Initialization
// ============================================================================

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST7789V2 display");

    // Allocate frame buffer
    g_lcd_buffer = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (g_lcd_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LCD buffer");
        return ESP_ERR_NO_MEM;
    }

    // Initialize backlight GPIO
    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = (1ULL << LCD_BL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(LCD_BL_GPIO, 1); // Turn on backlight

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
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &g_lcd_panel));

    // Initialize panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_lcd_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(g_lcd_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(g_lcd_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_lcd_panel, true));

    // Clear screen
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK);
    lcd_update_display();

    ESP_LOGI(TAG, "Display initialized successfully");
    return ESP_OK;
}

// ============================================================================
// Display Task
// ============================================================================

void display_task(void *arg)
{
    app_context_t *ctx = (app_context_t *)arg;
    char line_buf[80];

    ESP_LOGI(TAG, "Display task started");

    while (1) {
        // Clear screen
        lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_BLACK);

        // Title
        lcd_draw_string(5, 5, "ESP32 Audio Streamer", COLOR_CYAN, COLOR_BLACK);

        // WiFi status
        lcd_draw_string(5, 20, "WiFi:", COLOR_WHITE, COLOR_BLACK);
        if (ctx->wifi_connected) {
            lcd_draw_string(45, 20, "Connected", COLOR_GREEN, COLOR_BLACK);
        } else {
            lcd_draw_string(45, 20, "Disconnected", COLOR_RED, COLOR_BLACK);
        }

        // SD Card status
        lcd_draw_string(5, 35, "SD:", COLOR_WHITE, COLOR_BLACK);
        if (ctx->sd_card_available) {
            update_sd_card_space(ctx);
            uint32_t free_mb = ctx->sd_free_bytes / (1024 * 1024);
            uint32_t total_mb = ctx->sd_total_bytes / (1024 * 1024);
            snprintf(line_buf, sizeof(line_buf), "%lu/%luMB", free_mb, total_mb);
            lcd_draw_string(30, 35, line_buf, COLOR_GREEN, COLOR_BLACK);
        } else {
            lcd_draw_string(30, 35, "Not Available", COLOR_RED, COLOR_BLACK);
        }

        // Recording status
        app_state_t state = get_app_state(ctx);
        lcd_draw_string(5, 50, "Status:", COLOR_WHITE, COLOR_BLACK);

        if (state == APP_STATE_RECORDING) {
            lcd_draw_string(50, 50, "RECORDING", COLOR_RED, COLOR_BLACK);

            // Show recording stats
            uint32_t duration_sec = ctx->bytes_recorded / (48000 * 2);
            snprintf(line_buf, sizeof(line_buf), "Time: %lu:%02lu", duration_sec / 60, duration_sec % 60);
            lcd_draw_string(5, 65, line_buf, COLOR_YELLOW, COLOR_BLACK);

            uint32_t size_kb = ctx->bytes_recorded / 1024;
            snprintf(line_buf, sizeof(line_buf), "Size: %luKB", size_kb);
            lcd_draw_string(5, 80, line_buf, COLOR_YELLOW, COLOR_BLACK);

            if (ctx->tcp_socket >= 0) {
                lcd_draw_string(5, 95, "TCP: Active", COLOR_GREEN, COLOR_BLACK);
            }
        } else {
            lcd_draw_string(50, 50, "Idle", COLOR_GREEN, COLOR_BLACK);
            lcd_draw_string(5, 65, "Press BTN to record", COLOR_GRAY, COLOR_BLACK);
        }

        // Server info
        if (ctx->config.tcp_enabled && strlen(ctx->config.server_addr) > 0) {
            snprintf(line_buf, sizeof(line_buf), "Server: %s:%d",
                     ctx->config.server_addr, ctx->config.server_port);
            lcd_draw_string(5, 110, line_buf, COLOR_WHITE, COLOR_BLACK);
        }

        // Update display
        lcd_update_display();

        // Update every 500ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
