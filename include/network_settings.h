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

struct ThingsBoardSettings
{
    String token; // empty = ThingsBoard link off
    // MQTT host must be a DNS-only name: the main domain is behind Cloudflare, which
    // doesn't forward MQTT.
    String host = "mqtt.iot.boyoot.dev";
    uint16_t port = 8883;
    bool useTls = true;
    // OTA downloads use plain HTTP: HTTPS through the Cloudflare Tunnel hangs the ESP32's
    // TLS handshake. The firmware checksum still arrives over MQTT and is verified.
    String httpHost = "iot.boyoot.dev";
    uint16_t httpPort = 80;
};

// Stored in NVS, so safe to call from either core.
NetworkSettings loadNetworkSettings();
void saveNetworkSettings(const NetworkSettings &settings);
ThingsBoardSettings loadThingsBoardSettings();
void saveThingsBoardSettings(const ThingsBoardSettings &settings);
