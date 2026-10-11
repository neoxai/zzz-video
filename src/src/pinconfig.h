#pragma once

// Waveshare ESP32-S3-Touch-AMOLED-1.8

// AMOLED (SH8601, QSPI)
#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK  11
#define LCD_CS    12
#define LCD_WIDTH  368
#define LCD_HEIGHT 448

// Shared I2C bus (TCA9554 expander, FT3168 touch, AXP2101 PMU)
#define IIC_SDA 15
#define IIC_SCL 14

// Capacitive touch controller — differs by board revision (see amoled.h).
// Both share the FocalTech-style point registers (0x02..0x06).
#ifdef AMOLED_V1
#define TOUCH_ADDR 0x38  // FT3168
#else
#define TOUCH_ADDR 0x15  // CST816
#endif

// TCA9554 IO expander — P0..P2 drive the display/touch power and reset lines
#define TCA9554_ADDR 0x20
