#include "network_settings.h"

#include <Preferences.h>

namespace
{
const char *PREFS_NAMESPACE = "network";
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
