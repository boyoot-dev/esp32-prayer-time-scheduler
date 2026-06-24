#pragma once

#include <Arduino.h>

constexpr const char *WIFI_SSID = "PrayerTimer-Setup";
constexpr const char *WIFI_PASSWORD = "password123";

constexpr int RELAY_COUNT = 4;
constexpr int PRAYER_COUNT = 5;
constexpr int MAX_RULES = 10;

constexpr int RELAY_PINS[RELAY_COUNT] = {2, 4, 5, 18};
constexpr int RELAY_ON_STATE = LOW;
constexpr int RELAY_OFF_STATE = HIGH;

constexpr unsigned long STARTUP_SAFE_DELAY_MS = 10000;
constexpr int WATCHDOG_TIMEOUT_SECONDS = 15;

constexpr int RTC_MIN_VALID_YEAR = 2024;
constexpr int RTC_MAX_VALID_YEAR = 2035;
constexpr int EVENT_LOG_MAX_ENTRIES = 100;
