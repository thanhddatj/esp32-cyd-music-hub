// Man hinh dieu khien nhac cho ESP32 CYD (ESP32-2432S028) 320x240 - ban BLE
// Ket noi khong day toi DSP Oldwu-Studio qua BLE:
//   Service AB00 | AB02 = ghi lenh | AB03 = notify tra du lieu
//   Dinh dang goi giong UART: A5 5A | cmd | len | data | 16
// Luu y: neu app dieu chinh am tren dien thoai dang ket noi voi DSP, ESP32 co the khong ket noi duoc.

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <NimBLEDevice.h>

#define DSP_NAME "KBox"   // ten BLE cua DSP (thay neu khac)

TFT_eSPI tft;
TFT_eSprite titleSpr(&tft);
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(33, 36);

// ======================= BLE =======================
volatile uint8_t bleState = 0;            // 0 = dang quet, 1 = dang ket noi, 2 = da ket noi
static NimBLEClient* bleClient = nullptr;
static NimBLERemoteCharacteristic* chWrite = nullptr;   // AB02
static NimBLERemoteCharacteristic* chNotify = nullptr;  // AB03

// Bo dem vong: callback notify (task BLE) -> loop() (task chinh)
static uint8_t rb[512];
static volatile uint16_t rbHead = 0, rbTail = 0;
static inline void rbPush(uint8_t b) {
  uint16_t n = (rbHead + 1) % sizeof(rb);
  if (n != rbTail) { rb[rbHead] = b; rbHead = n; }
}
static inline bool rbPop(uint8_t& b) {
  if (rbTail == rbHead) return false;
  b = rb[rbTail];
  rbTail = (rbTail + 1) % sizeof(rb);
  return true;
}

void notifyCb(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  for (size_t i = 0; i < n; i++) rbPush(d[i]);
}

class ClientCB : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*) override { bleState = 0; chWrite = nullptr; chNotify = nullptr; }
};
static ClientCB clientCB;

bool bleConnect() {
  bleState = 0;
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults res = scan->start(4, false);   // quet 4 giay
  bool ok = false;
  for (int i = 0; i < res.getCount() && !ok; i++) {
    NimBLEAdvertisedDevice dev = res.getDevice(i);
    if (dev.getName() != DSP_NAME) continue;

    bleState = 1;
    if (!bleClient) {
      bleClient = NimBLEDevice::createClient();
      bleClient->setClientCallbacks(&clientCB, false);
      bleClient->setConnectionParams(12, 12, 0, 200);   // ~15ms
      bleClient->setConnectTimeout(5);
    }
    if (!bleClient->connect(&dev)) break;

    NimBLERemoteService* svc = bleClient->getService(NimBLEUUID((uint16_t)0xAB00));
    if (svc) {
      NimBLERemoteCharacteristic* w = svc->getCharacteristic(NimBLEUUID((uint16_t)0xAB02));
      NimBLERemoteCharacteristic* n = svc->getCharacteristic(NimBLEUUID((uint16_t)0xAB03));
      if (w && n && n->canNotify() && n->subscribe(true, notifyCb)) {
        chWrite = w; chNotify = n;
        ok = true;
      }
    }
    if (!ok) bleClient->disconnect();
  }
  scan->clearResults();
  bleState = ok ? 2 : 0;
  return ok;
}

