#include "relay_rules.h"

#include <Arduino.h>
#include <LittleFS.h>

RelayRule relayRules[RELAY_COUNT][MAX_RULES];

void setupRelayPins()
{
    for (int i = 0; i < RELAY_COUNT; i++)
    {
        pinMode(RELAY_PINS[i], OUTPUT);
        setRelayState(i, HIGH);
    }
}

void setRelayState(int relayIndex, int targetState)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return;

    digitalWrite(RELAY_PINS[relayIndex], targetState == LOW ? LOW : HIGH);
}

int getRelayState(int relayIndex)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return HIGH;

    return digitalRead(RELAY_PINS[relayIndex]);
}

void clearAllRules()
{
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        for (int i = 0; i < MAX_RULES; i++)
        {
            relayRules[r][i].prayerIndex = -1;
            relayRules[r][i].offsetMins = 0;
            relayRules[r][i].targetState = HIGH;
        }
    }
}

void saveRulesToStorage()
{
    File file = LittleFS.open("/rules.bin", "w");
    if (file)
    {
        file.write((uint8_t *)relayRules, sizeof(relayRules));
        file.close();
    }
}

void loadRulesFromStorage()
{
    clearAllRules();

    if (!LittleFS.exists("/rules.bin"))
        return;

    File file = LittleFS.open("/rules.bin", "r");
    if (file)
    {
        file.read((uint8_t *)relayRules, sizeof(relayRules));
        file.close();
    }
}

void applyRelayRules(int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT])
{
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        for (int i = 0; i < MAX_RULES; i++)
        {
            RelayRule rule = relayRules[r][i];
            if (rule.prayerIndex >= 0 && rule.prayerIndex < PRAYER_COUNT)
            {
                int target = prayerTimes[rule.prayerIndex] + rule.offsetMins;
                if (currentMinsPastMidnight == target)
                    setRelayState(r, rule.targetState);
            }
        }
    }
}