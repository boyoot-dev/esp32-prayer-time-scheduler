#include "relay_rules.h"

#include <LittleFS.h>

#include "event_log.h"

RelayRule relayRules[RELAY_COUNT][MAX_RULES];
RelayMode relayModes[RELAY_COUNT];

namespace
{
const char *RULES_PATH = "/rules.bin";
const char *RELAY_SETTINGS_PATH = "/relay_state.bin";
constexpr uint32_t RELAY_SETTINGS_MAGIC = 0x50545231;
constexpr uint16_t RELAY_SETTINGS_VERSION = 1;

struct RelaySettingsFile
{
    uint32_t magic;
    uint16_t version;
    uint8_t modes[RELAY_COUNT];
    uint8_t states[RELAY_COUNT];
};

int persistedRelayStates[RELAY_COUNT];

int normalizeRelayState(int targetState)
{
    return targetState == RELAY_ON_STATE ? RELAY_ON_STATE : RELAY_OFF_STATE;
}

RelayMode normalizeRelayMode(uint8_t mode)
{
    if (mode == RELAY_MODE_MANUAL_ON)
        return RELAY_MODE_MANUAL_ON;
    if (mode == RELAY_MODE_MANUAL_OFF)
        return RELAY_MODE_MANUAL_OFF;
    return RELAY_MODE_AUTO;
}

String relayStateLabel(int state)
{
    return normalizeRelayState(state) == RELAY_ON_STATE ? "ON" : "OFF";
}

bool isValidRule(const RelayRule &rule)
{
    return rule.prayerIndex >= 0 && rule.prayerIndex < PRAYER_COUNT;
}

void resetRelaySettings()
{
    for (int i = 0; i < RELAY_COUNT; i++)
    {
        relayModes[i] = RELAY_MODE_AUTO;
        persistedRelayStates[i] = RELAY_OFF_STATE;
    }
}

int computeAutoRelayStateAtTime(int relayIndex, int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT])
{
    int chosenTargetMinute = -1;
    int chosenState = RELAY_OFF_STATE;

    for (int i = 0; i < MAX_RULES; i++)
    {
        RelayRule rule = relayRules[relayIndex][i];
        if (!isValidRule(rule))
            continue;

        int target = prayerTimes[rule.prayerIndex] + rule.offsetMins;
        if (target < 0 || target > 1439)
            continue;

        if (target <= currentMinsPastMidnight && target >= chosenTargetMinute)
        {
            chosenTargetMinute = target;
            chosenState = normalizeRelayState(rule.targetState);
        }
    }

    return chosenState;
}
}

void setupRelayPins()
{
    for (int i = 0; i < RELAY_COUNT; i++)
    {
        pinMode(RELAY_PINS[i], OUTPUT);
        setRelayState(i, RELAY_OFF_STATE, false);
    }
}

void setRelayState(int relayIndex, int targetState, bool persist)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return;

    int normalizedState = normalizeRelayState(targetState);
    digitalWrite(RELAY_PINS[relayIndex], normalizedState);

    if (persist && persistedRelayStates[relayIndex] != normalizedState)
    {
        persistedRelayStates[relayIndex] = normalizedState;
        saveRelaySettingsToStorage();
    }
}

int getRelayState(int relayIndex)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return RELAY_OFF_STATE;

    return normalizeRelayState(digitalRead(RELAY_PINS[relayIndex]));
}

const char *relayModeLabel(RelayMode mode)
{
    switch (mode)
    {
    case RELAY_MODE_MANUAL_ON:
        return "MANUAL ON";
    case RELAY_MODE_MANUAL_OFF:
        return "MANUAL OFF";
    default:
        return "AUTO";
    }
}

RelayMode getRelayMode(int relayIndex)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return RELAY_MODE_AUTO;

    return relayModes[relayIndex];
}

void setRelayMode(int relayIndex, RelayMode mode)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return;

    RelayMode normalizedMode = normalizeRelayMode(mode);
    if (relayModes[relayIndex] != normalizedMode)
    {
        relayModes[relayIndex] = normalizedMode;
        saveRelaySettingsToStorage();
    }
}

