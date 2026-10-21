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

// Replaces one relay's rules with the first `count` of `rules` (not saved to flash).
void setRelayRules(int relayIndex, const RelayRule rules[MAX_RULES], int count);

// True if an AUTO relay has a rule due in the next `windowMins` minutes.
bool hasAutoRuleWithin(int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], int windowMins);

// Compact text form shared with ThingsBoard: "<prayer><+/-offset>:<1 on|0 off>" joined by
// ';', e.g. "1+10:1;1+20:0" = ON at Dhuhr+10, OFF at Dhuhr+20 (prayer 0-4 = Fajr..Isha).
// Short because TbLink publishes each attribute through a 128-byte buffer.
constexpr size_t RULES_TEXT_MAX = MAX_RULES * 9 + 1;
void formatRelayRules(int relayIndex, char *out, size_t outSize);
// Pure parser (touches no shared state), so the network task can validate before queuing.
bool parseRelayRules(const char *text, RelayRule rules[MAX_RULES], int &count);
