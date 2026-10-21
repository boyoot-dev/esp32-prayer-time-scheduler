# Prayer Automation Hub

An ESP32 relay controller that switches pumps or similar loads around the five daily prayer times. All scheduling runs on the device, so it keeps working with no internet. When it can reach WiFi it also connects to a self-hosted ThingsBoard server, which works as a remote panel: live status, relay control, rule editing, an event history and firmware updates.

> This README is written for future-you and for AI coding agents picking this project back up. It documents not just *what* the code does, but *why* it's structured this way, so changes can be made safely.

For a terse reference of the same information, see [PROJECT.md](PROJECT.md). The two are kept in sync by hand; if they disagree, trust the source code and fix whichever doc is stale.

---

## Table of Contents

- [What it does](#what-it-does)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Repository layout](#repository-layout)
- [Getting started](#getting-started)
- [Configuration](#configuration)
- [Network and time](#network-and-time)
- [ThingsBoard](#thingsboard)
- [Schedule CSV format](#schedule-csv-format)
- [Relay automation rules](#relay-automation-rules)
- [AUTO / Manual override modes](#auto--manual-override-modes)
- [Local web UI (HTTP API)](#local-web-ui-http-api)
- [Storage layout](#storage-layout)
- [Safety design](#safety-design)
- [Firmware architecture](#firmware-architecture)
- [Security notes / known limitations](#security-notes--known-limitations)
- [Development notes](#development-notes)
- [Roadmap / ideas for later](#roadmap--ideas-for-later)
- [License](#license)

---

## What it does

The device controls relays (e.g. a water pump for wudu, irrigation, lighting) around Fajr, Dhuhr, Asr, Maghrib and Isha, which shift by a few minutes every day.

1. You generate a full-year prayer-time calendar for your location as a CSV and upload it once through the device's local web page.
2. You define rules per relay, e.g. "turn Relay 3 ON 10 minutes after Dhuhr, OFF 20 minutes after Dhuhr", from the local page or from ThingsBoard.
3. The ESP32 keeps time with a battery-backed DS3231 RTC (corrected from NTP when online), loads today's row from the CSV each day, and switches relays at the configured offsets.

The schedule never depends on the network or the server. ThingsBoard only mirrors state and forwards commands; if it is down, the device carries on.

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32 Dev Module (generic devkit) |
| Framework | Arduino (Arduino-ESP32 2.0.17), via PlatformIO |
| RTC | DS3231 (battery-backed) on I²C (SDA 21, SCL 22), via [Adafruit RTClib](https://github.com/adafruit/RTClib) `2.1.4` |
| Storage | Internal flash: LittleFS for files, NVS for settings |
| Relay outputs | 4 channels, **active-low** |

### Default pin map

| Relay | GPIO |
|---|---|
| Relay 1 | 2 |
| Relay 2 | 4 |
| Relay 3 | 5 |
| Relay 4 | 18 |

Active-low means `digitalWrite(pin, LOW)` energizes the relay (ON). Pins and relay count are in [include/app_config.h](include/app_config.h); all other code reads `RELAY_PINS`/`RELAY_COUNT`.

Without a working DS3231 the firmware falls back to the system clock once NTP (or a browser sync) has set it, so automation still runs on a networked board. The RTC status then reads "Not working - using network time", and `rtc_ok` is `false` in ThingsBoard. Until a time source is available, AUTO relays stay OFF; manual modes always work.

## Wiring

### 4-channel relay module (active-low)

| Relay module | ESP32 |
|---|---|
| IN1 | GPIO 2 |
| IN2 | GPIO 4 |
| IN3 | GPIO 5 |
| IN4 | GPIO 18 |
| GND | GND |
| VCC | 5V (VIN), or 3.3V, see below |

The firmware drives the inputs **LOW = relay ON, HIGH = OFF**, which suits the common opto-isolated 4-channel modules.

### DS3231 RTC

| DS3231 | ESP32 |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

Fit the coin cell so the RTC keeps time through power cuts.

### Things to watch

- **Relay won't switch fully OFF:** with the module's VCC at 5V, the ESP32's 3.3V HIGH may not fully turn its input off, so a relay stays on or chatters. If the module has a **JD-VCC jumper**, remove it, connect **VCC to 3.3V** and **JD-VCC to 5V**, and keep GND common. The relay coils then run from 5V while the inputs see 3.3V logic.
- **USB upload fails:** GPIO 2 (Relay 1) is an ESP32 boot-strapping pin. If uploading over USB fails with the relay module connected, disconnect IN1 during the upload. OTA updates are not affected.
- **Blue LED:** the devkit's onboard LED is also on GPIO 2, so it mirrors Relay 1, inverted: LED lit = Relay 1 OFF.
- **Loads:** wire each load through the relay's **COM** and **NO** (normally open) terminals, so it is disconnected whenever the relay is OFF, including during boot and the startup delay. Keep mains wiring in a proper enclosure, with a fuse, and within the relay's rated current.

## Repository layout

```
.
├── platformio.ini            Board, libraries (TbLink pinned by commit), partition table
├── include/
│   ├── app_config.h           Pins, timings, AP credentials, time zone offset, limits
│   ├── ca_certs.h             ISRG Root X1, verifies the ThingsBoard MQTT server
│   ├── firmware_version.h     FW_TITLE / FW_VERSION for OTA
│   └── *.h                    One header per module below
├── src/
│   ├── main.cpp               Boot sequence + main loop (core 1): automation, commands, state snapshot
│   ├── relay_rules.cpp        Relay I/O, modes, rules, persistence, AUTO logic, rules text format
│   ├── schedule_manager.cpp   Parses /schedule.csv, finds today's row
│   ├── clock_sync.cpp         RTC <-> system clock, NTP, applying a trusted time
│   ├── net.cpp                Network task (core 0): AP + WiFi station, link watchdog
│   ├── network_settings.cpp   WiFi and ThingsBoard settings in NVS
│   ├── thingsboard_link.cpp   TbLink: telemetry, attributes, RPC, events, OTA gate
│   ├── web_ui.cpp             Local web UI (inlined HTML/CSS/JS) + HTTP handlers
│   ├── event_log.cpp          Capped log in LittleFS, also sent to ThingsBoard
│   └── time_safety.cpp        RTC sanity checks, timestamp formatting
├── thingsboard/control-panel/ ThingsBoard dashboard widget (HTML + CSS + README)
├── resources/                 Sample schedule CSV and a UI screenshot
├── PROJECT.md                 Terse technical reference
└── lib/, test/                PlatformIO scaffold, unused
```

`/schedule.csv` is not part of the firmware image. It is uploaded at runtime through the local web page, so a freshly flashed board has no schedule until you upload one.

## Getting started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI).
- A DS3231 module on the ESP32's I²C pins, with its coin cell.
- A 4-channel active-low relay board on GPIO 2, 4, 5, 18.
- For remote access: a WiFi network and a device on the ThingsBoard server (`iot.boyoot.dev`).

### Build, upload, monitor

```powershell
C:\Users\albedair\.platformio\penv\Scripts\pio.exe run
C:\Users\albedair\.platformio\penv\Scripts\pio.exe run --target upload
C:\Users\albedair\.platformio\penv\Scripts\pio.exe device monitor
```

Upload uses `COM3` at 115200 from [platformio.ini](platformio.ini). The monitor runs at 115200 and shows boot steps, WiFi, NTP and ThingsBoard connection progress.

**Partition table:** the project uses `min_spiffs.csv` (two 1.9 MB firmware slots for OTA, 128 KB for files). A board that was flashed with the old default table loses its LittleFS files (schedule, rules, relay modes, event log) the first time it is flashed with this one over USB, because the file area moves. Re-upload the CSV and re-enter the rules once afterwards. Later updates, by USB or OTA, keep the files.

### First boot checklist

1. Flash the firmware and watch the serial monitor for `LittleFS ready`, the RTC status and `SoftAP IP: 192.168.4.1`.
2. Connect a phone or laptop to WiFi `PrayerTimer-Setup` (password in [Configuration](#configuration)) and browse to `http://192.168.4.1`.
3. The page syncs the RTC from the browser's clock on load. With internet access later, NTP keeps it correct.
4. Upload a `schedule.csv` covering the current date.
5. Add rules per relay and click **Save Automation Rules**.
6. Optional, for remote access: open **Settings** (`/network`), enter your WiFi, then the ThingsBoard device access token. See [ThingsBoard](#thingsboard).

## Configuration

Compile-time constants live in [include/app_config.h](include/app_config.h):

```cpp
constexpr const char *AP_SSID = "PrayerTimer-Setup";
constexpr const char *AP_PASSWORD = "password123";
constexpr const char *NETWORK_HOSTNAME = "prayertimer";     // prayertimer.local on the LAN
constexpr long TIME_UTC_OFFSET_SECONDS = 2 * 3600;          // RTC keeps UTC+2 standard time

constexpr int RELAY_COUNT = 4;
constexpr int PRAYER_COUNT = 5;
constexpr int MAX_RULES = 10;                                // per relay
constexpr int RELAY_PINS[RELAY_COUNT] = {2, 4, 5, 18};

constexpr unsigned long STARTUP_SAFE_DELAY_MS = 10000;       // relays held OFF after boot
constexpr int WATCHDOG_TIMEOUT_SECONDS = 30;                 // covers a ~7 s TLS handshake

constexpr int RTC_MIN_VALID_YEAR = 2024;
constexpr int RTC_MAX_VALID_YEAR = 2035;                     // bump well before 2035
constexpr int EVENT_LOG_MAX_ENTRIES = 100;
```

Runtime settings (WiFi and ThingsBoard) are entered on the `/network` page and stored in NVS; see [Storage layout](#storage-layout). The firmware version for OTA is in [include/firmware_version.h](include/firmware_version.h).

## Network and time

### WiFi

- The setup AP `PrayerTimer-Setup` (`192.168.4.1`) is **always on**, also while the board is connected to WiFi, so the local page stays reachable on site even if the router changes.
- With a WiFi network saved, the board joins it as a station. It is then also reachable at its LAN IP and at `http://prayertimer.local`.
- With no network saved, the board is AP-only and behaves like the original offline design.
- Station retries run every 15 s, slowing to every 2 minutes while someone is connected to the AP. Each retry scans across channels, which briefly drops AP clients, and that would get in the way of fixing a wrong password.
- **Dead-link watchdog:** the ESP32 WiFi driver can keep reporting "connected" after the link has died. While ThingsBoard is not connected, the board pings the gateway once a minute; two failed checks in a row force a WiFi rejoin. It never reboots for this, because it drives relays.

### Time

- The DS3231 keeps **Egypt standard time (UTC+2) all year**. The schedule CSV and the rules use the same time base, so they never shift at DST changes.
- The ESP32 system clock keeps UTC. At boot it is set from a trusted RTC, so TLS certificate checks work before NTP answers.
- All code reads the time through `currentTime()` in [src/clock_sync.cpp](src/clock_sync.cpp): the RTC when it reads sane, otherwise the system clock once NTP or a browser sync has set it since boot.
- When NTP syncs (on WiFi, roughly hourly) and the RTC is off by 2 s or more, or automation is currently off, the RTC is corrected through `applyClockSync()` in [src/clock_sync.cpp](src/clock_sync.cpp). The browser sync on page load uses the same function. A correction reloads today's schedule, re-evaluates automation and restores relay states. It is logged only when automation turns on or off, or the clock jumps by more than 2 minutes.
- The browser sync converts the visitor's clock to Egypt standard time with a hard-coded DST window (`isEgyptDST()` in the inlined JS in [src/web_ui.cpp](src/web_ui.cpp), and a copy in [src/main.cpp](src/main.cpp) for the compile-time fallback). **This is Egypt-specific.** NTP doesn't need it, because it applies the fixed UTC+2 offset.

## ThingsBoard

The connection uses [TbLink](https://github.com/boyoot-dev/thingsboard-link) (PubSubClient + HTTPClient), pinned by commit in [platformio.ini](platformio.ini). Bump the hash to pick up TbLink changes.

### Setup

1. Create a device in ThingsBoard and copy its **access token**.
2. On the board's `/network` page, paste the token into the ThingsBoard form and save. The defaults match the server:
   - MQTT `mqtt.iot.boyoot.dev:8883` with TLS. This is a DNS-only name, because the main domain is behind Cloudflare, which doesn't forward MQTT.
   - Firmware downloads from `iot.boyoot.dev:80` over **plain HTTP**. HTTPS through the Cloudflare Tunnel hangs the ESP32's TLS handshake. The firmware checksum still arrives over the TLS MQTT channel and is verified before flashing.
3. Add the dashboard widget from [thingsboard/control-panel/](thingsboard/control-panel/). Its README has the steps.

Changing ThingsBoard settings on a board that is already connected restarts it (TbLink can't switch servers live), which turns every relay OFF for the startup delay. The page warns about this.

### Connection behavior

- TLS uses ISRG Root X1 ([include/ca_certs.h](include/ca_certs.h)). The board doesn't attempt the TLS connect until its clock is valid (from the RTC or NTP); with the clock at 1970 every certificate looks "not yet valid".
- A handshake verifies a P-384 chain in software: about 5–9 s on good WiFi, up to ~14 s on a weak link, capped at 30 s by TbLink. The first attempt after boot usually ends in `state=-4` (the server's reply arrives after the 3 s wait) and the retry 15 s later succeeds.
- The board re-requests its firmware assignment every 30 minutes, because ThingsBoard doesn't reliably push a new assignment to a connected device.

### What the board publishes

| Kind | Key | Notes |
|---|---|---|
| Telemetry | `relay1_state`…`relay4_state` | `true` = ON |
| Telemetry | `relay1_mode`…`relay4_mode` | `auto` / `on` / `off` |
| Telemetry | `rtc_ok`, `schedule_loaded`, `automation_enabled` | |
| Telemetry | `event` | Each event-log entry; up to 16 are held while offline |
| Telemetry | `heartbeat` | Every 8 s, from TbLink; the widget's Online/Offline uses it |
| Telemetry | `current_fw_title`, `current_fw_version` | On each connect, from TbLink |
| Attribute | `fajr`, `dhuhr`, `asr`, `maghrib`, `isha` | Today's times, `HH:MM` |
| Attribute | `relay1_rules`…`relay4_rules` | Compact rules text, see below |
| Attribute | `ip`, `rssi`, `uptime_s` | Every 60 s |
| Attribute | `fw_waiting`, `auto_update` | On connect and whenever they change |

State is published when it changes (checked every 250 ms), in full on every connect, and again every 5 minutes.

### Commands (RPC)

| Method | Params | Effect |
|---|---|---|
| `set_relay_mode` | `{"relay": 1-4, "mode": "auto"\|"on"\|"off"}` | Same as the local AUTO/ON/OFF buttons |
| `set_rules` | `{"relay": 1-4, "rules": "1+10:1;1+20:0"}` | Replaces that relay's rules; `""` clears them |
| `fw_check` | `{}` | Re-requests the assigned firmware now; replies `current`, `waiting`, `auto_update`. No restart |
| `fw_install` | `{}` | Lets a waiting update install with auto-update off (still only on idle relays) |
| `set_auto_update` | `{"enabled": true\|false}` | Turns automatic installs on/off (saved in NVS) |
| `reboot` | `{}` | Restarts the board after 1 s (relays OFF for the startup delay) |

Replies are `{"success": true}` once the command is queued, or `{"success": false, "error": "..."}` for bad parameters. The result shows up in the telemetry and attributes above.

**Rules text** is `<prayer><+/-offset>:<1 on|0 off>` joined by `;`, prayers 0–4 = Fajr..Isha. `1+10:1;1+20:0` = ON 10 min after Dhuhr, OFF 20 min after Dhuhr. It is compact so each relay's rules are one short attribute (10 rules fit in 89 characters) and trivial to parse on the device; the panel shows them in words.

### Firmware updates (OTA)

1. Bump `FW_VERSION` in [include/firmware_version.h](include/firmware_version.h) and build.
2. In ThingsBoard → OTA updates, upload `.pio/build/esp32dev/firmware.bin` with title **`prayer-timer`** and the same version string. Both must match byte-for-byte.
3. Assign the package to the device (or its profile). ThingsBoard usually pushes the assignment to the connected board immediately; as a fallback the board re-asks every 30 minutes and on every reconnect. **Check for update** (panel or local `/network` page) re-asks at once.
4. The board installs it only when **all relays are OFF and no AUTO rule is due within 15 minutes**, and, if **auto-update** is off, only after someone presses **Install** (panel, local page, or `fw_install`). Until then, `fw_waiting` shows the version. Installing downloads (~16 s), verifies the SHA256 checksum and **restarts by itself**; no manual reboot is needed.

Auto-update is on by default. Turn it off on the `/network` page's Firmware card or with the panel's Auto-update switch (`set_auto_update`) to control exactly when a board restarts.

Verified on the bench (1.1.0 → 1.1.1): a package titled differently was ignored, the correctly titled one was pushed live, downloaded in 16 s, verified and installed with an automatic restart.

A package with a different title is ignored, so a binary for another board can never be flashed onto this one.

The board installs whatever version is assigned whenever it differs from the running one, so **assigning an older version rolls a board back**. It also means a board flashed over USB with a different version than the one assigned to it in ThingsBoard reinstalls the assigned version as soon as it connects. For bench work over USB, unassign the firmware first.

## Schedule CSV format

Uploaded as `/schedule.csv` from the local page. One row per calendar day, no header. Parsing is in [src/schedule_manager.cpp](src/schedule_manager.cpp):

```
month,day,fajr_minutes,dhuhr_minutes,asr_minutes,maghrib_minutes,isha_minutes
```

All five times are **minutes past midnight** in Egypt standard time, e.g. `04:35` → `275`.

```csv
1,1,275,730,930,1040,1180
1,2,275,730,930,1041,1181
```

- Rows are matched by `month`/`day` only, with no year, so the same file is reused every year.
- Blank lines and lines starting with `-` or containing `Content-` are skipped (defensive against multipart boundary text from a malformed upload). Lines with fewer than 6 commas are skipped silently. Values are not range-checked.
- No row for today means automation is disabled for the day: AUTO relays are held OFF.
- The file is scanned linearly on each load (boot, day rollover, upload, time sync). Fine for ~365 rows.
- The CSV can only be uploaded locally. At about 10 KB it is far too large for TbLink's 512-byte incoming message limit.

## Relay automation rules

Each relay has up to `MAX_RULES` (10) rules:

```cpp
struct RelayRule {
    int prayerIndex;   // 0=Fajr .. 4=Isha, -1 = empty slot
    int offsetMins;    // negative = before the prayer
    int targetState;   // RELAY_ON_STATE or RELAY_OFF_STATE
};
```

**Rules are one-shot state changes, not durations.** A relay with only an ON rule stays ON. A 10-minute pump run after Dhuhr needs two rules:

| Offset | Prayer | Action |
|---|---|---|
| `+10` min | Dhuhr | Turn ON |
| `+20` min | Dhuhr | Turn OFF |

Rules can be edited on the local page (all relays saved together) or from ThingsBoard (`set_rules`, one relay per call). Both paths save `/rules.bin`, apply the new rules immediately and republish `relayN_rules`. Whichever side saves last wins.

Rules take effect in two ways:

1. **Minute tick:** `applyRelayRules()` runs once per changed minute and fires rules whose `prayerTime + offset` equals the current minute, for AUTO relays only.
2. **State reconstruction:** `computeAutoRelayStateAtTime()` picks the latest rule at or before the current minute. It runs on boot, day rollover, CSV upload, rule saves, mode switches and time syncs. This is how a reboot at 15:21 still leaves a relay ON whose ON rule fired at 15:20.

## AUTO / Manual override modes

| Mode | Behavior |
|---|---|
| `AUTO` | Follows the rules; held OFF when automation is disabled |
| `MANUAL ON` | Forced ON, always |
| `MANUAL OFF` | Forced OFF, always |

Manual modes don't depend on the clock, so they apply even without a valid RTC or schedule, and survive reboots (`/relay_state.bin`). Only AUTO relays are held OFF when time or the schedule can't be trusted. Modes can be set from the local page (`/relay`) or ThingsBoard (`set_relay_mode`), both through `changeRelayMode()`. The event log records the source: `(web)` or `(ThingsBoard)`.

## Local web UI (HTTP API)

Routes are registered in `configureWebServer()` in [src/web_ui.cpp](src/web_ui.cpp). None require authentication; see [Security notes](#security-notes--known-limitations).

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | Main page: clock, RTC/schedule/automation/WiFi status, prayer times, CSV upload, relay cards with rule tables, event log |
| `POST` | `/` | With `save_rules`, saves all relays' rules (fields `r{relay}i{index}_p/_o/_s`) |
| `GET` | `/relay` | `relay` (0–3) + `mode` (0 AUTO, 1 MANUAL ON, 2 MANUAL OFF) |
| `GET` | `/status` | JSON `{"relays":[{"mode":0-2,"on":bool,"rules":"<rules text>"}]}`; polled by the main page every 3 s |
| `GET` | `/time-sync` | `y, mo, d, h, mi, s`: set the RTC from the browser (called on every page load) |
| `POST` | `/upload` | Multipart field `update`: replaces `/schedule.csv` |
| `GET` | `/network` | Network status (WiFi, AP, NTP, ThingsBoard) and the WiFi + ThingsBoard forms |
| `POST` | `/network` | Save WiFi settings (`ssid`, `password`, `static`, `ip`, `gateway`, `subnet`, `dns`) and rejoin |
| `POST` | `/thingsboard` | Save ThingsBoard settings (`token`, `host`, `port`, `tls`, `http_host`, `http_port`, `clear_token`) |
| `POST` | `/firmware` | `action=save` (with `auto_update`), `check`, or `install` |

Password and token fields are never sent back to the browser. Leaving them blank keeps the saved value; for WiFi, only while the network name is unchanged.

The main page updates relay modes and ON/OFF badges live from `/status`, so changes made from ThingsBoard appear within 3 s. Rules changed elsewhere also refresh live, unless that relay's rule table has unsaved local edits (it no longer matches what the page loaded). In that case the page shows a "reload" notice on that relay instead of discarding the edits. Saving the edited table would still overwrite the newer rules.

The page is streamed in chunks (`sendContent`) to keep RAM use low. Arduino's `WebServer` handles one client at a time, so a phone holding an idle connection open can delay other requests by a few seconds.

## Storage layout

### LittleFS (128 KB, `min_spiffs.csv`)

| Path | Written by | Format |
|---|---|---|
| `/schedule.csv` | `POST /upload` | Plain CSV, see above |
| `/rules.bin` | `saveRulesToStorage()` | Raw dump of `relayRules[RELAY_COUNT][MAX_RULES]`, **not versioned**. Changing `RelayRule`, `RELAY_COUNT` or `MAX_RULES` misreads existing files silently. |
| `/relay_state.bin` | `saveRelaySettingsToStorage()` | Modes + last states, with magic `0x50545231` and version `1`; falls back to AUTO/OFF if invalid |
| `/events.log` | `appendEventLog()` | One line per event, capped at 100 |

### NVS (Preferences)

| Namespace | Keys |
|---|---|
| `network` | `ssid`, `pass`, `static`, `ip`, `gw`, `mask`, `dns` |
| `thingsboard` | `token`, `host`, `port`, `tls`, `http_host`, `http_port`, `auto_update` |

NVS lives in its own partition and survives both firmware updates and LittleFS reformatting.

## Safety design

The firmware is conservative because it drives relays unattended, often a pump:

- **Relays are OFF the instant pins are configured**, before anything else runs.
- **10 s startup delay** before any relay state is restored, to avoid chatter during brownout reboot loops. The watchdog is fed throughout.
- **AUTO needs trusted time and a schedule.** Trusted time is a sane RTC (2024–2035, no lost-power flag), or, if the RTC is missing or unusable, NTP or a browser sync since boot. Without it, or without a CSV row for today, AUTO relays are held OFF.
- **Manual modes always apply**, independent of the clock.
- **Reboot-safe reconstruction** of AUTO states (see [rules](#relay-automation-rules)).
- **Task watchdog**, 30 s, fed in every loop iteration. 30 s rather than the original 15 s covers a TLS handshake.
- **Firmware updates wait** until all relays are OFF and no AUTO rule is due within 15 minutes.
- **No network failure touches the relays.** WiFi recovery rejoins instead of rebooting, and ThingsBoard being unreachable changes nothing on the device.
- **Event log** in flash (and in ThingsBoard) records boot, RTC problems, schedule loads, time syncs, uploads, rule saves, mode changes and every relay transition with its cause.

## Firmware architecture

The work is split across the ESP32's two cores so that network stalls never delay the relays or the local page:

| Core | Task | Owns |
|---|---|---|
| 1 | Arduino `loop()` | All app state: relays, modes, rules, schedule, RTC (I²C), LittleFS, the local web server |
| 0 | `network` task ([src/net.cpp](src/net.cpp)) | WiFi station management, gateway pings, TbLink (MQTT, TLS handshakes, OTA downloads) |

The two sides only exchange data, never call into each other's state:

- **Core 1 → core 0:** every 250 ms the loop reports a `DeviceState` snapshot (relay states/modes, status flags, prayer times, whether a restart is safe) under a spinlock. Rule changes report a formatted rules snapshot, and event-log entries go into a 16-slot queue.
- **Core 0 → core 1:** `set_relay_mode` and `set_rules` are validated on core 0 and put in a FreeRTOS queue. The loop drains it and applies each command through the same functions the web buttons use.
- **NTP:** the SNTP callback only sets a flag; the loop task writes the RTC.
- Only the network task touches the MQTT client. Other code reads a `volatile bool` connected flag, because asking a TLS client whether it's connected from another task blocks during a handshake.

Modules:

- [src/main.cpp](src/main.cpp): boot (LittleFS → relays OFF → watchdog → startup delay → saved modes/rules → RTC → schedule → network + web server) and the loop (web clients, queued commands, state report, once-a-second RTC check, NTP correction, day rollover, minute tick).
- [src/relay_rules.cpp](src/relay_rules.cpp): relay I/O, modes, rules, persistence, AUTO reconstruction, the rules text format, and the "rule due soon" check for OTA.
- [src/clock_sync.cpp](src/clock_sync.cpp): `currentTime()` (RTC, or system clock fallback), system clock from RTC, SNTP start and callback, `applyClockSync()`.
- [src/net.cpp](src/net.cpp) and [src/network_settings.cpp](src/network_settings.cpp): network task, WiFi, link watchdog, settings in NVS.
- [src/thingsboard_link.cpp](src/thingsboard_link.cpp): TbLink setup, publishing, RPC handling, event forwarding, OTA gate.
- [src/web_ui.cpp](src/web_ui.cpp), [src/schedule_manager.cpp](src/schedule_manager.cpp), [src/event_log.cpp](src/event_log.cpp), [src/time_safety.cpp](src/time_safety.cpp): as named.

Module boundaries are by convention (`include/<name>.h` per `.cpp`); globals like `schedule`, `relayRules`, `relayModes` are shared via `extern` and must only be touched from core 1.

## Security notes / known limitations

- **The local page is now reachable from the whole LAN** when the board is on WiFi, not just from the AP. It has no authentication, and `/relay` and `/time-sync` are plain `GET`s with no CSRF protection, so any device on the network, or a malicious page opened in a browser on it, can change relay modes or the clock.
- **Hardcoded AP credentials.** `PrayerTimer-Setup` / `password123` are the same on every board and are in this repo's history.
- **The ThingsBoard token** is stored unencrypted in NVS. Anyone with USB access to the board can read it.
- **OTA binaries travel over plain HTTP.** Integrity is protected by the checksum, which arrives over TLS MQTT, but the binary itself isn't confidential.
- **Egypt-only DST logic** in the browser sync, duplicated in C++ and JS.
- **`/rules.bin` is unversioned** (see [Storage layout](#storage-layout)).
- **First connect after boot:** a few local page requests can take 3–8 s during the first ThingsBoard connect. The loop on core 1 was measured not to stall; the delay is in the network stack while the board is new on WiFi. It doesn't affect relay timing.

## Development notes

- **No unit tests** (`test/` holds PlatformIO's stub). Good host-test candidates: `computeAutoRelayStateAtTime()`, `parseRelayRules()`/`formatRelayRules()`, `hasAutoRuleWithin()`, CSV parsing, `isRtcTimeSane()`. There is no host C++ compiler on the dev machine yet.
- **No CI.** Build locally with `pio run`.
- **TbLink:** to work on it alongside this project, set `symlink://../TbLink` in `lib_deps` temporarily, then push TbLink and pin the new commit hash.
- **Serial capture from scripts:** opening the port with pyserial's defaults resets the board (DTR/RTS). Set both to inactive before opening.
- **Compile-time constants** (pins, counts, timeouts) need a re-flash; changing `RELAY_COUNT`/`MAX_RULES` also invalidates `/rules.bin`.
- **The widget** in [thingsboard/control-panel/](thingsboard/control-panel/) must match the keys and RPC names in [src/thingsboard_link.cpp](src/thingsboard_link.cpp). Change both together.

## Roadmap / ideas for later

- Version `/rules.bin` like `/relay_state.bin`.
- Basic authentication on the local page, now that it is reachable from the LAN.
- Validate CSV minute values (0–1439) at load time.
- A native PlatformIO test environment for the pure-logic functions above.

## License

No license file is included, so the code defaults to "all rights reserved". Add a `LICENSE` file (e.g. MIT) to permit reuse.
