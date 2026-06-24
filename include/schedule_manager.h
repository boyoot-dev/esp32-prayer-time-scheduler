#pragma once

#include <Arduino.h>

#include "app_config.h"

struct DailySchedule
{
    int prayerMins[PRAYER_COUNT];
    bool loaded;
};

extern DailySchedule schedule;

bool loadTodaySchedule(int targetMonth, int targetDay);
String formatMinsToTime(int totalMins);