void changeRelayMode(int relayIndex, RelayMode mode, int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], const DateTime &now, bool automationEnabled, const char *source)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return;

    setRelayMode(relayIndex, mode);

    if (mode == RELAY_MODE_MANUAL_ON)
        setRelayState(relayIndex, RELAY_ON_STATE);
    else if (mode == RELAY_MODE_MANUAL_OFF)
        setRelayState(relayIndex, RELAY_OFF_STATE);
    else
        applyRelayStateForModeAtTime(relayIndex, currentMinsPastMidnight, prayerTimes, now, automationEnabled);

    appendEventLog(now, "Relay " + String(relayIndex + 1) + " mode " + String(relayModeLabel(mode)) + "; state " + relayStateLabel(getRelayState(relayIndex)) + " (" + source + ")");
}

void loadRelaySettingsFromStorage()
{
    resetRelaySettings();

    if (!LittleFS.exists(RELAY_SETTINGS_PATH))
        return;

    File file = LittleFS.open(RELAY_SETTINGS_PATH, "r");
    if (!file)
        return;

    RelaySettingsFile settings;
    size_t bytesRead = file.read((uint8_t *)&settings, sizeof(settings));
    file.close();

    if (bytesRead != sizeof(settings) || settings.magic != RELAY_SETTINGS_MAGIC || settings.version != RELAY_SETTINGS_VERSION)
        return;

    for (int i = 0; i < RELAY_COUNT; i++)
    {
        relayModes[i] = normalizeRelayMode(settings.modes[i]);
        persistedRelayStates[i] = normalizeRelayState(settings.states[i]);
    }
}

void saveRelaySettingsToStorage()
{
    RelaySettingsFile settings;
    settings.magic = RELAY_SETTINGS_MAGIC;
    settings.version = RELAY_SETTINGS_VERSION;

    for (int i = 0; i < RELAY_COUNT; i++)
    {
        settings.modes[i] = relayModes[i];
        settings.states[i] = normalizeRelayState(persistedRelayStates[i]);
    }

    File file = LittleFS.open(RELAY_SETTINGS_PATH, "w");
    if (!file)
        return;

    file.write((uint8_t *)&settings, sizeof(settings));
    file.close();
}

void applyRelayStateForModeAtTime(int relayIndex, int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], const DateTime &now, bool automationEnabled)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return;

    int before = getRelayState(relayIndex);
    int desiredState = RELAY_OFF_STATE;

    // Manual modes don't depend on the clock, so they hold even when automation is
    // disabled; only AUTO relays fall back to OFF without trusted time and a schedule.
    if (relayModes[relayIndex] == RELAY_MODE_MANUAL_ON)
    {
        desiredState = RELAY_ON_STATE;
    }
    else if (relayModes[relayIndex] == RELAY_MODE_MANUAL_OFF)
    {
        desiredState = RELAY_OFF_STATE;
    }
    else if (automationEnabled)
    {
        desiredState = computeAutoRelayStateAtTime(relayIndex, currentMinsPastMidnight, prayerTimes);
    }

    setRelayState(relayIndex, desiredState);

    if (before != desiredState)
    {
        appendEventLog(now, "Relay " + String(relayIndex + 1) + " " + relayStateLabel(desiredState) + " (" + String(relayModeLabel(relayModes[relayIndex])) + " restore)");
    }
}

void restoreRelayStatesForNow(int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], const DateTime &now, bool automationEnabled)
{
    for (int r = 0; r < RELAY_COUNT; r++)
        applyRelayStateForModeAtTime(r, currentMinsPastMidnight, prayerTimes, now, automationEnabled);
}

void clearAllRules()
{
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        for (int i = 0; i < MAX_RULES; i++)
        {
            relayRules[r][i].prayerIndex = -1;
            relayRules[r][i].offsetMins = 0;
            relayRules[r][i].targetState = RELAY_OFF_STATE;
        }
    }
}

