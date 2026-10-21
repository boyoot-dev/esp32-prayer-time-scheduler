#include "thingsboard_link.h"

#include <TbLink.h>
#include <WiFi.h>
#include <time.h>

#include "ca_certs.h"
#include "firmware_version.h"
#include "network_settings.h"
#include "relay_rules.h"
#include "schedule_manager.h"

namespace
{
constexpr unsigned long FULL_STATE_INTERVAL_MS = 5UL * 60 * 1000;
constexpr unsigned long DEVICE_INFO_INTERVAL_MS = 60000;
// ThingsBoard doesn't reliably push a new firmware assignment to a connected device.
constexpr unsigned long FIRMWARE_CHECK_INTERVAL_MS = 30UL * 60 * 1000;
constexpr unsigned long RESTART_DELAY_MS = 1000;
// TLS checks certificate dates; an unset clock (1970) makes every certificate "not yet valid".
constexpr time_t MIN_VALID_EPOCH = 1700000000;

const char *const PRAYER_KEYS[PRAYER_COUNT] = {"fajr", "dhuhr", "asr", "maghrib", "isha"};

TbLink tb;
QueueHandle_t commandQueue = nullptr;
volatile bool started = false;
volatile bool cloudConnected = false;
volatile bool reconfigureRequested = false;
volatile bool restartPending = false;
unsigned long restartAt = 0;

portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
DeviceState reportedState;
bool reportedStateValid = false;

// Network task only.
bool useTls = false;
bool wasConnected = false;
bool loggedClockWait = false;
DeviceState sentState;
unsigned long lastFullState = 0;
unsigned long lastDeviceInfo = 0;

const char *relayModeKey(uint8_t mode)
{
    switch (mode)
    {
    case RELAY_MODE_MANUAL_ON:
        return "on";
    case RELAY_MODE_MANUAL_OFF:
        return "off";
    default:
        return "auto";
    }
}

int parseRelayMode(const char *key)
{
    for (uint8_t mode = RELAY_MODE_AUTO; mode <= RELAY_MODE_MANUAL_OFF; mode++)
    {
        if (strcmp(key, relayModeKey(mode)) == 0)
            return mode;
    }
    return -1;
}

void scheduleRestart()
{
    restartAt = millis() + RESTART_DELAY_MS;
    restartPending = true;
}

bool handleRpc(const String &method, JsonVariantConst params, JsonDocument &response)
{
    if (method == "set_relay_mode")
    {
        int relay = params["relay"] | 0;
        int mode = parseRelayMode(params["mode"] | "");
        if (relay < 1 || relay > RELAY_COUNT || mode < 0)
        {
            response["success"] = false;
            response["error"] = "expected {\"relay\": 1-4, \"mode\": \"auto\"|\"on\"|\"off\"}";
            return true;
        }

        RemoteCommand command = {REMOTE_SET_RELAY_MODE, static_cast<int8_t>(relay - 1), static_cast<uint8_t>(mode)};
        bool queued = xQueueSend(commandQueue, &command, 0) == pdTRUE;
        response["success"] = queued;
        if (!queued)
            response["error"] = "busy, try again";
        return true;
    }

    // Restarting turns every relay OFF for the startup safety delay.
    if (method == "reboot")
    {
        scheduleRestart();
        response["success"] = true;
        return true;
    }

    return false;
}

bool samePrayerTimes(const DeviceState &a, const DeviceState &b)
{
    if (a.scheduleLoaded != b.scheduleLoaded)
        return false;
    for (int p = 0; p < PRAYER_COUNT; p++)
    {
        if (a.prayerMins[p] != b.prayerMins[p])
            return false;
    }
    return true;
}

// Publishes the fields that differ from what was last sent, or all of them when `full`.
void sendState(const DeviceState &state, bool full)
{
    char key[16];
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        if (full || state.relayOn[r] != sentState.relayOn[r])
        {
            snprintf(key, sizeof(key), "relay%d_state", r + 1);
            tb.sendTelemetry(key, state.relayOn[r]);
        }
        if (full || state.relayMode[r] != sentState.relayMode[r])
        {
            snprintf(key, sizeof(key), "relay%d_mode", r + 1);
            tb.sendTelemetry(key, relayModeKey(state.relayMode[r]));
        }
    }

    if (full || state.rtcOk != sentState.rtcOk)
        tb.sendTelemetry("rtc_ok", state.rtcOk);
    if (full || state.scheduleLoaded != sentState.scheduleLoaded)
        tb.sendTelemetry("schedule_loaded", state.scheduleLoaded);
    if (full || state.automationEnabled != sentState.automationEnabled)
        tb.sendTelemetry("automation_enabled", state.automationEnabled);

    if (state.scheduleLoaded && (full || !samePrayerTimes(state, sentState)))
    {
        for (int p = 0; p < PRAYER_COUNT; p++)
            tb.sendAttribute(PRAYER_KEYS[p], formatMinsToTime(state.prayerMins[p]).c_str());
    }

