#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#include <LittleFS.h>

#include "amoled.h"
#include "pinconfig.h"
#include "video_player.h"

// Generated from the source .mp4 by upload.sh into data/
#define STARTUP_VIDEO "/s-rank.mjpeg"
#define STARTUP_VIDEO_FPS 30

// TCA9554 registers
#define TCA9554_REG_OUTPUT 0x01
#define TCA9554_REG_CONFIG 0x03

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = AMOLED_PANEL(bus, LCD_WIDTH, LCD_HEIGHT);

// The panel can only mirror, not swap rows/columns, so rotation is done in
// software: draw into a PSRAM framebuffer canvas and flush it to the panel.
// 0 = portrait, 1 = 90° clockwise, 2 = 180°, 3 = 270°.
#define DISPLAY_ROTATION 3
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel, 0, 0, DISPLAY_ROTATION);

static bool tca9554Write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Pulse expander P0..P2 low→high to power up and reset the panel.
static bool resetDisplayViaExpander() {
  if (!tca9554Write(TCA9554_REG_OUTPUT, 0x00)) return false;
  if (!tca9554Write(TCA9554_REG_CONFIG, 0xF8)) return false;  // P0..P2 outputs
  delay(20);
  if (!tca9554Write(TCA9554_REG_OUTPUT, 0x07)) return false;
  delay(50);
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("zzz-video: booting");

  Wire.begin(IIC_SDA, IIC_SCL);
  if (!resetDisplayViaExpander()) {
    Serial.println("TCA9554 not responding at 0x20");
  }

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed");
    return;
  }
  panel->setBrightness(200);
  gfx->fillScreen(BLACK);
  gfx->flush();

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed — run ./upload.sh to flash data/");
    return;
  }
  playMjpeg(STARTUP_VIDEO, gfx, STARTUP_VIDEO_FPS, true /* loop */);
}

void loop() {
  delay(1000);
}
