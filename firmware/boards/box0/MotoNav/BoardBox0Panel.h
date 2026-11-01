#pragma once
#include "Arduino_GFX_Library.h"

// BOX0 的 ST7789 使用 SPI mode 0；GFX 的 Arduino_ST7789 在 ESP32 上默认 mode 3。
class BoardBox0Panel : public Arduino_ST7789 {
public:
  explicit BoardBox0Panel(Arduino_DataBus *bus)
      : Arduino_ST7789(bus, LCD_RESET, 0, true, PANEL_WIDTH, PANEL_HEIGHT, 0, 0) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    _override_datamode = SPI_MODE0;
    return Arduino_TFT::begin(speed);
  }
};