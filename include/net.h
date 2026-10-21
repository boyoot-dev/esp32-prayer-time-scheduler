#pragma once

#include <Arduino.h>

struct NetworkStatus
{
    bool wifiConnected;
    String ssid;
    String ip;
    int rssi;
    String apIp;
    int apClients;
};

// Starts the always-on setup AP, SNTP, and the network task on core 0, which joins the
// saved WiFi network and keeps the link alive. Network stalls (reconnects, pings, TLS)
// stay on core 0 so the relay automation and web server on core 1 never wait on them.
void beginNetwork();

// Re-reads the saved network settings and rejoins WiFi. Safe to call from any task.
void requestNetworkReconnect();

NetworkStatus getNetworkStatus();
