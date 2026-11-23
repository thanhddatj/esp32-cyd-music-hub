#include "touch.h"

/*
 * ESP32-3248S035 uses XPT2046 on a secondary SPI bus.
 * TFT_eSPI handles touch via its own getTouch() when
 * TOUCH_CS is defined in User_Setup.h.
 *
 * Calibration data: adjust these values after running the
 * built-in TFT_eSPI calibration sketch once on your board.
 */
static uint16_t calData[5] = { 275, 3620, 264, 3532, 1 };

static uint32_t lastTouchMs = 0;
static bool     wasPressed  = false;

void touch_init() {
  // TFT_eSPI init already sets up the touch SPI;
  // we just store calibration so getTouch() uses it.
  // If you ran calibrate sketch, replace calData above.
}

bool touch_get(TFT_eSPI& tft, int16_t& tx, int16_t& ty) {
  uint16_t x, y;
  bool pressed = tft.getTouch(&x, &y, 600);  // threshold 600

  uint32_t now = millis();

  if (pressed && !wasPressed && (now - lastTouchMs > TOUCH_DEBOUNCE_MS)) {
    tx = (int16_t)x;
    ty = (int16_t)y;
    lastTouchMs = now;
    wasPressed  = true;
    return true;
  }

  if (!pressed) wasPressed = false;
  return false;
}
