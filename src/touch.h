#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "theme.h"

void touch_init();

// Returns true + fills (tx,ty) if a stable press is detected.
// Implements debounce so callers can call every loop tick safely.
bool touch_get(TFT_eSPI& tft, int16_t& tx, int16_t& ty);
