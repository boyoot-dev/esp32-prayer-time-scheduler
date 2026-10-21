#pragma once

#include <RTClib.h>

// The RTC keeps local standard time (UTC+2); the ESP32 system clock keeps UTC, which
// TLS certificate checks read.

// Call once at boot with whether rtc.begin() found the DS3231.
void setRtcPresent(bool present);

// Current local standard time: the RTC when it reads sane, otherwise the system clock
// once NTP or a browser sync has set it (RTC missing or dead), otherwise the RTC's
// insane reading.
DateTime currentTime(RTC_DS3231 &rtc);
// True when currentTime() last fell back to the system clock because the RTC was unusable.
bool usingNtpFallback();

// Copies trusted RTC time into the system clock, so TLS works before NTP answers.
void setSystemClockFromRtc(const DateTime &rtcNow);

// Starts SNTP. Call after WiFi mode is set.
void beginNtp();
bool hasNtpSynced();

// Returns true once per completed NTP sync, with the synced time in local standard time.
// SNTP runs in the network stack's task; only the loop task touches the RTC.
bool takeNtpSync(DateTime &localStandardNow);

// Sets the RTC and system clock to a trusted time from `source`, reloads today's
// schedule, re-evaluates automation and restores relay states for the new time.
void applyClockSync(RTC_DS3231 &rtc, bool &automationEnabled, const DateTime &synced, const char *source);
