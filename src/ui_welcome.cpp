#include "ui_welcome.h"
#include "theme.h"
#include <TFT_eSPI.h>

/*
 * Welcome screen
 * ──────────────
 * Shows logo + tagline + animated loading bar.
 * After WELCOME_MS ms it advances to AppState::SCAN automatically.
 * A touch also skips the wait.
 */

static constexpr uint32_t WELCOME_MS = 3600;
static uint32_t g_enterTime = 0;

// ── Accent bar animation state ───────────────────────────────
static int g_barW = 0;
static constexpr int BAR_Y   = 240;
static constexpr int BAR_H   = 4;
static constexpr int BAR_X   = 80;
static constexpr int BAR_MAX = 320;  // width of bar

static void drawBackground(TFT_eSPI& tft) {
  tft.fillScreen(CLR_BG);

  // Top decorative accent line
  tft.drawFastHLine(0, 0, SCREEN_W, CLR_ACCENT);

  // Subtle vertical gradient via horizontal bands (pseudo-gradient)
  for (int y = 1; y < SCREEN_H; y++) {
    uint16_t shade = (y < 60) ? CLR_SURFACE : CLR_BG;
    if (y < 60) tft.drawFastHLine(0, y, SCREEN_W, shade);
  }
}

static void drawLogo(TFT_eSPI& tft) {
  // Music note icon drawn with primitives
  int cx = 240, cy = 90;

  // Note stem
  tft.drawFastVLine(cx + 18, cy - 30, 36, CLR_ACCENT);
  // Note flag / beam
  tft.drawFastHLine(cx + 18, cy - 30, 20, CLR_ACCENT);
  // Note head (filled ellipse approximation with circles)
  for (int dx = -10; dx <= 10; dx++)
    for (int dy = -6; dy <= 6; dy++)
      if (dx*dx*36 + dy*dy*100 <= 3600)
        tft.drawPixel(cx + dx, cy + dy, CLR_ACCENT);

  // Second note (offset)
  int cx2 = cx + 30, cy2 = cy + 10;
  tft.drawFastVLine(cx2 + 18, cy2 - 30, 36, CLR_ACCENT);
  for (int dx = -10; dx <= 10; dx++)
    for (int dy = -6; dy <= 6; dy++)
      if (dx*dx*36 + dy*dy*100 <= 3600)
        tft.drawPixel(cx2 + dx, cy2 + dy, CLR_ACCENT);
}

static void drawText(TFT_eSPI& tft) {
  tft.setTextDatum(MC_DATUM);

  // Main title
  tft.setTextColor(CLR_TEXT_PRI, CLR_BG);
  tft.drawString("ESP32 Music Hub", 240, 160, FONT_MED);

  // Sub-title
  tft.setTextColor(CLR_TEXT_SEC, CLR_BG);
  tft.drawString("CYD Music Controller", 240, 185, FONT_SMALL);

  // "Tap to skip" hint
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString("Loading...", 240, 300, FONT_SMALL);
}

static void drawBarFrame(TFT_eSPI& tft) {
  // Empty track
  tft.drawRect(BAR_X - 1, BAR_Y - 1, BAR_MAX + 2, BAR_H + 2, CLR_BORDER);
}

static void updateBar(TFT_eSPI& tft) {
  uint32_t elapsed = millis() - g_enterTime;
  int targetW = (int)((float)elapsed / WELCOME_MS * BAR_MAX);
  if (targetW > BAR_MAX) targetW = BAR_MAX;

  if (targetW > g_barW) {
    tft.fillRect(BAR_X + g_barW, BAR_Y, targetW - g_barW, BAR_H, CLR_ACCENT);
    g_barW = targetW;
  }
}

// ── Public ───────────────────────────────────────────────────
void ui_welcome_enter(TFT_eSPI& tft) {
  g_enterTime = millis();
  g_barW      = 0;

  drawBackground(tft);
  drawLogo(tft);
  drawText(tft);
  drawBarFrame(tft);
}

AppState ui_welcome_update(TFT_eSPI& tft) {
  updateBar(tft);

  // Check touch (simple raw read – no debounce needed for skip)
  uint16_t x, y;
  if (tft.getTouch(&x, &y, 600)) return AppState::SCAN;

  // Timeout
  if (millis() - g_enterTime >= WELCOME_MS) return AppState::SCAN;

  return AppState::WELCOME;
}
