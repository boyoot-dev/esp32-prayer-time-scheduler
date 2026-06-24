#include "event_log.h"

#include <LittleFS.h>

#include "app_config.h"
#include "time_safety.h"

namespace
{
const char *EVENT_LOG_PATH = "/events.log";

String normalizedLine(const String &line)
{
    String clean = line;
    clean.replace("\r", "");
    clean.replace("\n", "");
    clean.trim();
    return clean;
}

void appendLine(const String &line)
{
    String entries[EVENT_LOG_MAX_ENTRIES];
    int count = 0;

    if (LittleFS.exists(EVENT_LOG_PATH))
    {
        File file = LittleFS.open(EVENT_LOG_PATH, "r");
        while (file && file.available())
        {
            String oldLine = normalizedLine(file.readStringUntil('\n'));
            if (oldLine.length() == 0)
                continue;

            if (count >= EVENT_LOG_MAX_ENTRIES - 1)
            {
                for (int i = 1; i < count; i++)
                    entries[i - 1] = entries[i];
                count--;
            }

            entries[count++] = oldLine;
        }
        if (file)
            file.close();
    }

    entries[count++] = normalizedLine(line);

    File file = LittleFS.open(EVENT_LOG_PATH, "w");
    if (!file)
        return;

    for (int i = 0; i < count; i++)
        file.println(entries[i]);

    file.close();
}
}

void appendEventLog(const String &message)
{
    appendLine(String("BOOT - ") + message);
}

void appendEventLog(const DateTime &time, const String &message)
{
    if (isRtcTimeSane(time))
        appendLine(formatTimestamp(time) + " - " + message);
    else
        appendEventLog(message);
}

String readEventLog()
{
    if (!LittleFS.exists(EVENT_LOG_PATH))
        return "";

    File file = LittleFS.open(EVENT_LOG_PATH, "r");
    if (!file)
        return "";

    String contents;
    while (file.available())
        contents += file.readStringUntil('\n') + "\n";

    file.close();
    return contents;
}
