#pragma once
#include <TFT_eSPI.h>
#include "main_states.h"

void      ui_welcome_enter(TFT_eSPI& tft);
AppState  ui_welcome_update(TFT_eSPI& tft);  // returns SCAN when ready
