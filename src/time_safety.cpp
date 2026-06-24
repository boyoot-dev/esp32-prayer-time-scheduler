#include "time_safety.h"

#include "app_config.h"

namespace
{
String twoDigits(int value)
{
    return (value < 10 ? "0" : "") + String(value);
}
}

bool isRtcTimeSane(const DateTime &time)
{
    if (time.year() < RTC_MIN_VALID_YEAR || time.year() > RTC_MAX_VALID_YEAR)
        return false;
    if (time.month() < 1 || time.month() > 12)
        return false;
    if (time.day() < 1 || time.day() > 31)
        return false;
    if (time.hour() < 0 || time.hour() > 23)
        return false;
    if (time.minute() < 0 || time.minute() > 59)
        return false;
    if (time.second() < 0 || time.second() > 59)
        return false;

    return true;
}

String formatTimestamp(const DateTime &time)
{
    return String(time.year()) + "-" + twoDigits(time.month()) + "-" + twoDigits(time.day()) + " " + twoDigits(time.hour()) + ":" + twoDigits(time.minute()) + ":" + twoDigits(time.second());
}
