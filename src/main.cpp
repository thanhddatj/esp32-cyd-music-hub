#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

static constexpr int BACKLIGHT_PIN = 21;

// Chỉnh lại đúng chân XPT2046 trên board 3248S035 của bạn nếu khác
static constexpr int TOUCH_CS  = 33;
static constexpr int TOUCH_IRQ = 36; // Có thể dùng -1 nếu không nối chân IRQ

// Thay bằng thông tin thật của loa (kiểm tra qua nRF Connect)
static const char* TARGET_NAME_PREFIX = "Oldwu";
static BLEUUID SERVICE_UUID("0000ffe0-0000-1000-8000-00805f9b34fb");
static BLEUUID CHAR_NOTIFY_UUID("0000ffe1-0000-1000-8000-00805f9b34fb");
static BLEUUID CHAR_WRITE_UUID("0000ffe2-0000-1000-8000-00805f9b34fb");

TFT_eSPI tft;
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

static BLEAdvertisedDevice* targetDevice = nullptr;
static BLERemoteCharacteristic* pWriteChar = nullptr;
static bool doConnect = false;
static bool isConnected = false;
static bool uiDirty = true;

String songTitle = "Dang do loa BLE...";
String connStatus = "Chua ket noi";

struct Button {
  int x, y, w, h;
  const char* label;
  uint16_t color;
};

Button btnPrev = {30,  220, 125, 70, "<< PREV", TFT_DARKGREY};
Button btnPlay = {177, 220, 126, 70, "PLAY/PAUSE", TFT_BLUE};
Button btnNext = {325, 220, 125, 70, "NEXT >>", TFT_DARKGREY};

void drawButton(const Button& b, uint16_t fillColor) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 10, fillColor);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 10, TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, fillColor);
  tft.drawString(b.label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

void drawUI() {
  tft.fillRect(0, 45, 480, 155, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);

  tft.setTextColor(isConnected ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  tft.drawString(connStatus, 240, 58, 2);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(songTitle, 240, 105, 4);
}

void sendSpeakerCommand(const char* cmd) {
  if (!isConnected || pWriteChar == nullptr) {
    Serial.println("[BLE] Chua ket noi loa, bo qua lenh");
    return;
  }
  if (pWriteChar->canWrite()) {
    // Thay chuỗi/byte dưới đây bằng giao thức lệnh thật của loa
    pWriteChar->writeValue((uint8_t*)cmd, strlen(cmd), false);
    Serial.printf("[BLE] Da gui lenh: %s\n", cmd);
  }
}

static void notifyCallback(
    BLERemoteCharacteristic* pBLERemoteCharacteristic,
    uint8_t* pData,
    size_t length,
    bool isNotify) {
  String payload((char*)pData, length);
  payload.trim();
  if (payload.length() > 0) {
    songTitle = payload;
    uiDirty = true;
    Serial.printf("[BLE] Nhan du lieu: %s\n", songTitle.c_str());
  }
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) override {
    isConnected = true;
    connStatus = "Da ket noi loa";
    uiDirty = true;
    Serial.println("[BLE] Connected to speaker");
  }

  void onDisconnect(BLEClient* pclient) override {
    isConnected = false;
    pWriteChar = nullptr;
    connStatus = "Mat ket noi - Dang quet lai...";
    uiDirty = true;
    Serial.println("[BLE] Disconnected from speaker");
  }
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    String devName = advertisedDevice.haveName()
                         ? String(advertisedDevice.getName().c_str())
                         : String("(khong co ten)");
    String devAddr = String(advertisedDevice.getAddress().toString().c_str());
    int rssi = advertisedDevice.getRSSI();

    Serial.printf("[BLE] Ten: %s | MAC: %s | RSSI: %d dBm",
                  devName.c_str(), devAddr.c_str(), rssi);
    if (advertisedDevice.haveServiceUUID()) {
      Serial.printf(" | UUID: %s", advertisedDevice.getServiceUUID().toString().c_str());
    }
    Serial.println();
  }
};

bool connectToSpeaker() {
  if (targetDevice == nullptr) return false;

  BLEClient* pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  if (!pClient->connect(targetDevice)) {
    Serial.println("[BLE] Ket noi that bai");
    return false;
  }

  BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.println("[BLE] Khong tim thay Service UUID");
    pClient->disconnect();
    return false;
  }

  BLERemoteCharacteristic* pNotifyChar = pRemoteService->getCharacteristic(CHAR_NOTIFY_UUID);
  if (pNotifyChar != nullptr && pNotifyChar->canNotify()) {
    pNotifyChar->registerForNotify(notifyCallback);
  }

  pWriteChar = pRemoteService->getCharacteristic(CHAR_WRITE_UUID);
  return true;
}

bool readTouchMapped(int &tx, int &ty) {
  if (!touch.touched()) return false;
  TS_Point p = touch.getPoint();

  // Giá trị raw (0..4095) cần hiệu chỉnh theo thực tế màn hình 480x320 ở rotation(1)
  tx = map(p.x, 300, 3800, 0, 479);
  ty = map(p.y, 300, 3800, 0, 319);
  tx = constrain(tx, 0, 479);
  ty = constrain(ty, 0, 319);
  return true;
}

bool hitButton(const Button& b, int tx, int ty) {
  return (tx >= b.x && tx <= b.x + b.w && ty >= b.y && ty <= b.y + b.h);
}

void setup() {
  Serial.begin(115200);
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touch.begin();
  touch.setRotation(1);

  tft.fillRect(0, 0, 480, 40, TFT_NAVY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString("ESP32 BLE SPEAKER CONTROLLER", 240, 20, 2);

  drawButton(btnPrev, btnPrev.color);
  drawButton(btnPlay, btnPlay.color);
  drawButton(btnNext, btnNext.color);
  drawUI();

  BLEDevice::init("ESP32_Controller");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void loop() {
  if (doConnect) {
    doConnect = false;
    if (connectToSpeaker()) {
      connStatus = "San sang dieu khien";
    } else {
      connStatus = "Loi GATT - Quet lai...";
      BLEDevice::getScan()->start(5, false);
    }
    uiDirty = true;
  }

    if (!isConnected && !doConnect) {
    static unsigned long lastScanMs = 0;
    if (millis() - lastScanMs > 6000) {
      lastScanMs = millis();
      BLEDevice::getScan()->clearResults();
      BLEDevice::getScan()->start(5, false);
    }
  }


  if (uiDirty) {
    uiDirty = false;
    drawUI();
  }

  int tx = -1, ty = -1;
  static unsigned long lastTouchMs = 0;
  if (readTouchMapped(tx, ty) && (millis() - lastTouchMs > 250)) {
    lastTouchMs = millis();
    if (hitButton(btnPrev, tx, ty)) {
      sendSpeakerCommand("PREV");
    } else if (hitButton(btnPlay, tx, ty)) {
      sendSpeakerCommand("TOGGLE");
    } else if (hitButton(btnNext, tx, ty)) {
      sendSpeakerCommand("NEXT");
    }
  }

  delay(20);
}
