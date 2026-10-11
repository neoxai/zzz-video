#include "touch.h"

#include <Wire.h>

#include "pinconfig.h"

// Shared register map: 0x02 finger count, then X1 high/low, Y1 high/low
#define REG_FINGERS 0x02
#ifdef AMOLED_V1
#define REG_CHIP_ID 0xA0  // FT3168
#else
#define REG_CHIP_ID 0xA7          // CST816
#define CST816_REG_NO_SLEEP 0xFE  // non-zero disables auto-sleep
#endif

#define POLL_MS 15
#define SWIPE_MIN_PX 60  // travel needed to count as a swipe

static uint8_t rotation;
static volatile Swipe pending = SWIPE_NONE;

static bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TOUCH_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

// Reads the first touch point in panel coordinates. Returns false if no finger.
static bool readPoint(int16_t *x, int16_t *y) {
  uint8_t b[5];
  if (!readRegs(REG_FINGERS, b, sizeof(b)) || (b[0] & 0x0F) == 0) return false;
  *x = ((b[1] & 0x0F) << 8) | b[2];
  *y = ((b[3] & 0x0F) << 8) | b[4];
  return true;
}

// Converts a movement in panel coordinates into on-screen coordinates,
// mirroring how Arduino_Canvas maps each rotation onto the panel.
static void toScreen(int16_t dpx, int16_t dpy, int16_t *dx, int16_t *dy) {
  switch (rotation) {
    case 1: *dx = dpy;  *dy = -dpx; break;
    case 2: *dx = -dpx; *dy = -dpy; break;
    case 3: *dx = -dpy; *dy = dpx;  break;
    default: *dx = dpx; *dy = dpy;
  }
}

static void touchTask(void *) {
  bool down = false;
  int16_t startX = 0, startY = 0, lastX = 0, lastY = 0;
  for (;;) {
    int16_t x, y;
    if (readPoint(&x, &y)) {
      if (!down) {
        down = true;
        startX = x;
        startY = y;
      }
      lastX = x;
      lastY = y;
    } else if (down) {
      // Finger lifted: a drag far enough along its dominant axis is a swipe.
      // Up counts as left and down as right.
      down = false;
      int16_t dx, dy;
      toScreen(lastX - startX, lastY - startY, &dx, &dy);
      int16_t travel = abs(dx) >= abs(dy) ? dx : dy;
      bool swiped = abs(travel) >= SWIPE_MIN_PX;
      if (swiped) pending = travel < 0 ? SWIPE_LEFT : SWIPE_RIGHT;
      const char *dir = abs(dx) >= abs(dy) ? (dx < 0 ? "left" : "right") : (dy < 0 ? "up" : "down");
      Serial.printf("touch: dx=%d dy=%d -> %s%s\n", dx, dy, swiped ? "swipe " : "no swipe",
                    swiped ? dir : "");
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

bool touchBegin(uint8_t r) {
  rotation = r;
  uint8_t id;
  bool found = readRegs(REG_CHIP_ID, &id, 1);
  if (found) Serial.printf("touch: chip id 0x%02X at 0x%02X\n", id, TOUCH_ADDR);
#ifdef CST816_REG_NO_SLEEP
  // The CST816 drops off the bus in auto-sleep until touched; keep it awake
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(CST816_REG_NO_SLEEP);
  Wire.write(0x01);
  Wire.endTransmission();
#endif
  xTaskCreatePinnedToCore(touchTask, "touch", 4096, nullptr, 1, nullptr, 1);
  return found;
}

Swipe takeSwipe() {
  Swipe s = pending;
  pending = SWIPE_NONE;
  return s;
}
