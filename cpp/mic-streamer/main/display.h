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
#define LCD_OFFSET_X            52
#define LCD_OFFSET_Y            40

// Function declarations
esp_err_t display_init(void);
void display_task(void *arg);
void display_update_status(app_context_t *ctx);

#ifdef __cplusplus
}
#endif
