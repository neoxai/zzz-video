#include "video_player.h"

#include <JPEGDEC.h>
#include <LittleFS.h>
#include <vector>

#define FRAME_END 0xFF  // slot value telling the flusher playback is over

static JPEGDEC jpeg;
static Arduino_Canvas *target;  // canvas the decoder is drawing into

struct Frame {
  const uint8_t *data;
  size_t len;
};

struct Ready {
  uint8_t slot;       // canvas index, or FRAME_END
  uint32_t index;     // frame number within the file
  uint32_t decodeUs;
};

struct Player {
  std::vector<Frame> frames;
  Arduino_Canvas **buffers;
  bool loop;
  volatile bool stop;        // set by the flusher to end playback early
  QueueHandle_t freeSlots;   // canvases the decoder may draw into
  QueueHandle_t readySlots;  // decoded canvases waiting to be flushed
};

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
static std::vector<Frame> splitFrames(const uint8_t *buf, size_t size) {
  std::vector<Frame> frames;
  size_t start = size;
  for (size_t i = 0; i + 2 < size; i++) {
    if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF) {
      if (start < size) frames.push_back({buf + start, i - start});
      start = i;
    }
  }
  if (start < size) frames.push_back({buf + start, size - start});
  return frames;
}

static void decodeTask(void *arg) {
  Player *p = (Player *)arg;
  uint32_t index = 0;
  for (;;) {
    Ready r = {0, index, 0};
    xQueueReceive(p->freeSlots, &r.slot, portMAX_DELAY);
    if (p->stop) break;

    uint32_t t = micros();
    const Frame &f = p->frames[index];
    target = p->buffers[r.slot];
    if (jpeg.openRAM((uint8_t *)f.data, f.len, drawBlock)) {
      jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
      int x = (target->width() - jpeg.getWidth()) / 2;
      int y = (target->height() - jpeg.getHeight()) / 2;
      jpeg.decode(x, y, 0);
      jpeg.close();
    }
    r.decodeUs = micros() - t;
    xQueueSend(p->readySlots, &r, portMAX_DELAY);
    // If decoding is the bottleneck this loop never blocks; yield a tick so
    // core 0's idle task runs and the task watchdog stays fed.
    vTaskDelay(1);

    if (++index < p->frames.size()) continue;
    if (!p->loop) break;
    index = 0;
  }
  Ready end = {FRAME_END, 0, 0};
  xQueueSend(p->readySlots, &end, portMAX_DELAY);
  vTaskDelete(nullptr);
}

bool playMjpeg(const char *path, Arduino_Canvas *buffers[2], uint8_t fps, bool loop,
               FrameHook onFrame) {
  size_t size;
  uint8_t *buf = loadFile(path, &size);
  if (!buf) {
    Serial.printf("video: failed to load %s\n", path);
    return false;
  }

  Player p;
  p.frames = splitFrames(buf, size);
  p.buffers = buffers;
  p.loop = loop;
  p.stop = false;
  if (p.frames.empty()) {
    Serial.printf("video: no frames in %s\n", path);
    free(buf);
    return false;
  }
  p.freeSlots = xQueueCreate(2, sizeof(uint8_t));
  p.readySlots = xQueueCreate(2, sizeof(Ready));
  for (uint8_t slot = 0; slot < 2; slot++) xQueueSend(p.freeSlots, &slot, 0);
  xTaskCreatePinnedToCore(decodeTask, "mjpeg", 8192, &p, 1, nullptr, 0);

  const uint32_t frameUs = 1000000 / fps;
  uint32_t nextDue = micros();
  uint32_t passStart = millis(), decodeUs = 0, flushUs = 0, shown = 0;

  for (;;) {
    Ready r;
    xQueueReceive(p.readySlots, &r, portMAX_DELAY);
    if (r.slot == FRAME_END) break;
    if (p.stop) {
      // Stopping: discard frames still in flight, handing their canvases
      // back so the decoder wakes, sees `stop`, and sends FRAME_END.
      xQueueSend(p.freeSlots, &r.slot, portMAX_DELAY);
      continue;
    }

    // Show frames on a steady clock. If we've fallen more than a frame
    // behind, resync rather than bursting to catch up (that reads as jitter).
    int32_t wait = (int32_t)(nextDue - micros());
    if (wait > 0) {
      delay(wait / 1000);
      delayMicroseconds(wait % 1000);
    } else if (-wait > (int32_t)frameUs) nextDue = micros();
    nextDue += frameUs;

    uint32_t t = micros();
    buffers[r.slot]->flush();
    flushUs += micros() - t;
    decodeUs += r.decodeUs;
    shown++;
    xQueueSend(p.freeSlots, &r.slot, portMAX_DELAY);

    if (r.index == p.frames.size() - 1) {
      uint32_t elapsed = millis() - passStart;
      Serial.printf("video: %u frames in %u ms (%.1f fps) | avg decode %.1f ms, flush %.1f ms\n",
                    shown, elapsed, elapsed ? shown * 1000.0f / elapsed : 0.0f,
                    decodeUs / 1000.0f / shown, flushUs / 1000.0f / shown);
      passStart = millis();
      decodeUs = flushUs = shown = 0;
    }

    if (onFrame && onFrame()) p.stop = true;
  }

  vQueueDelete(p.freeSlots);
  vQueueDelete(p.readySlots);
  free(buf);
  return true;
}
