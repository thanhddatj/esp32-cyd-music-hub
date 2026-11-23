#include "ui_settings.h"
#include "theme.h"
#include "touch.h"
#include "ble_manager.h"

/*
 * Settings screen (480×320)
 * ──────────────────────────────────────────────────────────────
 * [28px]   Top bar: "< Back"  |  "Settings"
 * [244px]  Left menu (140px wide):  EQ / Device / About
 * [244px]  Right panel (remaining): content for selected menu
 * [48px]   (no bottom bar – back is in top bar)
 *
 * Menu items:
 *   0 – Equaliser  (5-band ± slider)
 *   1 – Device     (connected info, Disconnect button)
 *   2 – About      (firmware version)
 */

static constexpr int MENU_W   = 140;
static constexpr int MENU_X   = 0;
static constexpr int PANEL_X  = MENU_W;
static constexpr int PANEL_W  = SCREEN_W - MENU_W;
static constexpr int CONTENT_Y2 = TOPBAR_H;
static constexpr int CONTENT_H2 = SCREEN_H - TOPBAR_H;
static constexpr int ITEM_H   = 44;

static AppState g_returnTo    = AppState::PLAYER;
static int      g_activeMenu  = 0;

// ── EQ slider state ───────────────────────────────────────────
static int g_dragBand = -1;   // which band is being dragged

// ── Top bar ───────────────────────────────────────────────────
static void drawTopBar(TFT_eSPI& tft) {
  tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, CLR_SURFACE);
  tft.drawFastHLine(0, TOPBAR_H-1, SCREEN_W, CLR_BORDER);
  tft.drawFastVLine(MENU_W, TOPBAR_H, CONTENT_H2, CLR_BORDER);

  // Back button
  tft.fillRoundRect(6, 4, 54, 20, 4, CLR_BTN_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_ACCENT, CLR_BTN_BG);
  tft.drawString("< Back", 33, 14, FONT_SMALL);

  // Title
  tft.setTextColor(CLR_TEXT_PRI, CLR_SURFACE);
  tft.drawString("Settings", SCREEN_W/2 + MENU_W/4, 14, FONT_SMALL);
}

// ── Left menu ────────────────────────────────────────────────
static const char* menuLabels[] = { "Equaliser", "Device", "About" };
static constexpr int MENU_COUNT = 3;

static void drawMenu(TFT_eSPI& tft) {
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = CONTENT_Y2 + i * ITEM_H;
    bool active = (i == g_activeMenu);
    uint16_t bg = active ? CLR_ACCENT_DIM : CLR_BG;
    uint16_t fg = active ? CLR_ACCENT     : CLR_TEXT_PRI;

    tft.fillRect(MENU_X, y, MENU_W, ITEM_H - 1, bg);
    tft.drawFastHLine(MENU_X, y + ITEM_H - 1, MENU_W, CLR_BORDER);

    // Active indicator bar
    if (active)
      tft.fillRect(MENU_X, y, 3, ITEM_H-1, CLR_ACCENT);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(fg, bg);
    tft.drawString(menuLabels[i], MENU_X + 12, y + ITEM_H/2, FONT_SMALL);
  }
  // Fill rest below menu
  tft.fillRect(MENU_X, CONTENT_Y2 + MENU_COUNT*ITEM_H,
               MENU_W, CONTENT_H2 - MENU_COUNT*ITEM_H, CLR_BG);
}

// ── EQ panel ──────────────────────────────────────────────────
/*
 * 5 vertical sliders side-by-side.
 * Range: -12 … +12 dB.  Zero line in centre.
 * Slider track H = 160px, thumb = 12×8px.
 */
static constexpr int EQ_TOP      = CONTENT_Y2 + 20;
static constexpr int EQ_TRACK_H  = 140;
static constexpr int EQ_TRACK_W  = 8;
static constexpr int EQ_COL_W    = (PANEL_W - 20) / 5;  // ~54px per column
static constexpr int EQ_THUMB_H  = 14;
static constexpr int EQ_THUMB_W  = 24;

static int eqGainToY(int8_t gain) {
  // gain -12..+12 → y: EQ_TOP+EQ_TRACK_H .. EQ_TOP
  return EQ_TOP + (int)((12 - gain) * EQ_TRACK_H / 24);
}

static void drawEqSlider(TFT_eSPI& tft, int band, bool redrawAll) {
  EqBand* eq = ble_get_eq();
  int col_x = PANEL_X + 10 + band * EQ_COL_W;
  int cx     = col_x + EQ_COL_W/2;

  // Track
  tft.fillRect(cx - EQ_TRACK_W/2, EQ_TOP, EQ_TRACK_W, EQ_TRACK_H, CLR_SURFACE);
  // Zero line
  int zeroY = eqGainToY(0);
  tft.drawFastHLine(cx - EQ_TRACK_W/2 - 4, zeroY, EQ_TRACK_W + 8, CLR_BORDER);

  // Fill above/below zero
  int thumbY = eqGainToY(eq[band].gain);
  if (thumbY < zeroY) {
    // positive: fill above zero
    tft.fillRect(cx - EQ_TRACK_W/2, thumbY, EQ_TRACK_W, zeroY - thumbY, CLR_ACCENT);
  } else if (thumbY > zeroY) {
    // negative: fill below zero
    tft.fillRect(cx - EQ_TRACK_W/2, zeroY, EQ_TRACK_W, thumbY - zeroY, CLR_WARN);
  }

  // Thumb
  tft.fillRoundRect(cx - EQ_THUMB_W/2, thumbY - EQ_THUMB_H/2, EQ_THUMB_W, EQ_THUMB_H, 3, CLR_ACCENT);

  // Label
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString(eq[band].label, cx, EQ_TOP + EQ_TRACK_H + 14, 1);

  // dB value
  char dbbuf[8];
  snprintf(dbbuf, sizeof(dbbuf), "%+d", eq[band].gain);
  tft.setTextColor(CLR_TEXT_SEC, CLR_BG);
  tft.drawString(dbbuf, cx, EQ_TOP + EQ_TRACK_H + 26, 1);
}

