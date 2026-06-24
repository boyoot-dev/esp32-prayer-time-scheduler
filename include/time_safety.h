#pragma once

#include <Arduino.h>
#include <RTClib.h>

bool isRtcTimeSane(const DateTime &time);
String formatTimestamp(const DateTime &time);
