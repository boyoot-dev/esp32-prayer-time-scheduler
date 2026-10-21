#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "app_config.h"

struct RelayRule
{
    int prayerIndex;
    int offsetMins;
    int targetState;
};

enum RelayMode : uint8_t
{
    RELAY_MODE_AUTO = 0,
    RELAY_MODE_MANUAL_ON = 1,
    RELAY_MODE_MANUAL_OFF = 2
};

extern RelayRule relayRules[RELAY_COUNT][MAX_RULES];
extern RelayMode relayModes[RELAY_COUNT];

void setupRelayPins();
void setRelayState(int relayIndex, int targetState, bool persist = true);
int getRelayState(int relayIndex);
const char *relayModeLabel(RelayMode mode);
RelayMode getRelayMode(int relayIndex);
void setRelayMode(int relayIndex, RelayMode mode);
// Sets a relay's mode and applies it now; `source` ("web", "ThingsBoard") goes in the event log.
void changeRelayMode(int relayIndex, RelayMode mode, int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], const DateTime &now, bool automationEnabled, const char *source);
void loadRelaySettingsFromStorage();
void saveRelaySettingsToStorage();
void applyRelayStateForModeAtTime(int relayIndex, int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], const DateTime &now, bool automationEnabled);
void restoreRelayStatesForNow(int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], const DateTime &now, bool automationEnabled);
void clearAllRules();
void saveRulesToStorage();
void loadRulesFromStorage();
void applyRelayRules(int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], const DateTime &now, bool automationEnabled);
