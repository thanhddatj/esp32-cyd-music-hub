#include "ui_player.h"
#include "theme.h"
#include "touch.h"
#include "ble_manager.h"

/*
 * Player screen (480×320)
 * ────────────────────────────────────────────────────────────
 *  [28px]  Top bar: "▶ KBox"  •  connected dot  •  ⚙ gear
 *  [200px] Album art placeholder (left 200×200) + track info
 *  [48px]  Progress bar + time
 *  [44px]  Bottom: |◀◀  ◀  ▶/⏸  ▶  ▶▶|
 */

// ── Layout ───────────────────────────────────────────────────
// Art area
static constexpr int ART_X = 20, ART_Y = TOPBAR_H + 10;
static constexpr int ART_W = 140, ART_H = 140;

// Track info (right of art)
static constexpr int INFO_X = ART_X + ART_W + 16;
static constexpr int INFO_W = SCREEN_W - INFO_X - 10;

// Progress bar
static constexpr int PROG_Y = TOPBAR_H + ART_H + 24;
static constexpr int PROG_X = 20, PROG_W = SCREEN_W - 40, PROG_H = 6;

// Transport buttons  (5 buttons in bottom bar)
static constexpr int BTN_Y    = BOTBAR_Y + 4;
static constexpr int BTN_H    = BOTBAR_H - 8;
static constexpr int BTN_W    = 56;
static constexpr int BTN_GAP  = 10;
static constexpr int BTNS_TOT = 5 * BTN_W + 4 * BTN_GAP;
static constexpr int BTN_X0   = (SCREEN_W - BTNS_TOT) / 2;

// Gear icon (top-right corner)
static constexpr int GEAR_X = SCREEN_W - 36, GEAR_Y = 4;
static constexpr int GEAR_W = 26, GEAR_H = 20;

// ── Button definitions ────────────────────────────────────────
struct TBtn { int x, y, w, h; const char* label; };
static TBtn buttons[5];

static void initButtons() {
  const char* labels[] = {"<<", "<", ">", ">", ">>"};
  // Actually: PREV, SEEK_BACK, PLAY/PAUSE, SEEK_FWD, NEXT
  for (int i = 0; i < 5; i++) {
    buttons[i] = { BTN_X0 + i * (BTN_W + BTN_GAP), BTN_Y, BTN_W, BTN_H, labels[i] };
  }
}

// ── State ─────────────────────────────────────────────────────
static TrackInfo g_lastTrack;
static bool      g_lastPlaying = false;
static uint32_t  g_progFakeMs  = 0;   // fake progress (replace with real notify)
static int       g_progPct     = 0;   // 0-100

// ── Draw helpers ──────────────────────────────────────────────
static void drawTopBar(TFT_eSPI& tft) {
  tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, CLR_SURFACE);
  tft.drawFastHLine(0, TOPBAR_H - 1, SCREEN_W, CLR_BORDER);

  // Connection dot
  tft.fillCircle(14, TOPBAR_H/2, 5, CLR_CONNECTED);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(CLR_TEXT_PRI, CLR_SURFACE);
  tft.drawString("KBox", 26, TOPBAR_H/2, FONT_SMALL);

  // Gear button
  tft.fillRoundRect(GEAR_X, GEAR_Y, GEAR_W, GEAR_H, 3, CLR_BTN_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_TEXT_SEC, CLR_BTN_BG);
  tft.drawString("*", GEAR_X + GEAR_W/2, GEAR_Y + GEAR_H/2, FONT_SMALL);  // ⚙ approx
}

static void drawArtPlaceholder(TFT_eSPI& tft) {
  tft.fillRoundRect(ART_X, ART_Y, ART_W, ART_H, 8, CLR_SURFACE);
  tft.drawRoundRect(ART_X, ART_Y, ART_W, ART_H, 8, CLR_BORDER);

  // Musical note primitive inside box
  int cx = ART_X + ART_W/2, cy = ART_Y + ART_H/2;
  tft.drawFastVLine(cx+12, cy-20, 28, CLR_ACCENT);
  tft.drawFastHLine(cx+12, cy-20, 14, CLR_ACCENT);
  for (int dx=-9;dx<=9;dx++)
    for (int dy=-5;dy<=5;dy++)
      if (dx*dx*25+dy*dy*81<=729)
        tft.drawPixel(cx+dx, cy+dy, CLR_ACCENT);
}

