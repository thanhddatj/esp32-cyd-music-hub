#pragma once
#include <Arduino.h>

/*
 * ota_manager.h
 * ─────────────
 * OTA qua WiFi:
 *   - Bật WiFi Access Point "MusicHub-OTA" (hoặc join mạng nhà)
 *   - Web server tại 192.168.4.1 (AP mode)
 *   - Upload file .bin từ trình duyệt → tự flash → reboot
 *
 * Dùng thư viện:
 *   ESPAsyncWebServer  (me-no-dev/ESPAsyncWebServer)
 *   AsyncTCP           (me-no-dev/AsyncTCP)
 *   Update             (built-in ESP-IDF)
 */

enum class OtaState : uint8_t {
  IDLE,          // chưa khởi động
  STARTING,      // đang bật WiFi
  READY,         // server đang chạy, chờ upload
  UPLOADING,     // đang nhận file
  FLASHING,      // đang ghi flash
  SUCCESS,       // xong, sắp reboot
  ERROR,         // lỗi
};

// Khởi động WiFi AP + web server OTA
// ssid/pass: tên và mật khẩu AP muốn tạo
void ota_start(const char* ssid = "MusicHub-OTA",
               const char* pass = "12345678");

// Dừng server + tắt WiFi
void ota_stop();

OtaState ota_get_state();

// Tiến độ upload (0-100), chỉ hợp lệ khi state == UPLOADING
uint8_t  ota_get_progress();

// IP của AP (để hiện lên màn hình)
String   ota_get_ip();

// Gọi từ loop() – xử lý reboot sau khi flash xong
void     ota_update();
