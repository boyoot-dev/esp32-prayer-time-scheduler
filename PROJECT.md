# Prayer Automation Hub

Terse technical reference. [README.md](README.md) has the reasoning behind each item; if the two disagree, trust the code.

## Overview

ESP32 relay controller that switches pumps or similar loads around the five daily prayer times. Automation runs entirely on the device from a DS3231 RTC and an uploaded CSV calendar. A self-hosted ThingsBoard server (via TbLink) is an optional remote panel.

## Hardware

- ESP32 Dev Module, Arduino-ESP32 2.0.17 via PlatformIO
- DS3231 RTC on I²C (SDA 21, SCL 22), Adafruit RTClib 2.1.4; optional when NTP is available
- 4 active-low relays: GPIO 2, 4, 5, 18 (`LOW` = ON)
- Flash: `min_spiffs.csv` partition table (2 × 1.9 MB app slots, 128 KB LittleFS), NVS for settings

## PlatformIO

```ini
lib_deps =
    adafruit/RTClib @ 2.1.4
    knolleary/PubSubClient @ ^2.8
    bblanchon/ArduinoJson @ ^6.21.3
    https://github.com/boyoot-dev/thingsboard-link.git#2b9748f
board_build.filesystem = littlefs
board_build.partitions = min_spiffs.csv
```

Upload/monitor on `COM3`, 115200. Switching a board from the default partition table loses its LittleFS files once.

## Task layout

| Core | Task | Owns |
|---|---|---|
| 1 | Arduino `loop()` | Relays, modes, rules, schedule, RTC, LittleFS, local web server |
| 0 | `network` (prio 1, 8 KB stack) | WiFi station, gateway pings, TbLink (MQTT/TLS/OTA) |

Cross-core only through: `DeviceState` snapshot (250 ms, spinlock), rules-text snapshot, event queue (16), command queue (8), NTP-sync flag. Only the network task touches the MQTT client.

## Network

- AP `PrayerTimer-Setup` / `password123` at `192.168.4.1`, always on
- WiFi station from NVS settings; retry 15 s (2 min while AP has clients); hostname `prayertimer.local`
- Link watchdog: while ThingsBoard is down, ping gateway every 60 s, rejoin WiFi after 2 failures; never reboots

## Time

- RTC keeps Egypt standard time (UTC+2) all year; system clock keeps UTC
- `currentTime()`: sane RTC, else system clock once NTP or a browser sync has set it
- NTP correction applied when drift ≥ 2 s or automation is off; browser sync on every page load
- Sane RTC: year 2024–2035, DS3231 not reporting lost power
- Egypt DST window hard-coded in the browser sync JS (and the compile-time fallback)

## Automation

- `/schedule.csv`: `month,day,fajr,dhuhr,asr,maghrib,isha` (minutes past midnight), matched by month/day
- Up to 10 rules per relay: prayer + offset (±1439 min) → ON/OFF; one-shot state changes, not durations
- Minute tick fires exact matches for AUTO relays; `computeAutoRelayStateAtTime()` reconstructs state on boot, rollover, upload, rule save, mode change, time sync
- Modes: AUTO (follows rules; OFF without trusted time + schedule), MANUAL ON, MANUAL OFF (always apply, survive reboot)

## ThingsBoard

- MQTT `mqtt.iot.boyoot.dev:8883` TLS (ISRG Root X1); waits for a valid clock before connecting
- OTA download `iot.boyoot.dev:80` plain HTTP (checksum verified); title `prayer-timer`, version `FW_VERSION`
- OTA installs only when all relays are OFF and no AUTO rule is due within 15 min, and (if `auto_update` is off) after Install; restarts by itself
- Firmware assignment re-requested every 30 min

Telemetry: `relay1..4_state`, `relay1..4_mode` (`auto|on|off`), `rtc_ok`, `schedule_loaded`, `automation_enabled`, `event`, `heartbeat` (8 s), `current_fw_title`, `current_fw_version`

Attributes: `fajr..isha` (`HH:MM`), `relay1..4_rules`, `ip`, `rssi`, `uptime_s`, `fw_waiting`, `auto_update`

RPC:

| Method | Params |
|---|---|
| `set_relay_mode` | `{"relay": 1-4, "mode": "auto"\|"on"\|"off"}` |
| `set_rules` | `{"relay": 1-4, "rules": "1+10:1;1+20:0"}` |
| `fw_check` | `{}` |
| `fw_install` | `{}` |
| `set_auto_update` | `{"enabled": bool}` |
| `reboot` | `{}` |

Rules text: `<prayer 0-4><+/-offset>:<1|0>` joined by `;` (fits TbLink's 128-byte publish buffer).

Dashboard widget: [thingsboard/control-panel/](thingsboard/control-panel/).

## Local HTTP routes

| Method | Path | Purpose |
|---|---|---|
| `GET`/`POST` | `/` | Main page / save all rules (`save_rules`) |
| `GET` | `/relay` | `relay` 0–3, `mode` 0–2 |
| `GET` | `/status` | Relay modes, states, rules as JSON (polled every 3 s) |
| `GET` | `/time-sync` | `y, mo, d, h, mi, s` |
| `POST` | `/upload` | Schedule CSV (field `update`) |
| `GET`/`POST` | `/network` | Network status / save WiFi settings |
| `POST` | `/thingsboard` | Save ThingsBoard settings (restarts if already connected) |
| `POST` | `/firmware` | `action=save\|check\|install` |

No authentication on any route.

## Storage

| Where | Item |
|---|---|
| LittleFS `/schedule.csv` | Uploaded calendar |
| LittleFS `/rules.bin` | Raw `relayRules` array, unversioned |
| LittleFS `/relay_state.bin` | Modes + states, magic `0x50545231`, version 1 |
| LittleFS `/events.log` | Last 100 events |
| NVS `network` | `ssid`, `pass`, `static`, `ip`, `gw`, `mask`, `dns` |
| NVS `thingsboard` | `token`, `host`, `port`, `tls`, `http_host`, `http_port`, `auto_update` |

## Safety

- Relays OFF at pin setup; 10 s startup delay before restoring any state
- AUTO only with trusted time and today's schedule; manual modes always apply
- Task watchdog 30 s (covers a TLS handshake)
- Network problems never reboot the board or change relays
- OTA waits for idle relays
- Event log (flash + ThingsBoard) records every relay change with its cause and source

## Modules

| File | Responsibility |
|---|---|
| `src/main.cpp` | Boot, loop, remote command handling, state snapshot |
| `src/relay_rules.cpp` | Relay I/O, modes, rules, persistence, AUTO logic, rules text |
| `src/schedule_manager.cpp` | CSV parsing |
| `src/clock_sync.cpp` | `currentTime()`, RTC/system clock, NTP, `applyClockSync()` |
| `src/net.cpp` | Network task, WiFi, link watchdog |
| `src/network_settings.cpp` | WiFi + ThingsBoard settings in NVS |
| `src/thingsboard_link.cpp` | TbLink, publishing, RPC, events, OTA gate |
| `src/web_ui.cpp` | Local web UI and HTTP handlers |
| `src/event_log.cpp` | Capped log, forwarded to ThingsBoard |
| `src/time_safety.cpp` | RTC sanity check, timestamp format |

## Build

```powershell
C:\Users\albedair\.platformio\penv\Scripts\pio.exe run
C:\Users\albedair\.platformio\penv\Scripts\pio.exe run --target upload
C:\Users\albedair\.platformio\penv\Scripts\pio.exe device monitor
```