void bleTask(void*) {
  NimBLEDevice::init("cyd-music");
  NimBLEDevice::setMTU(185);
  for (;;) {
    if (bleState != 2) {
      if (!bleConnect()) vTaskDelay(pdMS_TO_TICKS(1500));
    } else {
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
}

// ======================= Giao thuc =======================
enum : uint8_t { CMD_MEDIA = 0xEB, CMD_CTRL = 0xF0, CMD_STATUS = 0xF8 };
enum : uint8_t { T_VOL = 0x00, T_BASS = 0x20, T_MID = 0x21, T_TREBLE = 0x22, T_SYS = 0xFD };
enum : uint16_t { SYS_PLAY = 0x7F2F, SYS_PREV = 0x7F3A, SYS_NEXT = 0x7F3B, SYS_MUTE = 0x7F6D };

struct Frame { uint8_t cmd; uint8_t len; uint8_t data[255]; };

static inline uint16_t le16(const uint8_t* p) { return p[0] | (p[1] << 8); }

void sendFrame(uint8_t cmd, const uint8_t* d, uint8_t n) {
  NimBLERemoteCharacteristic* w = chWrite;
  if (bleState != 2 || !w || n > 16) return;
  uint8_t buf[24];
  buf[0] = 0xA5; buf[1] = 0x5A; buf[2] = cmd; buf[3] = n;
  if (n) memcpy(buf + 4, d, n);
  buf[4 + n] = 0x16;
  w->writeValue(buf, 5 + n, false);   // write without response
}
void sendCtrl(uint8_t type, uint16_t val) {
  uint8_t d[3] = {type, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
  sendFrame(CMD_CTRL, d, 3);
}
void queryMedia(uint8_t t) { sendFrame(CMD_MEDIA, &t, 1); }

// May trang thai doc khung: A5 5A | cmd | len | data[len] | 16  (cho phep goi bi chia nho qua nhieu notify)
bool readFrame(Frame& out) {
  static uint8_t st = 0, idx = 0;
  static Frame cur;
  uint8_t b;
  while (rbPop(b)) {
    switch (st) {
      case 0: st = (b == 0xA5) ? 1 : 0; break;
      case 1: st = (b == 0x5A) ? 2 : ((b == 0xA5) ? 1 : 0); break;
      case 2: cur.cmd = b; st = 3; break;
      case 3: cur.len = b; idx = 0; st = b ? 4 : 5; break;
      case 4: cur.data[idx++] = b; if (idx >= cur.len) st = 5; break;
      case 5:
        st = 0;
        if (b == 0x16) { out = cur; return true; }
        break;
    }
  }
  return false;
}

// ======================= Trang thai =======================
struct State {
  bool supported = false, playing = false;
  uint16_t cur = 0, total = 0, track = 0, tracks = 0;
  uint8_t verTitle = 255;
  char title[96] = "";
  uint8_t vol = 0, mode = 0, bass = 15, mid = 15, treble = 15;
} S;

bool dTitle = true, dProg = true, dPlay = true, dMode = true;
uint32_t lastUserMs = 0;  // thoi diem nguoi dung keo thanh truot gan nhat

const char* modeName(uint8_t m) {
  switch (m) {
    case 3: return "AUX";
    case 4: return "TF CARD";
    case 5: return "USB DISK";
    case 6: return "BLUETOOTH";
    case 10: return "USB AUDIO";
    case 12: return "RADIO";
    case 13: return "OPTICAL";
    case 14: return "COAXIAL";
    case 15: return "I2S";
    case 20: return "TWS SLAVE";
    default: return "IDLE";
  }
}

// Tieu de la GBK. Chi hien thi phan ASCII, moi ky tu 2 byte -> '?'
void decodeTitle(const uint8_t* p, uint8_t n) {
  size_t o = 0;
  for (uint8_t i = 0; i < n && o < sizeof(S.title) - 1; i++) {
    if (p[i] >= 0x20 && p[i] < 0x7F) S.title[o++] = (char)p[i];
    else if (p[i] >= 0x81) { S.title[o++] = '?'; i++; }
  }
  S.title[o] = 0;
}

void onFrame(const Frame& f) {
  if (f.cmd == CMD_MEDIA && f.len >= 1) {
    if (f.data[0] == 0 && f.len >= 14) {
      S.supported = f.data[1];
      bool pl = f.data[2];
      uint16_t cur = le16(f.data + 3), tot = le16(f.data + 5);
      uint16_t tr = le16(f.data + 7), trs = le16(f.data + 9);
      if (pl != S.playing) dPlay = true;
      if (cur != S.cur || tot != S.total || tr != S.track || trs != S.tracks) dProg = true;
      S.playing = pl; S.cur = cur; S.total = tot; S.track = tr; S.tracks = trs;
      if (S.supported && f.data[12] != S.verTitle) {   // doi so phien ban moi lay lai tieu de
        S.verTitle = f.data[12];
        queryMedia(2);
      }
      if (!S.supported && S.title[0]) { S.title[0] = 0; S.verTitle = 255; dTitle = true; }
    } else if (f.data[0] == 2) {
      decodeTitle(f.data + 1, f.len - 1);
      dTitle = true;
    }
  } else if (f.cmd == CMD_STATUS && f.len >= 20) {
    uint8_t m = f.data[8];
    if (m != S.mode) { S.mode = m; dMode = true; }
    if (millis() - lastUserMs > 1500) {   // dang keo thi khong ghi de
      S.vol = f.data[9];
      S.bass = f.data[17]; S.mid = f.data[18]; S.treble = f.data[19];
    }
  }
}

// ======================= Giao dien =======================
enum Page { P_PLAY, P_EQ } page = P_PLAY;

#define C_BG    0x0000
#define C_PANEL 0x2124
#define C_ACC   0x04DF
#define C_DIM   0x8410

struct Slider { int16_t x, y, w, h; uint8_t maxv; uint8_t type; uint8_t* v; const char* name; };
Slider sVol = {60, 206, 240, 26, 32, T_VOL, &S.vol, "VOL"};
Slider sEq[3] = {
  {20, 56, 280, 34, 31, T_BASS, &S.bass, "BASS"},
  {20, 110, 280, 34, 31, T_MID, &S.mid, "MID"},
  {20, 164, 280, 34, 31, T_TREBLE, &S.treble, "TREBLE"},
};

void drawSlider(const Slider& s) {
  int fill = (int)(*s.v) * s.w / s.maxv;
  tft.fillRoundRect(s.x, s.y, s.w, s.h, 6, C_PANEL);
  if (fill > 6) tft.fillRoundRect(s.x, s.y, fill, s.h, 6, C_ACC);
  if (s.maxv == 31) {  // vach 0 dB o giua
    int zx = s.x + s.w * 15 / 31;
    tft.drawFastVLine(zx, s.y + 3, s.h - 6, C_DIM);
  }
  char buf[28];
  if (s.maxv == 31) snprintf(buf, sizeof buf, "%s  %+.1f dB", s.name, ((int)*s.v - 15) * 0.5f);
  else snprintf(buf, sizeof buf, "%s  %u", s.name, *s.v);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(buf, s.x + 8, s.y + s.h / 2, 2);
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 28, C_PANEL);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  const char* left = bleState == 2 ? modeName(S.mode) : (bleState == 1 ? "CONNECTING..." : "SCANNING...");
  tft.drawString(left, 8, 14, 2);
  tft.fillRoundRect(170, 3, 70, 22, 5, page == P_PLAY ? C_ACC : C_BG);
  tft.fillRoundRect(246, 3, 70, 22, 5, page == P_EQ ? C_ACC : C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("PLAY", 205, 14, 2);
  tft.drawString("EQ", 281, 14, 2);
}

void drawTitleStatic() {
  titleSpr.fillSprite(C_BG);
  titleSpr.setTextColor(TFT_WHITE);
  titleSpr.setTextDatum(TL_DATUM);
  const char* t;
  if (bleState != 2) t = "Searching for " DSP_NAME "...";
  else if (!S.supported) t = "(no media info)";
  else t = S.title[0] ? S.title : "...";
  int w = titleSpr.textWidth(t, 4);
  static int scrollX = 0;
  if (dTitle) scrollX = 0;
  if (w <= 300) {
    titleSpr.drawString(t, (300 - w) / 2, 0, 4);
  } else {
    titleSpr.drawString(t, -scrollX, 0, 4);
    titleSpr.drawString(t, -scrollX + w + 40, 0, 4);
    scrollX = (scrollX + 2) % (w + 40);
  }
  titleSpr.pushSprite(10, 40);
}

void fmtTime(char* b, size_t n, uint16_t s) { snprintf(b, n, "%u:%02u", s / 60, s % 60); }

void drawProgress() {
  tft.fillRect(0, 76, 320, 50, C_BG);
  char a[12], b[12], line[40];
  fmtTime(a, sizeof a, S.cur); fmtTime(b, sizeof b, S.total);
  tft.setTextColor(C_DIM);
  tft.setTextDatum(ML_DATUM);
  snprintf(line, sizeof line, "%u/%u", S.track, S.tracks);
  tft.drawString(S.supported ? line : "", 10, 86, 2);
  tft.setTextDatum(MR_DATUM);
  snprintf(line, sizeof line, "%s / %s", a, b);
  tft.drawString(S.supported ? line : "", 310, 86, 2);
  tft.fillRoundRect(10, 104, 300, 8, 4, C_PANEL);
  if (S.supported && S.total) {
    int w = (int)((uint32_t)S.cur * 300 / S.total);
    if (w > 8) tft.fillRoundRect(10, 104, w, 8, 4, C_ACC);
  }
}

struct Btn { int16_t x, y, w, h; };
Btn bPrev = {10, 130, 90, 60}, bPlay = {115, 130, 90, 60}, bNext = {220, 130, 90, 60};

void drawTransport() {
  uint16_t bg = C_PANEL;
  tft.fillRoundRect(bPrev.x, bPrev.y, bPrev.w, bPrev.h, 10, bg);
  tft.fillRoundRect(bPlay.x, bPlay.y, bPlay.w, bPlay.h, 10, C_ACC);
  tft.fillRoundRect(bNext.x, bNext.y, bNext.w, bNext.h, 10, bg);
  int cy = bPrev.y + bPrev.h / 2;
  int px = bPrev.x + bPrev.w / 2;   // prev |<
  tft.fillRect(px - 14, cy - 12, 4, 24, TFT_WHITE);
  tft.fillTriangle(px + 12, cy - 12, px + 12, cy + 12, px - 8, cy, TFT_WHITE);
  int nx = bNext.x + bNext.w / 2;   // next >|
  tft.fillRect(nx + 10, cy - 12, 4, 24, TFT_WHITE);
  tft.fillTriangle(nx - 12, cy - 12, nx - 12, cy + 12, nx + 8, cy, TFT_WHITE);
  int mx = bPlay.x + bPlay.w / 2;   // play / pause
  if (S.playing) {
    tft.fillRect(mx - 12, cy - 14, 8, 28, TFT_WHITE);
    tft.fillRect(mx + 4, cy - 14, 8, 28, TFT_WHITE);
  } else {
    tft.fillTriangle(mx - 10, cy - 15, mx - 10, cy + 15, mx + 14, cy, TFT_WHITE);
  }
}

void drawPage() {
  tft.fillScreen(C_BG);
  drawHeader();
  if (page == P_PLAY) {
    dTitle = dProg = dPlay = true;
    drawSlider(sVol);
  } else {
    for (auto& s : sEq) drawSlider(s);
    tft.fillRoundRect(110, 208, 100, 26, 6, C_PANEL);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("RESET EQ", 160, 221, 2);
  }
}

// ======================= Cam ung =======================
Slider* active = nullptr;
bool touching = false;
uint32_t lastSend = 0;
extern uint32_t tOv;

bool hit(int x, int y, int rx, int ry, int rw, int rh, int m = 0) {
  return x >= rx - m && x < rx + rw + m && y >= ry - m && y < ry + rh + m;
}
bool hitSlider(const Slider& s, int x, int y) { return hit(x, y, s.x, s.y, s.w, s.h, 8); }

void setSlider(Slider& s, int x) {
  int v = ((x - s.x) * s.maxv + s.w / 2) / s.w;
  v = constrain(v, 0, (int)s.maxv);
  lastUserMs = millis();
  if (v != *s.v) { *s.v = v; drawSlider(s); }
  if (millis() - lastSend > 40) { sendCtrl(s.type, *s.v); lastSend = millis(); }
}

void onPress(int x, int y) {
  if (hit(x, y, 170, 0, 70, 28)) { if (page != P_PLAY) { page = P_PLAY; drawPage(); } return; }
  if (hit(x, y, 246, 0, 74, 28)) { if (page != P_EQ) { page = P_EQ; drawPage(); } return; }
  if (page == P_PLAY) {
    if (hit(x, y, bPrev.x, bPrev.y, bPrev.w, bPrev.h)) { sendCtrl(T_SYS, SYS_PREV); tOv = 0; return; }
    if (hit(x, y, bNext.x, bNext.y, bNext.w, bNext.h)) { sendCtrl(T_SYS, SYS_NEXT); tOv = 0; return; }
    if (hit(x, y, bPlay.x, bPlay.y, bPlay.w, bPlay.h)) { sendCtrl(T_SYS, SYS_PLAY); S.playing = !S.playing; dPlay = true; tOv = 0; return; }
    if (hitSlider(sVol, x, y)) active = &sVol;
  } else {
    for (auto& s : sEq) if (hitSlider(s, x, y)) { active = &s; break; }
    if (!active && hit(x, y, 110, 208, 100, 26)) {
      uint8_t* v[3] = {&S.bass, &S.mid, &S.treble};
      for (int i = 0; i < 3; i++) { *v[i] = 15; sendCtrl(sEq[i].type, 15); delay(30); drawSlider(sEq[i]); }
      lastUserMs = millis();
    }
  }
}

void handleTouch() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    // Hieu chinh: neu cham bi lech thi doi 4 so nay
    int x = constrain(map(p.x, 200, 3700, 0, 320), 0, 319);
    int y = constrain(map(p.y, 240, 3800, 0, 240), 0, 239);
    if (!touching) { touching = true; onPress(x, y); }
    if (active) setSlider(*active, x);
  } else if (touching) {
    touching = false;
    if (active) { sendCtrl(active->type, *active->v); active = nullptr; }  // gui gia tri cuoi
  }
}

// ======================= Main =======================
uint32_t tOv = 0, tSt = 0, tScroll = 0;

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);   // ngang 320x240
  touchSPI.begin(25, 39, 32, 33);
  ts.begin(touchSPI);
  ts.setRotation(1);
  titleSpr.createSprite(300, 30);
  drawPage();
  xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  uint32_t now = millis();
  static uint8_t lastBle = 255;

  if (bleState != lastBle) {
    lastBle = bleState;
    dMode = true;
    if (bleState == 2) { tOv = 0; tSt = 0; }          // vua ket noi: hoi ngay
    else { S.supported = false; S.title[0] = 0; S.verTitle = 255; dTitle = true; dProg = true; }
  }

  Frame f;
  while (readFrame(f)) onFrame(f);

  if (bleState == 2) {
    if (now - tOv > 500) { tOv = now; queryMedia(0); }
    if (now - tSt > 1500) { tSt = now; sendFrame(CMD_STATUS, nullptr, 0); }
  }

  handleTouch();

  if (dMode) { dMode = false; drawHeader(); }
  if (page == P_PLAY) {
    if (dProg) { dProg = false; drawProgress(); }
    if (dPlay) { dPlay = false; drawTransport(); }
    if (dTitle || now - tScroll > 40) { tScroll = now; drawTitleStatic(); dTitle = false; }
    static uint8_t lastVol = 255;
    if (!active && S.vol != lastVol) { lastVol = S.vol; drawSlider(sVol); }
  } else {
    static uint8_t lb = 255, lm = 255, lt = 255;
    if (!active && (S.bass != lb || S.mid != lm || S.treble != lt)) {
      lb = S.bass; lm = S.mid; lt = S.treble;
      for (auto& s : sEq) drawSlider(s);
    }
  }
}