    sentState = state;
}

void sendDeviceInfo()
{
    tb.sendAttribute("ip", WiFi.localIP().toString().c_str());
    tb.sendAttribute("rssi", static_cast<int>(WiFi.RSSI()));
    tb.sendAttribute("uptime_s", static_cast<int>(millis() / 1000));
}

void publishState(unsigned long now, bool justConnected)
{
    DeviceState state;
    portENTER_CRITICAL(&stateMux);
    bool valid = reportedStateValid;
    state = reportedState;
    portEXIT_CRITICAL(&stateMux);
    if (!valid)
        return;

    bool full = justConnected || now - lastFullState >= FULL_STATE_INTERVAL_MS;
    if (full)
        lastFullState = now;
    sendState(state, full);
}

void startLink()
{
    ThingsBoardSettings settings = loadThingsBoardSettings();
    if (settings.token.length() == 0 || settings.host.length() == 0)
    {
        Serial.println("ThingsBoard not configured; set the device token on the network page");
        return;
    }

    uint8_t mac[6];
    WiFi.macAddress(mac);
    char clientId[24];
    snprintf(clientId, sizeof(clientId), "%s-%02x%02x%02x", NETWORK_HOSTNAME, mac[3], mac[4], mac[5]);

    TbMqttConfig mqtt;
    mqtt.host = settings.host;
    mqtt.port = settings.port;
    mqtt.access_token = settings.token;
    mqtt.client_id = clientId;
    mqtt.use_tls = settings.useTls;
    mqtt.ca_cert = ISRG_ROOT_X1_PEM;
    mqtt.buffer_size = 1024;
    mqtt.reconnect_interval_ms = 15000;
    mqtt.attribute_refresh_interval_ms = FIRMWARE_CHECK_INTERVAL_MS;

    TbOtaConfig ota;
    ota.http_host = settings.httpHost;
    ota.http_port = settings.httpPort;
    ota.force_plain_http = true;
    ota.current_fw_title = FW_TITLE;
    ota.current_fw_version = FW_VERSION;
    ota.expected_fw_title = FW_TITLE;

    tb.onRpc(handleRpc);
    // Installing an update restarts the board, which drops the relays; nothing installs
    // until an install rule decides when that is safe.
    tb.onOtaGate([]()
                 { return false; });
    tb.onOtaResult([](bool success, const String &error)
                   {
                       if (!success)
                           Serial.println("OTA update failed: " + error); });
    tb.begin(mqtt, ota);

    useTls = settings.useTls;
    started = true;
    Serial.println("ThingsBoard link started: " + settings.host + ":" + String(settings.port) + (useTls ? " (TLS)" : ""));
}
}

void reportDeviceState(const DeviceState &state)
{
    portENTER_CRITICAL(&stateMux);
    reportedState = state;
    reportedStateValid = true;
    portEXIT_CRITICAL(&stateMux);
}

bool takeRemoteCommand(RemoteCommand &command)
{
    return commandQueue != nullptr && xQueueReceive(commandQueue, &command, 0) == pdTRUE;
}

void beginThingsBoard()
{
    commandQueue = xQueueCreate(8, sizeof(RemoteCommand));
    startLink();
}

void thingsBoardLoop(unsigned long now)
{
    if (restartPending && static_cast<long>(now - restartAt) >= 0)
        ESP.restart();

    if (reconfigureRequested)
    {
        reconfigureRequested = false;
        if (started)
        {
            Serial.println("ThingsBoard settings changed; restarting to apply");
            scheduleRestart();
        }
        else
        {
            startLink();
        }
    }

    bool clockOk = !useTls || time(nullptr) >= MIN_VALID_EPOCH;
    if (started && !clockOk && !loggedClockWait)
    {
        loggedClockWait = true;
        Serial.println("ThingsBoard: waiting for a valid clock (RTC or NTP) before TLS connect");
    }

    if (!started || !clockOk || WiFi.status() != WL_CONNECTED)
    {
        cloudConnected = false;
        wasConnected = false;
        return;
    }

    unsigned long loopStarted = millis();
    tb.loop(); // may block for a reconnect attempt (TLS handshake ~7 s) or an OTA download
    unsigned long took = millis() - loopStarted;
    if (took > 1000)
        Serial.printf("ThingsBoard: loop took %lu ms\n", took);

    bool connected = tb.connected();
    cloudConnected = connected;
    if (!connected)
    {
        wasConnected = false;
        return;
    }

    bool justConnected = !wasConnected;
    wasConnected = true;
    publishState(now, justConnected);
    if (justConnected || now - lastDeviceInfo >= DEVICE_INFO_INTERVAL_MS)
    {
        lastDeviceInfo = now;
        sendDeviceInfo();
    }
}

void requestThingsBoardReconfigure()
{
    reconfigureRequested = true;
}

bool isThingsBoardStarted()
{
    return started;
}

bool isThingsBoardConnected()
{
    return cloudConnected;
}
