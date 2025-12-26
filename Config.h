#pragma once
#include <Arduino.h>

// Pins
constexpr uint8_t NEOPIXEL_PIN = 8;
constexpr uint8_t BUTTON_PIN = BOOT_PIN;

// Preference Keys
const char* PREF_NAMESPACE = "MatterPrefs";
const char* KEY_TARGET_TEMP = "TargetTemp";
const char* KEY_STATE = "State";
const char* KEY_MODE = "Mode";

// Defaults & Ranges
constexpr double TEMP_DEFAULT = 22.0;
constexpr double TEMP_MIN = 5.0;
constexpr double TEMP_MAX = 60.0;
constexpr double TEMP_STEP = 0.5;
