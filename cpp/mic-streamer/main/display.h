/*
 * Display Module - ST7789V2 LCD Driver and UI
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "audio_streamer.h"

// Display Configuration - ST7789V2
#define LCD_MOSI_GPIO           35
#define LCD_SCK_GPIO            36
#define LCD_CS_GPIO             37
#define LCD_DC_GPIO             34
#define LCD_RST_GPIO            33
#define LCD_BL_GPIO             38
#define LCD_WIDTH               240
#define LCD_HEIGHT              135

// RGB565 Color Definitions
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x8410

// Function declarations
esp_err_t display_init(void);
void display_task(void *arg);

#ifdef __cplusplus
}
#endif
