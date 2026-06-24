#include <LittleFS.h>
#include <RTClib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_task_wdt.h>

#include "app_config.h"
#include "event_log.h"
#include "relay_rules.h"
#include "schedule_manager.h"
#include "time_safety.h"
#include "web_ui.h"

RTC_DS3231 rtc;
WebServer server(80);
bool automationEnabled = false;

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
    if (automationEnabled)
        loadScheduleForDate(now, true);
    else
        holdRelaysOff(now);

    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("SoftAP IP: ");
    Serial.println(WiFi.softAPIP());
    configureWebServer(server, rtc, &automationEnabled);
    server.begin();
    Serial.println("Web server started");
}

void loop()
{
    feedWatchdog();
    server.handleClient();
    feedWatchdog();

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 1000)
        return;

    lastCheck = millis();
    DateTime now = rtc.now();

    static bool loggedInvalidRtc = false;
    if (!isRtcTimeSane(now))
    {
        if (!loggedInvalidRtc)
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


