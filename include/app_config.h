#pragma once

#include <Arduino.h>

// The setup AP stays up permanently, alongside the WiFi station connection.
constexpr const char *AP_SSID = "PrayerTimer-Setup";
constexpr const char *AP_PASSWORD = "password123";
constexpr const char *NETWORK_HOSTNAME = "prayertimer";

// The DS3231 keeps Egypt standard time (UTC+2) all year; the schedule CSV and rules use it too.
constexpr long TIME_UTC_OFFSET_SECONDS = 2 * 3600;

constexpr int RELAY_COUNT = 4;
constexpr int PRAYER_COUNT = 5;
constexpr int MAX_RULES = 10;

constexpr int RELAY_PINS[RELAY_COUNT] = {2, 4, 5, 18};
constexpr int RELAY_ON_STATE = LOW;
constexpr int RELAY_OFF_STATE = HIGH;

constexpr unsigned long STARTUP_SAFE_DELAY_MS = 10000;
// A TLS handshake verifies the server's P-384 chain in software and holds core 0 for ~7 s.
constexpr int WATCHDOG_TIMEOUT_SECONDS = 30;

constexpr int RTC_MIN_VALID_YEAR = 2024;
constexpr int RTC_MAX_VALID_YEAR = 2035;
constexpr int EVENT_LOG_MAX_ENTRIES = 100;
