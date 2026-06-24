#include <LittleFS.h>
#include <RTClib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include "app_config.h"
#include "relay_rules.h"
#include "schedule_manager.h"
#include "web_ui.h"

RTC_DS3231 rtc;
WebServer server(80);

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

void initializeRtc()
{
    if (rtc.lostPower())
    {
        rtc.adjust(compileTimeForFallback());
        Serial.println("RTC lost power; fallback compile time applied");
    }
    else
    {
        Serial.println("RTC kept existing time");
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Prayer Automation Hub booting...");
    LittleFS.begin(true);
    Serial.println("LittleFS ready");

    setupRelayPins();
    Serial.println("Relay pins ready");
    rtc.begin();
    initializeRtc();
    Serial.println("RTC ready");

    loadRulesFromStorage();
    DateTime now = rtc.now();
    loadTodaySchedule(now.month(), now.day());

    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("SoftAP IP: ");
    Serial.println(WiFi.softAPIP());
    configureWebServer(server, rtc);
    server.begin();
    Serial.println("Web server started");
}

void loop()
{
    server.handleClient();

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 1000)
        return;

    lastCheck = millis();
    DateTime now = rtc.now();

    static int lastDayChecked = -1;
    if (now.day() != lastDayChecked)
    {
        lastDayChecked = now.day();
        loadTodaySchedule(now.month(), now.day());
    }

    if (schedule.loaded)
    {
        int currentMinsPastMidnight = (now.hour() * 60) + now.minute();
        applyRelayRules(currentMinsPastMidnight, schedule.prayerMins);
    }
}