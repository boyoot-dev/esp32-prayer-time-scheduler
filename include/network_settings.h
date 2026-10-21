#pragma once

#include <Arduino.h>

struct NetworkSettings
{
    String ssid; // empty = AP only, never join a WiFi network
    String password;
    bool useStaticIp = false;
    String ip;
    String gateway;
    String subnet;
    String dns; // empty = use the gateway
};

// Stored in NVS, so safe to call from either core.
NetworkSettings loadNetworkSettings();
void saveNetworkSettings(const NetworkSettings &settings);
