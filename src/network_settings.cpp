#include "network_settings.h"

#include <Preferences.h>

namespace
{
const char *PREFS_NAMESPACE = "network";
const char *THINGSBOARD_PREFS_NAMESPACE = "thingsboard";
}

ThingsBoardSettings loadThingsBoardSettings()
{
    ThingsBoardSettings settings;
    Preferences prefs;
    if (!prefs.begin(THINGSBOARD_PREFS_NAMESPACE, true))
        return settings;

    settings.token = prefs.getString("token", settings.token);
    settings.host = prefs.getString("host", settings.host);
    settings.port = prefs.getUShort("port", settings.port);
    settings.useTls = prefs.getBool("tls", settings.useTls);
    settings.httpHost = prefs.getString("http_host", settings.httpHost);
    settings.httpPort = prefs.getUShort("http_port", settings.httpPort);
    settings.autoUpdate = prefs.getBool("auto_update", settings.autoUpdate);
    prefs.end();
    return settings;
}

void saveThingsBoardSettings(const ThingsBoardSettings &settings)
{
    Preferences prefs;
    if (!prefs.begin(THINGSBOARD_PREFS_NAMESPACE, false))
        return;

    prefs.putString("token", settings.token);
    prefs.putString("host", settings.host);
    prefs.putUShort("port", settings.port);
    prefs.putBool("tls", settings.useTls);
    prefs.putString("http_host", settings.httpHost);
    prefs.putUShort("http_port", settings.httpPort);
    prefs.putBool("auto_update", settings.autoUpdate);
    prefs.end();
}

NetworkSettings loadNetworkSettings()
{
    NetworkSettings settings;
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, true))
        return settings;

    settings.ssid = prefs.getString("ssid", "");
    settings.password = prefs.getString("pass", "");
    settings.useStaticIp = prefs.getBool("static", false);
    settings.ip = prefs.getString("ip", "");
    settings.gateway = prefs.getString("gw", "");
    settings.subnet = prefs.getString("mask", "");
    settings.dns = prefs.getString("dns", "");
    prefs.end();
    return settings;
}

void saveNetworkSettings(const NetworkSettings &settings)
{
    Preferences prefs;
    if (!prefs.begin(PREFS_NAMESPACE, false))
        return;

    prefs.putString("ssid", settings.ssid);
    prefs.putString("pass", settings.password);
    prefs.putBool("static", settings.useStaticIp);
    prefs.putString("ip", settings.ip);
    prefs.putString("gw", settings.gateway);
    prefs.putString("mask", settings.subnet);
    prefs.putString("dns", settings.dns);
    prefs.end();
}
