#pragma once

// 正点原子 ESP32-S3 BOX0（240x240 ST7789）
#define LCD_SCLK 39
#define LCD_MOSI 40
#define LCD_DC 38
#define LCD_CS 41
#define LCD_RESET -1
#define LCD_BL 42
#define PANEL_WIDTH 240
#define PANEL_HEIGHT 240

// BOX0 原生 240x240 方屏画布，不做缩放。
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

#define BTN_LEFT 3       // 低有效，内部上拉
#define BTN_MIDDLE 4     // 高有效，内部下拉；兼电源键/唤醒键
#define BTN_RIGHT 0      // 低有效，内部上拉；兼启动模式键
#define POWER_HOLD 2     // 高有效，保持整机供电
#define BATTERY_ADC 1    // ADC1_CH0；未标定，本固件不推算百分比或低电压保护
#define BOARD_TOUCH_FLIP_X 0
#define BOARD_TOUCH_FLIP_Y 0