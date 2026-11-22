#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

// ESP32-3248S035: test firmware only.
// No BLE, Wi-Fi, touch controller, FreeRTOS task, or DSP code.

static constexpr int BACKLIGHT_PIN = 21;
TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[BOOT] CYD 3.5 Hello test starting");

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);
  Serial.println("[BOOT] Backlight enabled");

  Serial.println("[BOOT] Initializing TFT (ILI9488, 480x320)...");
  tft.init();
  Serial.println("[BOOT] TFT init returned");

  tft.setRotation(1); // Landscape: 480 x 320
  tft.fillScreen(TFT_BLACK);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("HELLO", 240, 145, 4);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("ESP32-3248S035", 240, 190, 2);

  Serial.println("[BOOT] HELLO rendered");
}

void loop() {
  // Deliberately empty: isolate display initialization.
  delay(1000);
}
