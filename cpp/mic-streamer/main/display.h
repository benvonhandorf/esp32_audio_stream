/*
 * Display Module - ST7789V2 LCD Driver with LVGL UI
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "audio_streamer.h"
#include "lvgl.h"

// Display Configuration - ST7789V2
#define LCD_MOSI_GPIO           35
#define LCD_SCK_GPIO            36
#define LCD_CS_GPIO             37
#define LCD_DC_GPIO             34
#define LCD_RST_GPIO            33
#define LCD_BL_GPIO             38
// Physical dimensions: 240×135, but with swap_xy(true) we use swapped dimensions
#define LCD_WIDTH               135
#define LCD_HEIGHT              240
#define LCD_OFFSET_X            53
#define LCD_OFFSET_Y            40

// Function declarations
esp_err_t display_init(void);
void display_task(void *arg);
void display_update_status(app_context_t *ctx);
void display_set_backlight(bool on);

#define LCD_COLOR_RED 0x4C212A
#define LCD_COLOR_GREEN 0xC9CBA3
#define LCD_COLOR_YELLOW 0xFFE1A8
#define LCD_COLOR_CORAL 0xE26D5C
#define LCD_COLOR_TEAL 0x517664

#define LCD_COLOR_TITLE LCD_COLOR_RED
#define LCD_COLOR_LABEL LCD_COLOR_GREEN
#define LCD_COLOR_POSITIVE LCD_COLOR_TEAL
#define LCD_COLOR_NEUTRAL LCD_COLOR_YELLOW
#define LCD_COLOR_NEGATIVE LCD_COLOR_CORAL

// this converts to string
#define STR_(X) #X

// this makes sure the argument is expanded before converting to string
#define STR(X) STR_(X)

#ifdef __cplusplus
}
#endif
