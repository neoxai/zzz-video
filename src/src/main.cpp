#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#include "amoled.h"
#include "pinconfig.h"

// TCA9554 registers
#define TCA9554_REG_OUTPUT 0x01
#define TCA9554_REG_CONFIG 0x03

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = AMOLED_PANEL(bus, LCD_WIDTH, LCD_HEIGHT);

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
  gfx->setBrightness(200);
  gfx->fillScreen(BLACK);

  const char *msg = "Hello World";
  gfx->setTextSize(4);
  gfx->setTextColor(WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((LCD_WIDTH - w) / 2, (LCD_HEIGHT - h) / 2);
  gfx->println(msg);

  Serial.println("zzz-video: hello world drawn");
}

void loop() {
  delay(1000);
}
