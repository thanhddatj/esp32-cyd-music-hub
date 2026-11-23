#pragma once
#include <TFT_eSPI.h>
#include "main_states.h"

void      ui_player_enter(TFT_eSPI& tft);
AppState  ui_player_update(TFT_eSPI& tft);
// returns SCAN on disconnect, SETTINGS on gear press
