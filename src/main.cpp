/*
 * Music Hub – ESP32-3248S035 (ILI9488, 480×320)
 * -----------------------------------------------
 * Module layout
 *   ui_welcome.h/cpp   – splash / welcome screen
 *   ui_scan.h/cpp      – BLE scan + device list
 *   ui_player.h/cpp    – Now Playing + transport controls
 *   ui_settings.h/cpp  – Settings screen (EQ, etc.)
 *   ble_manager.h/cpp  – BLE central logic (KBox AB-service)
 *   touch.h/cpp        – XPT2046 touch helper
 *   theme.h            – shared colours, fonts, layout consts
 *
 * No Wi-Fi / FreeRTOS / weather code in this build.
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "theme.h"
#include "touch.h"
#include "ble_manager.h"
#include "main_states.h"
#include "ui_welcome.h"
#include "ui_scan.h"
#include "ui_player.h"
#include "ui_settings.h"

// ── Hardware ────────────────────────────────────────────────
static constexpr int BACKLIGHT_PIN = 21;

// ── Shared display ──────────────────────────────────────────
TFT_eSPI tft;

static AppState currentState  = AppState::WELCOME;
static AppState previousState = AppState::WELCOME; // for back-navigation

// ── Forward declarations ─────────────────────────────────────
void transitionTo(AppState next);

// ── State-enter dispatchers ──────────────────────────────────
void enterState(AppState s) {
  switch (s) {
    case AppState::WELCOME:  ui_welcome_enter(tft);              break;
    case AppState::SCAN:     ui_scan_enter(tft);                 break;
    case AppState::PLAYER:   ui_player_enter(tft);               break;
    case AppState::SETTINGS: ui_settings_enter(tft, previousState); break;
  }
}

// ── State-update dispatchers (called every loop tick) ────────
void updateState(AppState s) {
  switch (s) {
    case AppState::WELCOME: {
      AppState next = ui_welcome_update(tft);
      if (next != AppState::WELCOME) transitionTo(next);
      break;
    }
    case AppState::SCAN: {
      AppState next = ui_scan_update(tft);
      if (next == AppState::PLAYER) transitionTo(AppState::PLAYER);
      break;
    }
    case AppState::PLAYER: {
      AppState next = ui_player_update(tft);
      if (next == AppState::SCAN)     transitionTo(AppState::SCAN);
      if (next == AppState::SETTINGS) { previousState = AppState::PLAYER; transitionTo(AppState::SETTINGS); }
      break;
    }
    case AppState::SETTINGS: {
      AppState next = ui_settings_update(tft);
      if (next != AppState::SETTINGS) transitionTo(next); // back to previousState
      break;
    }
  }
}

void transitionTo(AppState next) {
  currentState = next;
  enterState(next);
}

// ── setup ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("[BOOT] Music Hub starting");

  // Backlight
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  // TFT
  tft.init();
  tft.setRotation(1);  // landscape 480×320
  tft.fillScreen(TFT_BLACK);
  Serial.println("[BOOT] TFT ready");

  // Touch
  touch_init();
  Serial.println("[BOOT] Touch ready");

  // BLE manager (does not start scanning yet)
  ble_manager_init();
  Serial.println("[BOOT] BLE manager ready");

  // Enter first screen
  enterState(AppState::WELCOME);
  Serial.println("[BOOT] Welcome screen drawn");
}

// ── loop ─────────────────────────────────────────────────────
void loop() {
  updateState(currentState);
  delay(10);  // ~100 Hz poll; adjust if needed
}
