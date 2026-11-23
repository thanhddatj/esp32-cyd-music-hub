#include "ui_ota.h"
#include "theme.h"
#include "touch.h"
#include "ota_manager.h"

/*
 * OTA screen (480×320)
 * ─────────────────────────────────────────────────────────
 *  [28px]   Top bar: "Firmware OTA"
 *  [244px]  Nội dung:
 *             - WiFi name + IP (lớn, dễ đọc)
 *             - Hướng dẫn 3 bước
 *             - Progress bar (ẩn khi chưa upload)
 *             - Status text
 *  [48px]   Bottom: [Cancel] button
 */

// ── Layout ───────────────────────────────────────────────────
static constexpr int CX         = SCREEN_W / 2;
static constexpr int INFO_Y     = TOPBAR_H + 16;
static constexpr int STEPS_Y    = TOPBAR_H + 90;
static constexpr int PROG_Y     = BOTBAR_Y - 60;
static constexpr int PROG_X     = 30;
static constexpr int PROG_W     = SCREEN_W - 60;
static constexpr int PROG_H     = 14;
static constexpr int STATUS_Y   = PROG_Y + PROG_H + 14;
static constexpr int CANCEL_X   = (SCREEN_W - 120) / 2;
static constexpr int CANCEL_Y   = BOTBAR_Y + 7;
static constexpr int CANCEL_W   = 120;
static constexpr int CANCEL_H   = 34;

// ── State ─────────────────────────────────────────────────────
static OtaState  g_lastOtaState = OtaState::IDLE;
static uint8_t   g_lastProgress = 255;
static bool      g_otaStarted   = false;

// ── Draw helpers ──────────────────────────────────────────────
static void drawTopBar(TFT_eSPI& tft) {
  tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, CLR_SURFACE);
  tft.drawFastHLine(0, TOPBAR_H - 1, SCREEN_W, CLR_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_TEXT_PRI, CLR_SURFACE);
  tft.drawString("Firmware OTA", CX, TOPBAR_H / 2, FONT_SMALL);
}

static void drawWifiInfo(TFT_eSPI& tft) {
  // WiFi name box
  tft.fillRoundRect(20, INFO_Y, SCREEN_W - 40, 66, 10, CLR_SURFACE);
  tft.drawRoundRect(20, INFO_Y, SCREEN_W - 40, 66, 10, CLR_ACCENT);

  tft.setTextDatum(MC_DATUM);

  // Label
  tft.setTextColor(CLR_TEXT_DIM, CLR_SURFACE);
  tft.drawString("Ket noi WiFi:", CX, INFO_Y + 14, FONT_SMALL);

  // SSID
  tft.setTextColor(CLR_ACCENT, CLR_SURFACE);
  tft.drawString("MusicHub-OTA", CX, INFO_Y + 32, FONT_SMALL);

  // Password + IP
  String ipLine = "Pass: 12345678   IP: " + ota_get_ip();
  tft.setTextColor(CLR_TEXT_SEC, CLR_SURFACE);
  tft.drawString(ipLine.c_str(), CX, INFO_Y + 50, 1);
}

static void drawSteps(TFT_eSPI& tft) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(CLR_TEXT_DIM, CLR_BG);

  struct Step { const char* num; const char* text; };
  Step steps[] = {
    { "1.", "Ket noi WiFi \"MusicHub-OTA\"" },
    { "2.", "Mo trinh duyet, vao: " },
    { "3.", "Chon file .bin va bam Upload" },
  };

  int y = STEPS_Y;
  for (auto& s : steps) {
    tft.setTextColor(CLR_ACCENT, CLR_BG);
    tft.drawString(s.num, 24, y, FONT_SMALL);
    tft.setTextColor(CLR_TEXT_SEC, CLR_BG);
    tft.drawString(s.text, 46, y, FONT_SMALL);
    if (String(s.num) == "2.") {
      // IP lớn hơn ở dòng riêng
      tft.setTextColor(CLR_ACCENT, CLR_BG);
      tft.drawString(("http://" + ota_get_ip()).c_str(), 46, y + 18, FONT_SMALL);
      y += 36;
    } else {
      y += 22;
    }
  }
}

