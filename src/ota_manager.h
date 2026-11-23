#pragma once
#include <Arduino.h>

/*
 * ota_manager.h  –  v2 (synchronous WebServer, BLE-safe)
 * ────────────────────────────────────────────────────────
 * Dùng WebServer built-in (synchronous, BLE-safe).
 * handleClient() được gọi từ ui_ota_update() mỗi tick.
 * Không có callback từ thread khác → không xung đột BLE/TCP.
 */

enum class OtaState : uint8_t {
  IDLE,        // chưa khởi động
  READY,       // WiFi AP + server đang chạy, chờ upload
  UPLOADING,   // đang nhận + ghi flash (trong handleClient)
  SUCCESS,     // flash xong, chờ reboot
  ERROR,       // lỗi flash
};

// Khởi động WiFi AP + web server
// Gọi một lần khi vào màn hình OTA
void ota_start(const char* ssid = "MusicHub-OTA",
               const char* pass = "12345678");

// Dừng server + tắt WiFi
// Gọi khi thoát màn hình OTA
void ota_stop();

// Gọi từ loop mỗi tick – xử lý HTTP request + reboot nếu cần
// Trả về true nếu đang bận xử lý upload (để UI biết không vẽ)
bool ota_tick();

OtaState ota_get_state();
uint8_t  ota_get_progress();   // 0-100
String   ota_get_ip();
const char* ota_get_error();
