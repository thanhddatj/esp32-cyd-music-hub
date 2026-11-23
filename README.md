# Music Hub – ESP32-3248S035

Firmware điều khiển nhạc qua BLE cho DSP KBox (service AB00).

---

## Luồng màn hình

```
[BOOT]
  │
  ▼
┌─────────────────┐  2.8s / tap   ┌──────────────────┐
│  WELCOME        │──────────────▶│  SCAN            │
│  Splash + bar   │               │  [SCAN] button   │
└─────────────────┘               │  Device list     │
                                  └────────┬─────────┘
                                           │ tap device
                                           ▼
                                  ┌──────────────────┐
                                  │  PLAYER          │
                                  │  Art / Track info│
                                  │  << < ▶/⏸ > >> │◀─────┐
                                  │  [⚙]             │      │ back
                                  └────────┬─────────┘      │
                                           │ tap ⚙          │
                                           ▼                 │
                                  ┌──────────────────┐       │
                                  │  SETTINGS        │       │
                                  │  [Equaliser]     │       │
                                  │  [Device]        │───────┘
                                  │  [About]         │
                                  └──────────────────┘
```

---

## Cấu trúc module

```
src/
├── main.cpp          – AppState machine, setup(), loop()
├── main_states.h     – enum AppState (shared)
├── theme.h           – màu, layout, font IDs
├── touch.h/cpp       – XPT2046 wrapper, debounce
├── ble_manager.h/cpp – BLE central, KBox AB-service
├── ui_welcome.h/cpp  – màn hình chào
├── ui_scan.h/cpp     – quét BLE + danh sách
├── ui_player.h/cpp   – now playing + transport
└── ui_settings.h/cpp – EQ sliders + Device + About
```

---

## Điều chỉnh cần thiết

### 1. Touch calibration
Chạy sketch `TFT_eSPI/examples/Calibrate/Touch_calibrate` một lần,  
copy 5 số vào `touch.cpp → calData[]`.

### 2. BLE command bytes
File `ble_manager.cpp` → `ble_cmd_play/pause/next/prev/eq_set()`  
Thay byte `{0x05, 0x01}` ... bằng lệnh thực từ tài liệu DSP山景 BP10.

### 3. Notify parser
Hàm `notifyCallback()` trong `ble_manager.cpp` parse chuỗi `title\0artist\0album\0state`.  
Điều chỉnh theo format thực tế notify AB03 của KBox.

### 4. SPI pins
Kiểm tra lại pin mapping trong `platformio.ini` với board thực tế của bạn.

---

## Build

```bash
pio run -t upload
pio device monitor
```