static void drawTrackInfo(TFT_eSPI& tft, const TrackInfo& t) {
  // Clear info zone
  tft.fillRect(INFO_X, TOPBAR_H+4, INFO_W, ART_H+10, CLR_BG);

  tft.setTextDatum(TL_DATUM);

  // Title (may be long → truncate)
  tft.setTextColor(CLR_TEXT_PRI, CLR_BG);
  String title = t.title;
  if (title.length() > 20) title = title.substring(0,19) + "~";
  tft.drawString(title.c_str(), INFO_X, TOPBAR_H + 16, FONT_SMALL);

  // Artist
  tft.setTextColor(CLR_TEXT_SEC, CLR_BG);
  tft.drawString(t.artist.substring(0,22).c_str(), INFO_X, TOPBAR_H + 36, FONT_SMALL);

  // Album
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);
  tft.drawString(t.album.substring(0,22).c_str(), INFO_X, TOPBAR_H + 54, FONT_SMALL);

  // Playing status badge
  uint16_t badgeBg = t.playing ? CLR_OK : CLR_WARN;
  tft.fillRoundRect(INFO_X, TOPBAR_H+76, 68, 20, 4, badgeBg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_BG, badgeBg);
  tft.drawString(t.playing ? "PLAYING" : "PAUSED", INFO_X+34, TOPBAR_H+86, 1);
}

static void drawProgressBar(TFT_eSPI& tft, int pct) {
  tft.fillRect(PROG_X, PROG_Y, PROG_W, PROG_H, CLR_SURFACE);
  int filled = (PROG_W * pct) / 100;
  if (filled > 0)
    tft.fillRect(PROG_X, PROG_Y, filled, PROG_H, CLR_ACCENT);
  tft.drawRect(PROG_X-1, PROG_Y-1, PROG_W+2, PROG_H+2, CLR_BORDER);
}

static void drawTransport(TFT_eSPI& tft, bool playing) {
  tft.fillRect(0, BOTBAR_Y, SCREEN_W, BOTBAR_H, CLR_SURFACE);
  tft.drawFastHLine(0, BOTBAR_Y, SCREEN_W, CLR_BORDER);

  // Labels for 5 buttons: PREV  |<  PLAY/PAUSE  >|  NEXT
  const char* labels[] = { "|<", "<", playing ? "||" : ">", ">", ">|" };

  for (int i = 0; i < 5; i++) {
    TBtn& b = buttons[i];
    bool isMain = (i == 2);
    uint16_t bg = isMain ? CLR_ACCENT : CLR_BTN_BG;
    uint16_t fg = isMain ? CLR_BG     : CLR_TEXT_PRI;

    tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, bg);
    tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, isMain ? CLR_ACCENT : CLR_BTN_BORDER);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(fg, bg);
    tft.drawString(labels[i], b.x + b.w/2, b.y + b.h/2, FONT_SMALL);
  }
}

// ── Public ───────────────────────────────────────────────────
void ui_player_enter(TFT_eSPI& tft) {
  initButtons();
  g_progPct    = 0;
  g_progFakeMs = millis();
  g_lastPlaying = false;

  tft.fillScreen(CLR_BG);
  drawTopBar(tft);
  drawArtPlaceholder(tft);

  const TrackInfo& t = ble_get_track();
  g_lastTrack = t;
  drawTrackInfo(tft, t);
  drawProgressBar(tft, 0);
  drawTransport(tft, t.playing);
}

AppState ui_player_update(TFT_eSPI& tft) {
  // Check disconnect
  if (ble_get_state() != BleState::CONNECTED)
    return AppState::SCAN;

  ble_manager_update();

  // Refresh track info if changed
  const TrackInfo& t = ble_get_track();
  bool infoChanged = (t.title != g_lastTrack.title ||
                      t.artist != g_lastTrack.artist ||
                      t.playing != g_lastTrack.playing);
  if (infoChanged) {
    g_lastTrack = t;
    drawTrackInfo(tft, t);
    drawTransport(tft, t.playing);
  }

  // Fake progress tick (replace with real timestamp from notify)
  if (t.playing) {
    uint32_t now = millis();
    if (now - g_progFakeMs > 2000) {
      g_progFakeMs = now;
      g_progPct = (g_progPct + 5) % 100;
      drawProgressBar(tft, g_progPct);
    }
  }

  // Touch
  int16_t tx, ty;
  if (!touch_get(tft, tx, ty)) return AppState::PLAYER;

  // Gear icon
  if (tx >= GEAR_X && tx <= GEAR_X + GEAR_W &&
      ty >= GEAR_Y && ty <= GEAR_Y + GEAR_H) {
    return AppState::SETTINGS;
  }

  // Transport buttons
  if (ty >= BTN_Y && ty <= BTN_Y + BTN_H) {
    for (int i = 0; i < 5; i++) {
      TBtn& b = buttons[i];
      if (tx >= b.x && tx <= b.x + b.w) {
        switch (i) {
          case 0: ble_cmd_prev();  break;
          case 1: /* seek back – no cmd yet */ break;
          case 2:
            if (t.playing) ble_cmd_pause();
            else           ble_cmd_play();
            break;
          case 3: /* seek fwd  – no cmd yet */ break;
          case 4: ble_cmd_next();  break;
        }
        break;
      }
    }
  }

  return AppState::PLAYER;
}
