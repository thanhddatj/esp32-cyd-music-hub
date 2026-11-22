// Man hinh dieu khien nhac - ESP32 CYD 3.5 inch (ESP32-3248S035, ST7796 480x320, cam ung dien tro XPT2046)
// Ket noi khong day toi DSP Oldwu-Studio qua BLE:
//   Service AB00 | AB02 = ghi lenh | AB03 = notify tra du lieu
//   Dinh dang goi giong UART: A5 5A | cmd | len | data | 16
// Luu y: neu app dieu chinh am tren dien thoai dang ket noi voi DSP, ESP32 co the khong ket noi duoc.

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <NimBLEDevice.h>
#include <nvs_flash.h>

#define DSP_NAME "KBox"       // ten BLE cua DSP (thay neu khac)
#define SCREEN_ROTATION 1     // neu hinh bi lat nguoc, doi thanh 3

static const int SW = 480, SH = 320;

TFT_eSPI tft;
TFT_eSprite titleSpr(&tft);
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(33, 36);

// ======================= BLE =======================
volatile uint8_t bleState = 0;            // 0 = dang quet, 1 = dang ket noi, 2 = da ket noi
static NimBLEClient* bleClient = nullptr;
static NimBLERemoteCharacteristic* chWrite = nullptr;   // AB02
static NimBLERemoteCharacteristic* chNotify = nullptr;  // AB03

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
  NimBLEScanResults res = scan->start(4, false);
  bool ok = false;
  for (int i = 0; i < res.getCount() && !ok; i++) {
    NimBLEAdvertisedDevice dev = res.getDevice(i);
    if (dev.getName() != DSP_NAME) continue;

    bleState = 1;
    if (!bleClient) {
      bleClient = NimBLEDevice::createClient();
      bleClient->setClientCallbacks(&clientCB, false);
      bleClient->setConnectionParams(12, 12, 0, 200);
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
  // NimBLEDevice::init() da duoc goi trong setup()
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
  w->writeValue(buf, 5 + n, false);
}
void sendCtrl(uint8_t type, uint16_t val) {
  uint8_t d[3] = {type, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
  sendFrame(CMD_CTRL, d, 3);
}
void queryMedia(uint8_t t) { sendFrame(CMD_MEDIA, &t, 1); }

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
uint32_t lastUserMs = 0;

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
      if (S.supported && f.data[12] != S.verTitle) {
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
    if (millis() - lastUserMs > 1500) {
      S.vol = f.data[9];
      S.bass = f.data[17]; S.mid = f.data[18]; S.treble = f.data[19];
    }
  }
}

// ======================= Giao dien 480x320 =======================
enum Page { P_PLAY, P_EQ } page = P_PLAY;

#define C_BG    0x0000
#define C_PANEL 0x2124
#define C_ACC   0x04DF
#define C_DIM   0x8410

struct Slider { int16_t x, y, w, h; uint8_t maxv; uint8_t type; uint8_t* v; const char* name; };
Slider sVol = {80, 262, 380, 40, 32, T_VOL, &S.vol, "VOL"};
Slider sEq[3] = {
  {30, 56, 420, 48, 31, T_BASS, &S.bass, "BASS"},
  {30, 122, 420, 48, 31, T_MID, &S.mid, "MID"},
  {30, 188, 420, 48, 31, T_TREBLE, &S.treble, "TREBLE"},
};

// Vung bam (toa do man hinh)
const int TAB_PLAY_X = 250, TAB_EQ_X = 365, TAB_W = 105;
struct Btn { int16_t x, y, w, h; };
Btn bPrev = {15, 160, 135, 70}, bPlay = {172, 160, 135, 70}, bNext = {330, 160, 135, 70};
Btn bReset = {180, 262, 120, 40};

void drawSlider(const Slider& s) {
  int fill = (int)(*s.v) * s.w / s.maxv;
  tft.fillRoundRect(s.x, s.y, s.w, s.h, 8, C_PANEL);
  if (fill > 8) tft.fillRoundRect(s.x, s.y, fill, s.h, 8, C_ACC);
  if (s.maxv == 31) {
    int zx = s.x + s.w * 15 / 31;
    tft.drawFastVLine(zx, s.y + 4, s.h - 8, C_DIM);
  }
  char buf[28];
  if (s.maxv == 31) snprintf(buf, sizeof buf, "%s  %+.1f dB", s.name, ((int)*s.v - 15) * 0.5f);
  else snprintf(buf, sizeof buf, "%s  %u", s.name, *s.v);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(buf, s.x + 12, s.y + s.h / 2, 4);
}

void drawHeader() {
  tft.fillRect(0, 0, SW, 36, C_PANEL);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(ML_DATUM);
  const char* left = bleState == 2 ? modeName(S.mode) : (bleState == 1 ? "CONNECTING..." : "SCANNING...");
  tft.drawString(left, 10, 18, 4);
  tft.fillRoundRect(TAB_PLAY_X, 4, TAB_W, 28, 6, page == P_PLAY ? C_ACC : C_BG);
  tft.fillRoundRect(TAB_EQ_X, 4, TAB_W, 28, 6, page == P_EQ ? C_ACC : C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("PLAY", TAB_PLAY_X + TAB_W / 2, 18, 2);
  tft.drawString("EQ", TAB_EQ_X + TAB_W / 2, 18, 2);
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
  if (w <= 460) {
    titleSpr.drawString(t, (460 - w) / 2, 0, 4);
  } else {
    titleSpr.drawString(t, -scrollX, 0, 4);
    titleSpr.drawString(t, -scrollX + w + 40, 0, 4);
    scrollX = (scrollX + 2) % (w + 40);
  }
  titleSpr.pushSprite(10, 52);
}

void fmtTime(char* b, size_t n, uint16_t s) { snprintf(b, n, "%u:%02u", s / 60, s % 60); }

void drawProgress() {
  tft.fillRect(0, 92, SW, 60, C_BG);
  char a[12], b[12], line[40];
  fmtTime(a, sizeof a, S.cur); fmtTime(b, sizeof b, S.total);
  tft.setTextColor(C_DIM);
  tft.setTextDatum(ML_DATUM);
  snprintf(line, sizeof line, "%u/%u", S.track, S.tracks);
  tft.drawString(S.supported ? line : "", 10, 106, 2);
  tft.setTextDatum(MR_DATUM);
  snprintf(line, sizeof line, "%s / %s", a, b);
  tft.drawString(S.supported ? line : "", 470, 106, 2);
  tft.fillRoundRect(10, 128, 460, 10, 5, C_PANEL);
  if (S.supported && S.total) {
    int w = (int)((uint32_t)S.cur * 460 / S.total);
    if (w > 10) tft.fillRoundRect(10, 128, w, 10, 5, C_ACC);
  }
}

void drawTransport() {
  tft.fillRoundRect(bPrev.x, bPrev.y, bPrev.w, bPrev.h, 12, C_PANEL);
  tft.fillRoundRect(bPlay.x, bPlay.y, bPlay.w, bPlay.h, 12, C_ACC);
  tft.fillRoundRect(bNext.x, bNext.y, bNext.w, bNext.h, 12, C_PANEL);
  int cy = bPrev.y + bPrev.h / 2;
  int px = bPrev.x + bPrev.w / 2;   // prev |<
  tft.fillRect(px - 18, cy - 16, 5, 32, TFT_WHITE);
  tft.fillTriangle(px + 16, cy - 16, px + 16, cy + 16, px - 10, cy, TFT_WHITE);
  int nx = bNext.x + bNext.w / 2;   // next >|
  tft.fillRect(nx + 13, cy - 16, 5, 32, TFT_WHITE);
  tft.fillTriangle(nx - 16, cy - 16, nx - 16, cy + 16, nx + 10, cy, TFT_WHITE);
  int mx = bPlay.x + bPlay.w / 2;   // play / pause
  if (S.playing) {
    tft.fillRect(mx - 16, cy - 20, 10, 40, TFT_WHITE);
    tft.fillRect(mx + 6, cy - 20, 10, 40, TFT_WHITE);
  } else {
    tft.fillTriangle(mx - 12, cy - 20, mx - 12, cy + 20, mx + 20, cy, TFT_WHITE);
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
    tft.fillRoundRect(bReset.x, bReset.y, bReset.w, bReset.h, 8, C_PANEL);
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("RESET EQ", bReset.x + bReset.w / 2, bReset.y + bReset.h / 2, 2);
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
  if (hit(x, y, TAB_PLAY_X - 4, 0, TAB_W + 8, 36)) { if (page != P_PLAY) { page = P_PLAY; drawPage(); } return; }
  if (hit(x, y, TAB_EQ_X - 4, 0, SW - TAB_EQ_X + 4, 36)) { if (page != P_EQ) { page = P_EQ; drawPage(); } return; }
  if (page == P_PLAY) {
    if (hit(x, y, bPrev.x, bPrev.y, bPrev.w, bPrev.h)) { sendCtrl(T_SYS, SYS_PREV); tOv = 0; return; }
    if (hit(x, y, bNext.x, bNext.y, bNext.w, bNext.h)) { sendCtrl(T_SYS, SYS_NEXT); tOv = 0; return; }
    if (hit(x, y, bPlay.x, bPlay.y, bPlay.w, bPlay.h)) { sendCtrl(T_SYS, SYS_PLAY); S.playing = !S.playing; dPlay = true; tOv = 0; return; }
    if (hitSlider(sVol, x, y)) active = &sVol;
  } else {
    for (auto& s : sEq) if (hitSlider(s, x, y)) { active = &s; break; }
    if (!active && hit(x, y, bReset.x, bReset.y, bReset.w, bReset.h)) {
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
    int x = constrain(map(p.x, 200, 3700, 0, SW), 0, SW - 1);
    int y = constrain(map(p.y, 240, 3800, 0, SH), 0, SH - 1);
    if (!touching) { touching = true; onPress(x, y); }
    if (active) setSlider(*active, x);
  } else if (touching) {
    touching = false;
    if (active) { sendCtrl(active->type, *active->v); active = nullptr; }
  }
}

// ======================= Main =======================
uint32_t tOv = 0, tSt = 0, tScroll = 0;

void setup() {
  Serial.begin(115200);
  Serial.printf("boot: heap free=%u max=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Bao Arduino giu lai bo nho Bluetooth (khong giai phong luc khoi dong)
  (void)btStarted();

  // NVS: BLE can bo nho NVS de luu hieu chuan radio
  esp_err_t nvsErr = nvs_flash_init();
  if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvsErr = nvs_flash_init();
  }
  Serial.printf("boot: nvs err=%d\n", (int)nvsErr);

  // Khoi tao BLE truoc khi man hinh chiem bo nho
  Serial.println("boot: init ble");
  NimBLEDevice::init("cyd-music");
  NimBLEDevice::setMTU(185);
  Serial.printf("boot: ble ok, heap free=%u max=%u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  Serial.println("boot: init tft");
  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);
  Serial.println("boot: init touch");
  touchSPI.begin(25, 39, 32, 33);
  ts.begin(touchSPI);
  ts.setRotation(SCREEN_ROTATION);
  Serial.println("boot: sprite");
  titleSpr.createSprite(460, 30);
  drawPage();
  Serial.println("boot: start ble task");
// Tạm tắt BLE để kiểm tra nguyên nhân bootloop
// xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 1, nullptr, 0);

  Serial.println("BOOT TEST: BLE DISABLED");
 // xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  uint32_t now = millis();
  static uint8_t lastBle = 255;

  if (bleState != lastBle) {
    lastBle = bleState;
    dMode = true;
    if (bleState == 2) { tOv = 0; tSt = 0; }
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
