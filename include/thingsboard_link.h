#pragma once

#include <Arduino.h>

#include "app_config.h"
#include "relay_rules.h"

// ThingsBoard connection via TbLink. TbLink runs only in the network task (core 0); the
// loop task (core 1) owns all relay/rule state and talks to it through the "Loop task"
// calls below (snapshots and queues), so neither core ever waits on the other.

struct DeviceState
{
    bool relayOn[RELAY_COUNT];
    uint8_t relayMode[RELAY_COUNT]; // RelayMode
    bool rtcOk;
    bool scheduleLoaded;
    bool automationEnabled;
    int prayerMins[PRAYER_COUNT]; // valid when scheduleLoaded
    // All relays OFF and no AUTO rule due soon: a firmware update may restart the board.
    bool safeToRestart;
};

enum RemoteCommandType : uint8_t
{
    REMOTE_SET_RELAY_MODE,
    REMOTE_SET_RELAY_RULES
};

struct RemoteCommand
{
    RemoteCommandType type;
    int8_t relayIndex;
    uint8_t mode;      // RelayMode, for REMOTE_SET_RELAY_MODE
    uint8_t ruleCount; // for REMOTE_SET_RELAY_RULES
    RelayRule rules[MAX_RULES];
};

// Loop task: hands the current state to the network task, which publishes what changed.
void reportDeviceState(const DeviceState &state);
// Loop task: call after rules change (from either side) to republish them.
void reportRelayRules();
// Loop task: queues an event-log message for the "event" telemetry key. Kept while
// ThingsBoard is unreachable, up to a small backlog.
void publishEvent(const String &message);
// Loop task: returns the next command received from ThingsBoard, if any.
bool takeRemoteCommand(RemoteCommand &command);

// Reads the saved settings and starts the link if a device token is set. Call once,
// before the network task starts.
void beginThingsBoard();
// Network task only.
void thingsBoardLoop(unsigned long now);

// Applies newly saved settings: starts the link if it wasn't running, otherwise restarts
// the board (TbLink can't switch servers live). Safe to call from any task.
void requestThingsBoardReconfigure();

// Firmware updates. An assigned update installs by itself once the relays are idle when
// auto-update is on; with it off, only after requestFirmwareInstall(). Safe from any task.
struct FirmwareStatus
{
    char waiting[24];   // version waiting to install, "" if none
    char lastError[64]; // last failed attempt since boot, "" if none
    bool autoUpdate;
    bool installRequested;
};
FirmwareStatus getFirmwareStatus();
// Asks ThingsBoard for the assigned firmware now instead of at the 30-minute refresh.
void requestFirmwareCheck();
// Allows a waiting update to install (once relays are idle); false if none is waiting.
bool requestFirmwareInstall();
// Saves the setting to NVS and applies it immediately.
void setAutoUpdate(bool enabled);
bool isThingsBoardStarted();
bool isThingsBoardConnected();
