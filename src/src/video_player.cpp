#include "video_player.h"

#include <JPEGDEC.h>
#include <LittleFS.h>

static JPEGDEC jpeg;
static Arduino_Canvas *target;

static int drawBlock(JPEGDRAW *draw) {
  target->draw16bitRGBBitmap(draw->x, draw->y, draw->pPixels, draw->iWidth, draw->iHeight);
  return 1;
}

// Loads the whole file into PSRAM so frame decoding never waits on flash.
static uint8_t *loadFile(const char *path, size_t *size) {
  File f = LittleFS.open(path, "r");
  if (!f) return nullptr;
  *size = f.size();
  uint8_t *buf = (uint8_t *)ps_malloc(*size);
  if (buf && f.read(buf, *size) != *size) {
    free(buf);
    buf = nullptr;
  }
  f.close();
  return buf;
}

// Every frame starts with a JPEG SOI marker (FF D8 FF). FF bytes inside
// entropy-coded data are always stuffed (FF 00), so this can't false-match.
static size_t nextFrameStart(const uint8_t *buf, size_t size, size_t from) {
  for (size_t i = from; i + 2 < size; i++) {
    if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF) return i;
  }
  return size;
}

static void logPass(uint32_t frames, uint32_t elapsed) {
  Serial.printf("video: %u frames in %u ms (%.1f fps)\n", frames, elapsed,
                elapsed ? frames * 1000.0f / elapsed : 0.0f);
}

bool playMjpeg(const char *path, Arduino_Canvas *canvas, uint8_t fps, bool loop) {
  size_t size;
  uint8_t *buf = loadFile(path, &size);
  if (!buf) {
    Serial.printf("video: failed to load %s\n", path);
    return false;
  }

  target = canvas;
  const uint32_t frameMs = 1000 / fps;
  uint32_t frames = 0;
  uint32_t started = millis();
  uint32_t nextDue = started;

  const size_t first = nextFrameStart(buf, size, 0);
  size_t start = first;
  while (start < size) {
    size_t end = nextFrameStart(buf, size, start + 3);

    if (jpeg.openRAM(buf + start, end - start, drawBlock)) {
      jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
      int x = (canvas->width() - jpeg.getWidth()) / 2;
      int y = (canvas->height() - jpeg.getHeight()) / 2;
      jpeg.decode(x, y, 0);
      jpeg.close();
      canvas->flush();
      frames++;
    }

    nextDue += frameMs;
    int32_t wait = (int32_t)(nextDue - millis());
    if (wait > 0) delay(wait);
    start = end;

    if (start < size) continue;  // more frames in this pass
    // End of pass. frames == 0 means nothing decoded — don't spin forever.
    if (!loop || frames == 0) break;
    logPass(frames, millis() - started);
    frames = 0;
    started = millis();
    start = first;
  }

  logPass(frames, millis() - started);
  free(buf);
  return true;
}
