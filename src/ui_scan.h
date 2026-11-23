#pragma once
#include <TFT_eSPI.h>
#include "main_states.h"

void      ui_scan_enter(TFT_eSPI& tft);
AppState  ui_scan_update(TFT_eSPI& tft);  // returns PLAYER on successful connect
