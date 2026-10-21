#include "clock_sync.h"

#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "event_log.h"
#include "relay_rules.h"
#include "schedule_manager.h"
#include "time_safety.h"

namespace
{
volatile bool ntpSyncPending = false;
volatile bool ntpEverSynced = false;
bool rtcPresent = false;
bool ntpFallback = false;
// Set once NTP or a browser sync has set the system clock since boot.
volatile bool systemClockTrusted = false;

void onNtpSync(struct timeval *)
{
    ntpEverSynced = true;
    systemClockTrusted = true;
    ntpSyncPending = true;
}
}

void setRtcPresent(bool present)
{
    rtcPresent = present;
}

DateTime currentTime(RTC_DS3231 &rtc)
{
    // Skip the I2C read when there is no RTC: each failed read logs an error.
    DateTime rtcNow = rtcPresent ? rtc.now() : DateTime(static_cast<uint32_t>(0));
    ntpFallback = !isRtcTimeSane(rtcNow) && systemClockTrusted;
    if (ntpFallback)
        return DateTime(static_cast<uint32_t>(time(nullptr) + TIME_UTC_OFFSET_SECONDS));
    return rtcNow;
}

bool usingNtpFallback()
{
    return ntpFallback;
}

void setSystemClockFromRtc(const DateTime &rtcNow)
{
    struct timeval tv = {static_cast<time_t>(rtcNow.unixtime() - TIME_UTC_OFFSET_SECONDS), 0};
    settimeofday(&tv, nullptr);
}

void beginNtp()
{
    sntp_set_time_sync_notification_cb(onNtpSync);
    configTime(0, 0, "pool.ntp.org", "time.google.com");
}

bool hasNtpSynced()
{
    return ntpEverSynced;
}

bool takeNtpSync(DateTime &localStandardNow)
{
    if (!ntpSyncPending)
        return false;

    ntpSyncPending = false;
    localStandardNow = DateTime(static_cast<uint32_t>(time(nullptr) + TIME_UTC_OFFSET_SECONDS));
    return true;
}

void applyClockSync(RTC_DS3231 &rtc, bool &automationEnabled, const DateTime &synced, const char *source)
{
    DateTime before = currentTime(rtc);
    bool wasEnabled = automationEnabled;
    uint32_t beforeSec = before.unixtime();
    uint32_t syncedSec = synced.unixtime();
    uint32_t deltaSec = beforeSec > syncedSec ? beforeSec - syncedSec : syncedSec - beforeSec;

    if (rtcPresent)
        rtc.adjust(synced);
    setSystemClockFromRtc(synced);
    systemClockTrusted = true;

    bool loaded = loadTodaySchedule(synced.month(), synced.day());
    automationEnabled = isRtcTimeSane(synced) && loaded;
    restoreRelayStatesForNow((synced.hour() * 60) + synced.minute(), schedule.prayerMins, synced, automationEnabled);

    if (automationEnabled != wasEnabled || deltaSec > 120)
        appendEventLog(synced, String("Time synced from ") + source + "; automation " + (automationEnabled ? "enabled" : "disabled"));
}
