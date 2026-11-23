#pragma once
/*
 * theme.h – shared visual tokens for all UI modules
 * Screen: 480 × 320  (landscape)
 */

// ── Palette ──────────────────────────────────────────────────
// Dark base (OLED-style)
#define CLR_BG          0x0841   // near-black   #101021
#define CLR_SURFACE     0x10A3   // dark panel   #2124..
#define CLR_BORDER      0x2965   // subtle line  #294C..

// Accent – deep cyan / teal  (music/audio vibe)
#define CLR_ACCENT      0x07FF   // TFT_CYAN  → swap to 0x0575 for a softer teal
#define CLR_ACCENT_DIM  0x03EF   // dimmed accent for inactive items

// Text
#define CLR_TEXT_PRI    0xFFFF   // TFT_WHITE
#define CLR_TEXT_SEC    0xC618   // light grey
#define CLR_TEXT_DIM    0x7BEF   // dim grey (sub-labels)

// Status / feedback
#define CLR_OK          0x07E0   // green
#define CLR_WARN        0xFD20   // orange
#define CLR_ERR         0xF800   // red
#define CLR_CONNECTED   0x07E0   // same as OK – used for BLE dot

// Buttons
#define CLR_BTN_BG      0x2124   // button fill
#define CLR_BTN_PRESS   0x07FF   // pressed highlight (accent)
#define CLR_BTN_BORDER  0x07FF   // accent border

// ── Layout ───────────────────────────────────────────────────
#define SCREEN_W        480
#define SCREEN_H        320

// Status / top bar
#define TOPBAR_H        28
#define TOPBAR_Y        0

// Bottom nav bar (transport + gear)
#define BOTBAR_H        48
#define BOTBAR_Y        (SCREEN_H - BOTBAR_H)

// Content area between top and bottom bars
#define CONTENT_Y       TOPBAR_H
#define CONTENT_H       (SCREEN_H - TOPBAR_H - BOTBAR_H)

// ── Font IDs (TFT_eSPI built-in) ─────────────────────────────
// 1 = 7px,  2 = 11px,  4 = 19px,  6 = 39px (digits),  7 = 48px (7-seg)
#define FONT_SMALL      2
#define FONT_MED        4
#define FONT_LARGE      6

// ── Touch debounce (ms) ──────────────────────────────────────
#define TOUCH_DEBOUNCE_MS  180
