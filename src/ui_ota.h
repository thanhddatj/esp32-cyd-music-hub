#pragma once
#include <TFT_eSPI.h>
#include "main_states.h"

// Màn hình OTA: hiện IP, QR text, progress, nút Cancel
void     ui_ota_enter(TFT_eSPI& tft);
AppState ui_ota_update(TFT_eSPI& tft);  // returns SETTINGS khi cancel / xong
