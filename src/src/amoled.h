#pragma once

#include <Arduino_GFX_Library.h>

// The ESP32-S3-Touch-AMOLED-1.8 shipped with two panel controllers:
//   v1: SH8601, no column offset
//   v2: CO5300, 16px column offset (current stock)
// Both speak the same MIPI-style command set, so v1 reuses the upstream
// CO5300 driver with Waveshare's SH8601 init sequence swapped in.
// Build with -DAMOLED_V1 for the SH8601 board.

#ifdef AMOLED_V1

static const uint8_t sh8601_init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,  // Sleep out
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x13,  // Normal display mode on
    WRITE_COMMAND_8, 0x20,  // Inversion off
    WRITE_C8_D8, 0x3A, 0x05,  // 16 bit/pixel
    WRITE_COMMAND_8, 0x29,  // Display on
    WRITE_C8_D8, 0x53, 0x28,  // Brightness control + dimming on
    WRITE_C8_D8, 0x51, 0xD0,  // Brightness
    WRITE_C8_D8, 0x58, 0x00,  // High contrast mode off
    END_WRITE,
    DELAY, 10};

class Arduino_SH8601 : public Arduino_CO5300 {
 public:
  Arduino_SH8601(Arduino_DataBus *bus, int16_t w, int16_t h)
      : Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, false, w, h) {}

 protected:
  void tftInit() override {
    _bus->sendCommand(0x01);  // Software reset
    delay(200);
    _bus->batchOperation(sh8601_init_operations, sizeof(sh8601_init_operations));
  }
};

#define AMOLED_PANEL(bus, w, h) new Arduino_SH8601(bus, w, h)

#else

#define AMOLED_PANEL(bus, w, h) \
  new Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, false, w, h, 16, 0, 0, 0)

#endif
