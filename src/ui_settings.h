#pragma once
#include <TFT_eSPI.h>
#include "main_states.h"

void      ui_settings_enter(TFT_eSPI& tft, AppState returnTo);
AppState  ui_settings_update(TFT_eSPI& tft);
// returns previous state (PLAYER) on back press
