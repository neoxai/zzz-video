#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#include <LittleFS.h>
#include <algorithm>
#include <vector>

#include "amoled.h"
#include "pinconfig.h"
#include "video_player.h"

// Mode is chosen by the PlatformIO env (MODE in upload.sh):
//   VIDEO_MODE_STATIC  — loop the one video in LittleFS, no input
//   VIDEO_MODE_DYNAMIC — all videos in LittleFS, swipe left/right to switch
#if defined(VIDEO_MODE_DYNAMIC)
#include "touch.h"
#elif !defined(VIDEO_MODE_STATIC)
#error "Build with -e static or -e dynamic (see upload.sh)"
#endif

// Must match ESP_FPS in web/server.js (the rate clips are exported at)
#define VIDEO_FPS 15
#define BRIGHTNESS 200

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
// Second framebuffer so video can decode one frame while flushing the other
Arduino_Canvas *backBuffer = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel, 0, 0, DISPLAY_ROTATION);
Arduino_Canvas *buffers[2] = {gfx, backBuffer};

// Every .mjpeg at the LittleFS root, sorted by name (the swipe order)
static std::vector<String> listVideos() {
  std::vector<String> videos;
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String path = f.path();
    if (!f.isDirectory() && path.endsWith(".mjpeg")) videos.push_back(path);
  }
  std::sort(videos.begin(), videos.end());
  return videos;
}

#if defined(VIDEO_MODE_DYNAMIC)
#define FADE_STEP 50  // brightness change per step when switching videos

static uint8_t brightness = 0;  // starts dark; the first video fades in
static Swipe swipe = SWIPE_NONE;

// Per-frame hook: fades the video in, and stops playback on a swipe
static bool onFrame() {
  if (brightness < BRIGHTNESS) {
    brightness = min(BRIGHTNESS, brightness + FADE_STEP);
    panel->setBrightness(brightness);
  }
  swipe = takeSwipe();
  return swipe != SWIPE_NONE;
}

// Dims out the last frame before the next video loads
static void fadeOut() {
  while (brightness > 0) {
    brightness = brightness > FADE_STEP ? brightness - FADE_STEP : 0;
    panel->setBrightness(brightness);
    delay(30);
  }
}
#endif

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

  if (!gfx->begin() || !backBuffer->begin(GFX_SKIP_OUTPUT_BEGIN)) {
    Serial.println("display/framebuffer init failed");
    return;
  }
#if defined(VIDEO_MODE_STATIC)
  panel->setBrightness(BRIGHTNESS);
#else
  panel->setBrightness(0);
#endif
  gfx->fillScreen(BLACK);
  gfx->flush();

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed — run ./upload.sh to flash data/");
    return;
  }
  std::vector<String> videos = listVideos();
  if (videos.empty()) {
    Serial.println("No .mjpeg files in LittleFS — run ./upload.sh");
    return;
  }

#if defined(VIDEO_MODE_STATIC)
  playMjpeg(videos[0].c_str(), buffers, VIDEO_FPS, true /* loop */);
#else
  if (!touchBegin(DISPLAY_ROTATION)) {
    Serial.println("Touch controller not responding at 0x38 — swiping disabled");
  }
  size_t current = 0;
  for (;;) {
    Serial.printf("zzz-video: playing %s (%u/%u)\n", videos[current].c_str(),
                  current + 1, videos.size());
    swipe = SWIPE_NONE;
    if (playMjpeg(videos[current].c_str(), buffers, VIDEO_FPS, true /* loop */, onFrame)) {
      fadeOut();
    } else {
      delay(1000);  // unreadable file: skip to the next one, without spinning
    }
    // Swipe left brings in the next video, right the previous (wrapping)
    size_t n = videos.size();
    current = swipe == SWIPE_RIGHT ? (current + n - 1) % n : (current + 1) % n;
  }
#endif
}

void loop() {
  delay(1000);
}
