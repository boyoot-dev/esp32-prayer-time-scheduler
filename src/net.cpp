#include "net.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <ping/ping_sock.h>

#include "app_config.h"
#include "clock_sync.h"
#include "network_settings.h"
#include "thingsboard_link.h"

namespace
{
// Each WiFi.begin() scans across channels, which briefly drops clients on the always-on
// AP, so retry rarely while someone is connected to it (e.g. fixing a wrong password).
constexpr unsigned long STA_RETRY_MS = 15000;
constexpr unsigned long STA_RETRY_WITH_AP_CLIENTS_MS = 120000;

// The WiFi driver can keep reporting WL_CONNECTED after the link has silently died.
// While ThingsBoard isn't connected, ping the gateway once a minute; two failed checks
// in a row force a WiFi rejoin. Never a reboot: this board drives relays.
constexpr unsigned long LINK_CHECK_MS = 60000;
constexpr int LINK_FAILS_BEFORE_RESET = 2;

// Owned by the network task after beginNetwork().
NetworkSettings settings;
volatile bool reconnectRequested = false;
bool wasConnected = false;
bool mdnsStarted = false;
unsigned long lastStaAttempt = 0;
unsigned long lastLinkCheck = 0;
int linkFails = 0;

void applyIpConfig()
{
    IPAddress ip, gateway, subnet, dns;
    if (settings.useStaticIp && ip.fromString(settings.ip) && gateway.fromString(settings.gateway) && subnet.fromString(settings.subnet))
    {
        if (!dns.fromString(settings.dns))
            dns = gateway;
        WiFi.config(ip, gateway, subnet, dns);
        return;
    }

    if (settings.useStaticIp)
        Serial.println("Static IP settings invalid; using DHCP");
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
}

void joinWifi()
{
    lastStaAttempt = millis();
    if (settings.ssid.length() == 0)
        return;

    applyIpConfig();
    Serial.println("Joining WiFi " + settings.ssid);
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
}

void manageWifi(unsigned long now)
{
    if (reconnectRequested)
    {
        reconnectRequested = false;
        settings = loadNetworkSettings();
        WiFi.disconnect(false);
        joinWifi();
        return;
    }

    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wasConnected)
    {
        wasConnected = connected;
        if (connected)
            Serial.println("WiFi connected, IP " + WiFi.localIP().toString() + ", RSSI " + String(WiFi.RSSI()));
        else
            Serial.println("WiFi disconnected");
    }

    if (connected || settings.ssid.length() == 0)
        return;

    unsigned long retryMs = WiFi.softAPgetStationNum() > 0 ? STA_RETRY_WITH_AP_CLIENTS_MS : STA_RETRY_MS;
    if (now - lastStaAttempt >= retryMs)
        joinWifi();
}

// Sends 3 pings to the gateway; true if any comes back.
bool gatewayReachable()
{
    IPAddress gw = WiFi.gatewayIP();
    if (gw == IPAddress(0, 0, 0, 0))
        return false;

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    IP_ADDR4(&config.target_addr, gw[0], gw[1], gw[2], gw[3]);
    config.count = 3;
    config.interval_ms = 300;
    config.timeout_ms = 1000;

    volatile int replies = 0;
    esp_ping_callbacks_t callbacks = {};
    callbacks.cb_args = (void *)&replies;
    callbacks.on_ping_success = [](esp_ping_handle_t, void *arg)
    { (*(volatile int *)arg)++; };

    esp_ping_handle_t ping;
    if (esp_ping_new_session(&config, &callbacks, &ping) != ESP_OK)
        return true; // can't tell; don't tear down a link we couldn't test

    esp_ping_start(ping);
    vTaskDelay(pdMS_TO_TICKS(3 * 300 + 1500));
    esp_ping_stop(ping);
    esp_ping_delete_session(ping);
    return replies > 0;
}

void linkWatchdog(unsigned long now)
{
    if (WiFi.status() != WL_CONNECTED || isThingsBoardConnected())
    {
        linkFails = 0;
        lastLinkCheck = now;
        return;
    }

    if (now - lastLinkCheck < LINK_CHECK_MS)
        return;

    lastLinkCheck = now;
    if (gatewayReachable())
    {
        linkFails = 0;
        return;
    }

    Serial.printf("Gateway not answering pings (%d/%d)\n", ++linkFails, LINK_FAILS_BEFORE_RESET);
    if (linkFails >= LINK_FAILS_BEFORE_RESET)
    {
        Serial.println("WiFi link dead although the driver reports connected; rejoining");
        linkFails = 0;
        WiFi.disconnect(false);
        vTaskDelay(pdMS_TO_TICKS(200));
        joinWifi();
    }
}

void networkTask(void *)
{
    for (;;)
    {
        unsigned long now = millis();
        manageWifi(now);
        linkWatchdog(now);
        thingsBoardLoop(now);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
}

void beginNetwork()
{
    settings = loadNetworkSettings();

    WiFi.persistent(false);
    WiFi.setHostname(NETWORK_HOSTNAME);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t)
                 {
                     if (!mdnsStarted && MDNS.begin(NETWORK_HOSTNAME))
                     {
                         mdnsStarted = true;
                         MDNS.addService("http", "tcp", 80);
                     } },
                 ARDUINO_EVENT_WIFI_STA_GOT_IP);

    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.print("SoftAP IP: ");
    Serial.println(WiFi.softAPIP());

    beginNtp();
    if (settings.ssid.length() == 0)
        Serial.println("No WiFi network saved; AP only");
    joinWifi();
    beginThingsBoard();

    xTaskCreatePinnedToCore(networkTask, "network", 8192, nullptr, 1, nullptr, 0);
}

void requestNetworkReconnect()
{
    reconnectRequested = true;
}

NetworkStatus getNetworkStatus()
{
    NetworkStatus status;
    status.wifiConnected = WiFi.status() == WL_CONNECTED;
    status.ssid = WiFi.SSID();
    status.ip = WiFi.localIP().toString();
    status.rssi = WiFi.RSSI();
    status.apIp = WiFi.softAPIP().toString();
    status.apClients = WiFi.softAPgetStationNum();
    return status;
}
