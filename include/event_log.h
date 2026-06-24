#pragma once

#include <Arduino.h>
#include <RTClib.h>

void appendEventLog(const String &message);
void appendEventLog(const DateTime &time, const String &message);
String readEventLog();
