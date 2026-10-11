#pragma once

#include <Arduino_GFX_Library.h>

// Plays an MJPEG file (concatenated JPEG frames, as produced by
// `ffmpeg -f mjpeg`) from LittleFS, centered and paced to `fps`.
//
// Decoding and flushing are pipelined across both cores: core 0 decodes the
// next frame into one canvas while the caller's core flushes the other, so a
// frame costs max(decode, flush) instead of their sum. `buffers` must be two
// begun canvases of the same size and rotation, sharing one output.
//
// Called after each frame is shown; return true to stop playback.
typedef bool (*FrameHook)();

// With `loop`, playback restarts seamlessly and only returns once `onFrame`
// asks it to stop. Returns false if the file can't be loaded.
bool playMjpeg(const char *path, Arduino_Canvas *buffers[2], uint8_t fps, bool loop = false,
               FrameHook onFrame = nullptr);
