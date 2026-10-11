#pragma once

#include <Arduino.h>

// Swipe detection for the touch controller (CST816 on v2 boards, FT3168 on
// v1 — see TOUCH_ADDR), polled on a background task. Directions are as seen
// on screen at `rotation` (the canvas rotation, 0-3).

// Vertical swipes are folded in: up reports SWIPE_LEFT, down SWIPE_RIGHT.
enum Swipe : int8_t { SWIPE_NONE = 0, SWIPE_LEFT, SWIPE_RIGHT };

// Wire must already be started. Polling starts either way; returns false if
// the controller didn't answer yet (a sleeping CST816 wakes on touch).
bool touchBegin(uint8_t rotation);

// Returns the most recent completed swipe and clears it.
Swipe takeSwipe();