void saveRulesToStorage()
{
    File file = LittleFS.open(RULES_PATH, "w");
    if (file)
    {
        file.write((uint8_t *)relayRules, sizeof(relayRules));
        file.close();
    }
}

void loadRulesFromStorage()
{
    clearAllRules();

    if (!LittleFS.exists(RULES_PATH))
        return;

    File file = LittleFS.open(RULES_PATH, "r");
    if (file)
    {
        file.read((uint8_t *)relayRules, sizeof(relayRules));
        file.close();
    }
}

void setRelayRules(int relayIndex, const RelayRule rules[MAX_RULES], int count)
{
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
        return;

    for (int i = 0; i < MAX_RULES; i++)
    {
        if (i < count)
        {
            relayRules[relayIndex][i] = rules[i];
        }
        else
        {
            relayRules[relayIndex][i].prayerIndex = -1;
            relayRules[relayIndex][i].offsetMins = 0;
            relayRules[relayIndex][i].targetState = RELAY_OFF_STATE;
        }
    }
}

bool hasAutoRuleWithin(int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], int windowMins)
{
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        if (relayModes[r] != RELAY_MODE_AUTO)
            continue;

        for (int i = 0; i < MAX_RULES; i++)
        {
            RelayRule rule = relayRules[r][i];
            if (!isValidRule(rule))
                continue;

            int minutesUntil = prayerTimes[rule.prayerIndex] + rule.offsetMins - currentMinsPastMidnight;
            if (minutesUntil < 0)
                minutesUntil += 1440;
            if (minutesUntil <= windowMins)
                return true;
        }
    }
    return false;
}

void formatRelayRules(int relayIndex, char *out, size_t outSize)
{
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < MAX_RULES && relayIndex >= 0 && relayIndex < RELAY_COUNT; i++)
    {
        RelayRule rule = relayRules[relayIndex][i];
        if (!isValidRule(rule))
            continue;

        int written = snprintf(out + used, outSize - used, "%s%d%+d:%d", used ? ";" : "", rule.prayerIndex, rule.offsetMins, normalizeRelayState(rule.targetState) == RELAY_ON_STATE ? 1 : 0);
        if (written < 0 || used + written >= outSize)
            break;
        used += written;
    }
}

bool parseRelayRules(const char *text, RelayRule rules[MAX_RULES], int &count)
{
    count = 0;
    const char *p = text;
    while (*p != '\0')
    {
        int prayer, offset, on, consumed = 0;
        if (count >= MAX_RULES || sscanf(p, "%d%d:%d%n", &prayer, &offset, &on, &consumed) != 3)
            return false;
        if (prayer < 0 || prayer >= PRAYER_COUNT || offset < -1439 || offset > 1439 || (on != 0 && on != 1))
            return false;

        rules[count].prayerIndex = prayer;
        rules[count].offsetMins = offset;
        rules[count].targetState = on ? RELAY_ON_STATE : RELAY_OFF_STATE;
        count++;

        p += consumed;
        if (*p == ';')
            p++;
        else if (*p != '\0')
            return false;
    }
    return true;
}

void applyRelayRules(int currentMinsPastMidnight, const int prayerTimes[PRAYER_COUNT], const DateTime &now, bool automationEnabled)
{
    if (!automationEnabled)
        return;

    for (int r = 0; r < RELAY_COUNT; r++)
    {
        if (relayModes[r] != RELAY_MODE_AUTO)
            continue;

        for (int i = 0; i < MAX_RULES; i++)
        {
            RelayRule rule = relayRules[r][i];
            if (!isValidRule(rule))
                continue;

            int target = prayerTimes[rule.prayerIndex] + rule.offsetMins;
            if (currentMinsPastMidnight != target)
                continue;

            int desiredState = normalizeRelayState(rule.targetState);
            int before = getRelayState(r);
            setRelayState(r, desiredState);

            if (before != desiredState)
                appendEventLog(now, "Relay " + String(r + 1) + " " + relayStateLabel(desiredState) + " (Auto schedule)");
        }
    }
}
