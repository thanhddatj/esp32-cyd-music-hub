#pragma once
/*
 * main_states.h
 * Declares AppState so UI modules can return it
 * without depending on main.cpp.
 */
enum class AppState : uint8_t {
  WELCOME,
  SCAN,
  PLAYER,
  SETTINGS,
  OTA,       // OTA firmware update screen
};
