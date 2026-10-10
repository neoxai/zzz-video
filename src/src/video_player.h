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
// With `loop`, playback restarts seamlessly and the call never returns.
// Returns false if the file can't be loaded.
bool playMjpeg(const char *path, Arduino_Canvas *buffers[2], uint8_t fps, bool loop = false);
