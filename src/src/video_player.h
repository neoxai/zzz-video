#pragma once

#include <Arduino_GFX_Library.h>

// Plays an MJPEG file (concatenated JPEG frames, as produced by
// `ffmpeg -f mjpeg`) from LittleFS onto a canvas, flushing after each frame.
// Frames are centered on the canvas and paced to `fps`. With `loop`, the file
// stays in PSRAM and playback restarts seamlessly — the call never returns.
// Returns false if the file can't be loaded.
bool playMjpeg(const char *path, Arduino_Canvas *canvas, uint8_t fps, bool loop = false);
