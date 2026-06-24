#pragma once

#include <RTClib.h>
#include <WebServer.h>

void configureWebServer(WebServer &server, RTC_DS3231 &rtc);