static void drawEqPanel(TFT_eSPI& tft) {
  tft.fillRect(PANEL_X, CONTENT_Y2, PANEL_W, CONTENT_H2, CLR_BG);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(CLR_TEXT_SEC, CLR_BG);
  tft.drawString("Equaliser", PANEL_X + 10, CONTENT_Y2 + 8, FONT_SMALL);

  for (int i = 0; i < 5; i++) drawEqSlider(tft, i, true);
}

// ── Device panel ─────────────────────────────────────────────
static void drawDevicePanel(TFT_eSPI& tft) {
  tft.fillRect(PANEL_X, CONTENT_Y2, PANEL_W, CONTENT_H2, CLR_BG);
  tft.setTextDatum(TL_DATUM);

  int y = CONTENT_Y2 + 12;

  tft.setTextColor(CLR_TEXT_PRI, CLR_BG);
  tft.drawString("Connected Device", PANEL_X+10, y, FONT_SMALL); y += 22;

  tft.setTextColor(CLR_ACCENT, CLR_BG);
  tft.drawString("KBox", PANEL_X+10, y, FONT_SMALL); y += 22;

  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Service: AB00", PANEL_X+10, y, 1); y += 16;
  tft.drawString("RSSI: n/a", PANEL_X+10, y, 1); y += 28;

  // Disconnect button
  tft.fillRoundRect(PANEL_X+10, y, 110, 32, 6, CLR_ERR);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, CLR_ERR);
  tft.drawString("Disconnect", PANEL_X+10+55, y+16, FONT_SMALL);
}

// ── About panel ───────────────────────────────────────────────
static void drawAboutPanel(TFT_eSPI& tft) {
  tft.fillRect(PANEL_X, CONTENT_Y2, PANEL_W, CONTENT_H2, CLR_BG);
  tft.setTextDatum(TL_DATUM);
  int y = CONTENT_Y2 + 12;
  tft.setTextColor(CLR_TEXT_PRI, CLR_BG);
  tft.drawString("Music Hub", PANEL_X+10, y, FONT_SMALL); y+=22;
  tft.setTextColor(CLR_TEXT_SEC, CLR_BG);
  tft.drawString("Version: 1.0.0", PANEL_X+10, y, 1); y+=18;
  tft.drawString("Board: ESP32-3248S035", PANEL_X+10, y, 1); y+=18;
  tft.drawString("Display: ILI9488 480x320", PANEL_X+10, y, 1); y+=18;
  tft.drawString("BLE: KBox AB-service", PANEL_X+10, y, 1);
}

static void drawPanel(TFT_eSPI& tft) {
  switch (g_activeMenu) {
    case 0: drawEqPanel(tft);     break;
    case 1: drawDevicePanel(tft); break;
    case 2: drawAboutPanel(tft);  break;
  }
}

// ── EQ touch: drag thumb ──────────────────────────────────────
static void handleEqTouch(TFT_eSPI& tft, int16_t tx, int16_t ty) {
  for (int i = 0; i < 5; i++) {
    int col_x = PANEL_X + 10 + i * EQ_COL_W;
    int cx     = col_x + EQ_COL_W/2;

    // Check if touch is within slider column
    if (tx >= cx - EQ_THUMB_W/2 && tx <= cx + EQ_THUMB_W/2 &&
        ty >= EQ_TOP && ty <= EQ_TOP + EQ_TRACK_H) {
      // Map y → gain
      int8_t gain = (int8_t)(12 - ((ty - EQ_TOP) * 24 / EQ_TRACK_H));
      if (gain >  12) gain =  12;
      if (gain < -12) gain = -12;
      ble_cmd_eq_set(i, gain);
      drawEqSlider(tft, i, false);
      break;
    }
  }
}

// ── Public ───────────────────────────────────────────────────
void ui_settings_enter(TFT_eSPI& tft, AppState returnTo) {
  g_returnTo   = returnTo;
  g_activeMenu = 0;
  g_dragBand   = -1;

  tft.fillScreen(CLR_BG);
  drawTopBar(tft);
  drawMenu(tft);
  drawPanel(tft);
}

AppState ui_settings_update(TFT_eSPI& tft) {
  int16_t tx, ty;
  if (!touch_get(tft, tx, ty)) return AppState::SETTINGS;

  // Back button
  if (tx >= 6 && tx <= 60 && ty >= 4 && ty <= 24)
    return g_returnTo;

  // Left menu
  if (tx >= MENU_X && tx < MENU_W) {
    int row = (ty - CONTENT_Y2) / ITEM_H;
    if (row >= 0 && row < MENU_COUNT && row != g_activeMenu) {
      g_activeMenu = row;
      drawMenu(tft);
      drawPanel(tft);
    }
    return AppState::SETTINGS;
  }

  // Right panel touches
  if (tx >= PANEL_X) {
    if (g_activeMenu == 0) {
      // EQ sliders
      handleEqTouch(tft, tx, ty);
    } else if (g_activeMenu == 1) {
      // Disconnect button area
      int y = CONTENT_Y2 + 12 + 22 + 22 + 16 + 28;
      if (tx >= PANEL_X+10 && tx <= PANEL_X+120 &&
          ty >= y && ty <= y+32) {
        ble_disconnect();
        return g_returnTo;
      }
    }
  }

  return AppState::SETTINGS;
}
