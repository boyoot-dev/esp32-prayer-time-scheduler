#pragma once

#include <Arduino.h>

#include "app_config.h"

// ThingsBoard connection via TbLink. TbLink runs only in the network task (core 0); the
// loop task (core 1) owns all relay/rule state and talks to it through the two calls
// below, so neither core ever waits on the other.

struct DeviceState
{
    bool relayOn[RELAY_COUNT];
    uint8_t relayMode[RELAY_COUNT]; // RelayMode
    bool rtcOk;
    bool scheduleLoaded;
    bool automationEnabled;
    int prayerMins[PRAYER_COUNT]; // valid when scheduleLoaded
};

enum RemoteCommandType : uint8_t
{
    REMOTE_SET_RELAY_MODE
};

struct RemoteCommand
{
    RemoteCommandType type;
    int8_t relayIndex;
    uint8_t mode; // RelayMode
};

// Loop task: hands the current state to the network task, which publishes what changed.
void reportDeviceState(const DeviceState &state);
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
bool isThingsBoardStarted();
bool isThingsBoardConnected();
