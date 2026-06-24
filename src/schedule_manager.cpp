#include "schedule_manager.h"

#include <LittleFS.h>

DailySchedule schedule = {{0, 0, 0, 0, 0}, false};

bool loadTodaySchedule(int targetMonth, int targetDay)
{
    if (!LittleFS.exists("/schedule.csv"))
    {
        schedule.loaded = false;
        return false;
    }

    File file = LittleFS.open("/schedule.csv", "r");
    if (!file)
    {
        schedule.loaded = false;
        return false;
    }

    schedule.loaded = false;
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.replace("\r", "");
        line.trim();
        if (line.length() == 0 || line.startsWith("-") || line.indexOf("Content-") != -1)
            continue;

        int values[7] = {0};
        int startPos = 0;
        int idx = 0;
        bool corrupt = false;

        for (int i = 0; i < 6; i++)
        {
            int commaPos = line.indexOf(',', startPos);
            if (commaPos == -1)
            {
                corrupt = true;
                break;
            }
            values[idx++] = line.substring(startPos, commaPos).toInt();
            startPos = commaPos + 1;
        }
        values[idx] = line.substring(startPos).toInt();

        if (!corrupt && values[0] == targetMonth && values[1] == targetDay)
        {
            for (int p = 0; p < PRAYER_COUNT; p++)
                schedule.prayerMins[p] = values[p + 2];
            schedule.loaded = true;
            break;
        }
    }

    file.close();
    return schedule.loaded;
}

String formatMinsToTime(int totalMins)
{
    int h = totalMins / 60;
    int m = totalMins % 60;
    return (h < 10 ? "0" : "") + String(h) + ":" + (m < 10 ? "0" : "") + String(m);
}