static void drawProgressBar(TFT_eSPI& tft, uint8_t pct) {
  // Track
  tft.fillRoundRect(PROG_X, PROG_Y, PROG_W, PROG_H, 4, CLR_SURFACE);
  tft.drawRoundRect(PROG_X - 1, PROG_Y - 1, PROG_W + 2, PROG_H + 2, 4, CLR_BORDER);

  // Fill
  int filled = (int)(PROG_W * pct) / 100;
  if (filled > 0) {
    uint16_t col = (pct == 100) ? CLR_OK : CLR_ACCENT;
    tft.fillRoundRect(PROG_X, PROG_Y, filled, PROG_H, 4, col);
  }

  // Percentage text
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  tft.fillRect(CX - 20, STATUS_Y - 4, 40, 14, CLR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(CLR_TEXT_PRI, CLR_BG);
  tft.drawString(buf, CX, STATUS_Y + 4, FONT_SMALL);
}

static void drawStatus(TFT_eSPI& tft, const char* msg, uint16_t color) {
  tft.fillRect(0, STATUS_Y + 20, SCREEN_W, 24, CLR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, CLR_BG);
  tft.drawString(msg, CX, STATUS_Y + 30, FONT_SMALL);
}

static void drawCancelButton(TFT_eSPI& tft, bool enabled) {
  tft.fillRect(0, BOTBAR_Y, SCREEN_W, BOTBAR_H, CLR_SURFACE);
  tft.drawFastHLine(0, BOTBAR_Y, SCREEN_W, CLR_BORDER);

  uint16_t bg = enabled ? CLR_BTN_BG : CLR_SURFACE;
  uint16_t fg = enabled ? CLR_TEXT_PRI : CLR_TEXT_DIM;

  tft.fillRoundRect(CANCEL_X, CANCEL_Y, CANCEL_W, CANCEL_H, 6, bg);
  tft.drawRoundRect(CANCEL_X, CANCEL_Y, CANCEL_W, CANCEL_H, 6, CLR_BORDER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString("Huy / Thoat", CANCEL_X + CANCEL_W / 2, CANCEL_Y + CANCEL_H / 2, FONT_SMALL);
}

// ── Redraw only what changed ──────────────────────────────────
static void refreshState(TFT_eSPI& tft) {
  OtaState st  = ota_get_state();
  uint8_t  pct = ota_get_progress();

  bool stateChanged = (st != g_lastOtaState);
  bool progChanged  = (pct != g_lastProgress);

  if (!stateChanged && !progChanged) return;

  g_lastOtaState = st;
  g_lastProgress = pct;

  switch (st) {
    case OtaState::READY:
      drawStatus(tft, "San sang nhan firmware...", CLR_TEXT_DIM);
      break;

    case OtaState::UPLOADING:
      drawProgressBar(tft, pct);
      drawStatus(tft, "Dang nhan file...", CLR_TEXT_SEC);
      break;

    case OtaState::FLASHING:
      drawProgressBar(tft, pct);
      drawStatus(tft, "Dang ghi flash...", CLR_ACCENT);
      break;

    case OtaState::SUCCESS:
      drawProgressBar(tft, 100);
      drawStatus(tft, "Thanh cong!  Dang reboot...", CLR_OK);
      drawCancelButton(tft, false);
      break;

    case OtaState::ERROR:
      drawStatus(tft, "LOI! Kiem tra file va thu lai.", CLR_ERR);
      drawCancelButton(tft, true);
      break;

    default:
      break;
  }
}

// ── Pulsing dot to show server is alive ───────────────────────
static uint32_t g_dotLast = 0;
static bool     g_dotOn   = false;
static void tickHeartbeat(TFT_eSPI& tft) {
  if (ota_get_state() != OtaState::READY) return;
  uint32_t now = millis();
  if (now - g_dotLast < 800) return;
  g_dotLast = now;
  g_dotOn   = !g_dotOn;
  uint16_t col = g_dotOn ? CLR_OK : CLR_SURFACE;
  tft.fillCircle(SCREEN_W - 16, TOPBAR_H / 2, 5, col);
}

// ── Public ───────────────────────────────────────────────────
void ui_ota_enter(TFT_eSPI& tft) {
  g_lastOtaState = OtaState::IDLE;
  g_lastProgress = 255;
  g_otaStarted   = false;
  g_dotLast      = 0;
  g_dotOn        = false;

  tft.fillScreen(CLR_BG);
  drawTopBar(tft);

  // Khởi động OTA server
  ota_start("MusicHub-OTA", "12345678");
  g_otaStarted = true;

  drawWifiInfo(tft);
  drawSteps(tft);

  // Vẽ progress bar rỗng
  drawProgressBar(tft, 0);
  drawStatus(tft, "San sang nhan firmware...", CLR_TEXT_DIM);
  drawCancelButton(tft, true);
}

AppState ui_ota_update(TFT_eSPI& tft) {
  ota_update();         // xử lý reboot nếu cần
  tickHeartbeat(tft);   // nhấp nháy dot
  refreshState(tft);    // cập nhật progress

  int16_t tx, ty;
  if (!touch_get(tft, tx, ty)) return AppState::OTA;

  // Cancel / Exit – chỉ cho phép khi chưa flash thành công
  OtaState st = ota_get_state();
  if (st != OtaState::SUCCESS &&
      tx >= CANCEL_X && tx <= CANCEL_X + CANCEL_W &&
      ty >= CANCEL_Y && ty <= CANCEL_Y + CANCEL_H) {
    ota_stop();
    g_otaStarted = false;
    return AppState::SETTINGS;
  }

  return AppState::OTA;
}
