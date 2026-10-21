#include <LittleFS.h>
#include <RTClib.h>
#include <WebServer.h>
#include <Wire.h>
#include <esp_task_wdt.h>

#include "app_config.h"
#include "clock_sync.h"
#include "event_log.h"
#include "net.h"
#include "relay_rules.h"
#include "schedule_manager.h"
#include "thingsboard_link.h"
#include "time_safety.h"
#include "web_ui.h"

constexpr unsigned long STATE_REPORT_INTERVAL_MS = 250;

RTC_DS3231 rtc;
WebServer server(80);
bool automationEnabled = false;
bool rtcSane = false;

bool isEgyptDST(int month, int day)
{
    if (month < 4 || month > 10)
        return false;
    if (month > 4 && month < 10)
        return true;
    if (month == 4)
        return day >= 24;
    if (month == 10)
        return day < 29;
    return false;
}

DateTime compileTimeForFallback()
{
    DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
    if (isEgyptDST(compileTime.month(), compileTime.day()))
    {
        TimeSpan oneHour(0, 1, 0, 0);
        return compileTime - oneHour;
    }

    return compileTime;
}

void feedWatchdog()
{
    esp_task_wdt_reset();
}

void initializeWatchdog()
{
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true);
    esp_task_wdt_add(NULL);
    Serial.println("Watchdog ready");
}

void safeStartupDelay()
{
    Serial.println("Startup safety delay active; relays held OFF");
    unsigned long started = millis();
    while (millis() - started < STARTUP_SAFE_DELAY_MS)
    {
        feedWatchdog();
        delay(250);
    }
}

bool initializeRtc()
{
    if (!rtc.begin())
    {
        Serial.println("RTC not found; automation disabled");
        appendEventLog("RTC not found; automation disabled");
        return false;
    }

    if (rtc.lostPower())
    {
        rtc.adjust(compileTimeForFallback());
        Serial.println("RTC lost power; automation disabled until browser time sync");
        appendEventLog("RTC lost power; automation disabled until browser time sync");
        return false;
    }

    DateTime now = rtc.now();
    if (!isRtcTimeSane(now))
    {
        Serial.println("RTC time failed sanity check; automation disabled");
        appendEventLog("RTC time failed sanity check; automation disabled");
        return false;
    }

    Serial.println("RTC ready");
    appendEventLog(now, "RTC ready");
    return true;
}

void holdRelaysOff(const DateTime &now)
{
    for (int r = 0; r < RELAY_COUNT; r++)
        setRelayState(r, RELAY_OFF_STATE);

    appendEventLog(now, "Relay outputs held OFF");
}
void loadScheduleForDate(const DateTime &now, bool restoreRelays)
{
    bool loaded = loadTodaySchedule(now.month(), now.day());
    automationEnabled = loaded && isRtcTimeSane(now);

    if (loaded)
    {
        appendEventLog(now, "Schedule loaded for today");
        if (restoreRelays)
        {
            int currentMinsPastMidnight = (now.hour() * 60) + now.minute();
            restoreRelayStatesForNow(currentMinsPastMidnight, schedule.prayerMins, now, automationEnabled);
        }
    }
    else
    {
        appendEventLog(now, "Schedule missing for today; automation disabled");
        holdRelaysOff(now);
    }
}

void handleRemoteCommand(const RemoteCommand &command)
{
    DateTime now = rtc.now();
    int currentMinsPastMidnight = (now.hour() * 60) + now.minute();
    bool canUseAutomation = automationEnabled && schedule.loaded && isRtcTimeSane(now);

    switch (command.type)
    {
    case REMOTE_SET_RELAY_MODE:
        changeRelayMode(command.relayIndex, static_cast<RelayMode>(command.mode), currentMinsPastMidnight, schedule.prayerMins, now, canUseAutomation, "ThingsBoard");
        break;
    }
}

void reportCurrentState()
{
    DeviceState state = {};
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        state.relayOn[r] = getRelayState(r) == RELAY_ON_STATE;
        state.relayMode[r] = getRelayMode(r);
    }
    state.rtcOk = rtcSane;
    state.scheduleLoaded = schedule.loaded;
    state.automationEnabled = automationEnabled && schedule.loaded && rtcSane;
    for (int p = 0; p < PRAYER_COUNT; p++)
        state.prayerMins[p] = schedule.prayerMins[p];
    reportDeviceState(state);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Prayer Automation Hub booting...");

    bool fsReady = LittleFS.begin(true);
    if (fsReady)
    {
        Serial.println("LittleFS ready");
        appendEventLog("Boot");
    }
    else
    {
        Serial.println("LittleFS failed");
    }

    setupRelayPins();
    Serial.println("Relay pins ready and OFF");

    initializeWatchdog();
    safeStartupDelay();

    loadRelaySettingsFromStorage();
    loadRulesFromStorage();

    automationEnabled = initializeRtc();
    DateTime now = rtc.now();
    rtcSane = automationEnabled;
    if (automationEnabled)
    {
        setSystemClockFromRtc(now);
        loadScheduleForDate(now, true);
    }
    else
    {
        holdRelaysOff(now);
    }

    beginNetwork();
    configureWebServer(server, rtc, &automationEnabled);
    server.begin();
    Serial.println("Web server started");
}

void loop()
{
    feedWatchdog();
    server.handleClient();
    feedWatchdog();

    RemoteCommand command;
    while (takeRemoteCommand(command))
        handleRemoteCommand(command);

    static unsigned long lastReport = 0;
    if (millis() - lastReport >= STATE_REPORT_INTERVAL_MS)
    {
        lastReport = millis();
        reportCurrentState();
    }

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 1000)
        return;

    lastCheck = millis();
    DateTime now = rtc.now();

    DateTime ntpNow;
    if (takeNtpSync(ntpNow) && isRtcTimeSane(ntpNow))
    {
        uint32_t rtcSec = now.unixtime();
        uint32_t ntpSec = ntpNow.unixtime();
        uint32_t driftSec = rtcSec > ntpSec ? rtcSec - ntpSec : ntpSec - rtcSec;
        if (driftSec >= 2)
        {
            applyClockSync(rtc, automationEnabled, ntpNow, "NTP");
            now = ntpNow;
        }
    }

    rtcSane = isRtcTimeSane(now);
    static bool loggedInvalidRtc = false;
    if (!rtcSane)
    {
        // Also catches a time sync that re-enabled automation while the RTC is unreadable.
        if (!loggedInvalidRtc || automationEnabled)
        {
            loggedInvalidRtc = true;
            automationEnabled = false;
            holdRelaysOff(now);
        }
        return;
    }
    loggedInvalidRtc = false;

    static int lastDayChecked = -1;
    if (automationEnabled && now.day() != lastDayChecked)
    {
        lastDayChecked = now.day();
        loadScheduleForDate(now, true);
    }

    if (automationEnabled && schedule.loaded)
    {
        int currentMinsPastMidnight = (now.hour() * 60) + now.minute();
        static int lastAppliedMinute = -1;
        if (currentMinsPastMidnight != lastAppliedMinute)
        {
            lastAppliedMinute = currentMinsPastMidnight;
            applyRelayRules(currentMinsPastMidnight, schedule.prayerMins, now, automationEnabled);
        }
    }
}


