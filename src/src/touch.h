#pragma once

#include <Arduino.h>

// Swipe detection for the FT3168 touch controller (I2C 0x38), polled on a
// background task. Directions are as seen on screen at `rotation`
// (the canvas rotation, 0-3).

enum Swipe : int8_t { SWIPE_NONE = 0, SWIPE_LEFT, SWIPE_RIGHT };

// Wire must already be started. Returns false if the controller doesn't answer.
bool touchBegin(uint8_t rotation);

// Returns the most recent completed swipe and clears it.
Swipe takeSwipe();
