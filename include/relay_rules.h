#pragma once

#include "app_config.h"

struct RelayRule
{
    int prayerIndex;
    int offsetMins;
    int targetState;
};

extern RelayRule relayRules[RELAY_COUNT][MAX_RULES];

void setupRelayPins();
void setRelayState(int relayIndex, int targetState);
int getRelayState(int relayIndex);
void clearAllRules();
void saveRulesToStorage();
void loadRulesFromStorage();
void applyRelayRules(int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT]);