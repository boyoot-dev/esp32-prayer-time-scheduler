# Prayer Automation Hub

## Overview

Prayer Automation Hub is an ESP32-based relay automation controller for switching pumps or similar loads according to daily prayer-time schedules.

The device runs as a standalone WiFi access point, hosts a local web UI at `192.168.4.1`, reads time from a DS3231 RTC module, stores configuration in LittleFS flash storage, and controls four active-low relay outputs.

The project is designed for local operation without internet access. A user connects to the ESP32 access point, uploads a schedule CSV, configures relay rules, and can manually override each relay when needed.

## Hardware Target

- Board: ESP32 Dev Module
- Framework: Arduino via PlatformIO
- RTC: DS3231, using Adafruit RTClib
- Storage: ESP32 LittleFS
- Relay outputs: 4 active-low GPIO outputs
- Default relay pins:
  - Relay 1: GPIO 2
  - Relay 2: GPIO 4
  - Relay 3: GPIO 5
  - Relay 4: GPIO 18

Active-low relay behavior:

- `LOW` means relay ON
- `HIGH` means relay OFF

## PlatformIO Configuration

The firmware is configured in `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_port = COM3
monitor_port = COM3
lib_deps = adafruit/RTClib @ 2.1.4
upload_speed = 115200
board_build.filesystem = littlefs
```

## Network Behavior

On boot, the ESP32 starts a WiFi SoftAP:

- SSID: `PrayerTimer-Setup`
- Password: `password123`
- Web UI address: `http://192.168.4.1`

The web UI is served from the ESP32 itself using Arduino `WebServer`.

## Core Features

### Prayer-Time Schedule Automation

The firmware reads `/schedule.csv` from LittleFS and loads the entry matching the current RTC month and day.

Each schedule row is expected to provide:

- Month
- Day
- Fajr time in minutes past midnight
- Dhuhr time in minutes past midnight
- Asr time in minutes past midnight
- Maghrib time in minutes past midnight
- Isha time in minutes past midnight

The loaded schedule is stored in memory as five prayer times.

### Relay Rules

Each relay can have up to 10 automation rules.

A rule contains:

- Prayer index: Fajr, Dhuhr, Asr, Maghrib, or Isha
- Offset in minutes
- Target state: ON or OFF

Example:

- Relay 3
- `Turn ON`
- `10 min`
- `Dhuhr`

This means Relay 3 turns ON 10 minutes after Dhuhr and remains ON until another rule turns it OFF.

Important behavior:

Rules are state-change commands, not durations. If a relay has only an ON rule and no later OFF rule, it will remain ON after that rule has passed.

### AUTO / Manual Override Modes

Each relay has a persistent mode:

- `AUTO`
- `MANUAL ON`
- `MANUAL OFF`

In `AUTO`, the relay follows schedule rules.

In `MANUAL ON`, the relay is forced ON and automation skips that relay.

In `MANUAL OFF`, the relay is forced OFF and automation skips that relay.

Manual modes are saved in flash, so they survive reboot.

### Startup Safety Delay

At startup, all relay pins are immediately configured and held OFF.

The firmware waits for `STARTUP_SAFE_DELAY_MS`, currently 10 seconds, before restoring any relay state or applying automation.

This reduces relay chatter during power recovery.

### RTC Sanity Check

Automation is disabled if the RTC is not safe to trust.

The firmware checks:

- RTC device must be present
- DS3231 must not report lost power
- Year must be within the configured sane range

Current sane RTC range:

- Minimum year: 2024
- Maximum year: 2035

If the RTC is invalid, relays are held OFF and automation stays disabled until a valid browser time sync occurs.

### Browser Time Sync

When the web UI loads, the browser sends the current local time to `/time-sync`.

The firmware adjusts the DS3231 RTC from that browser-provided time if the submitted values pass validation.

The UI includes Egypt daylight-saving handling and syncs the RTC to UTC+2 standard time.

### Relay State Restoration

On boot or mode changes, the firmware restores relay outputs based on mode:

- Manual ON restores ON
- Manual OFF restores OFF
- AUTO calculates the intended state from today's schedule and all rules up to the current minute

This prevents missed schedule events after power loss.

Example:

If Relay 1 had an ON rule at 15:20 and the ESP32 reboots at 15:21, AUTO mode can infer that Relay 1 should already be ON.

### Watchdog

The ESP32 task watchdog is enabled with a 15-second timeout.

The main loop feeds the watchdog before and after web-server handling. The startup safety delay also feeds the watchdog while waiting.

If firmware execution hangs long enough, the ESP32 will reboot automatically.

### Event Log

The firmware maintains a capped event log in LittleFS.

Log file:

- `/events.log`

Maximum entries:

- 100

Logged events include:

- Boot
- RTC ready
- RTC missing, invalid, or lost power
- Schedule loaded or missing
- Browser time sync
- Schedule CSV upload
- Automation rule saves
- Relay mode changes
- Relay ON/OFF changes caused by automation or restore logic

The event log is displayed in the web UI for troubleshooting.

## Web UI Features

The local web UI provides:

- Live clock display
- Current date
- RTC status
- Schedule file status
- Automation enabled/disabled status
- Today's prayer times
- Schedule CSV upload
- Per-relay mode controls:
  - AUTO
  - ON
  - OFF
- Per-relay automation rule editor
- Save automation rules button
- Event log viewer

## Firmware Modules

### `src/main.cpp`

Responsible for:

- System boot flow
- LittleFS startup
- Relay safe startup
- Watchdog setup and feeding
- RTC initialization and sanity checks
- Schedule loading
- Main automation loop
- Web server startup

### `src/relay_rules.cpp`

Responsible for:

- Relay pin setup
- Relay ON/OFF state control
- Active-low relay normalization
- Relay rule storage
- Relay mode persistence
- AUTO state calculation
- Manual override behavior
- Scheduled relay changes

### `src/schedule_manager.cpp`

Responsible for:

- Loading `/schedule.csv`
- Finding today's schedule row
- Converting prayer minutes to display text

### `src/web_ui.cpp`

Responsible for:

- Rendering the local HTML/CSS/JavaScript UI
- Handling rule-save requests
- Handling relay mode changes
- Handling browser time sync
- Handling schedule CSV upload
- Showing system status and event log

### `src/event_log.cpp`

Responsible for:

- Appending events to `/events.log`
- Keeping only the latest 100 entries
- Reading the log for display in the web UI

### `src/time_safety.cpp`

Responsible for:

- RTC sanity validation
- Timestamp formatting for logs

## LittleFS Files

The firmware uses these files in ESP32 flash:

- `/schedule.csv`: uploaded prayer-time calendar
- `/rules.bin`: saved relay automation rules
- `/relay_state.bin`: saved relay modes and last known relay states
- `/events.log`: capped troubleshooting event log

## Safety Design

The firmware is intentionally conservative:

- Relays default OFF during boot
- Startup delay prevents immediate relay switching during unstable power recovery
- Automation is blocked if RTC time is not sane
- Automation is blocked if today's schedule is missing
- Manual override modes take priority over schedule automation
- AUTO mode reconstructs expected relay state after reboot
- Watchdog recovers from software hangs
- Event log records key state transitions for troubleshooting

## Important Operational Notes

Relay rules are persistent state changes. For pump-style operation, every ON rule should usually have a matching OFF rule.

Example 10-minute pump run after Dhuhr:

- `Turn ON`, `10 min`, `Dhuhr`
- `Turn OFF`, `20 min`, `Dhuhr`

If only the ON rule exists, the relay will remain ON until another rule or manual command turns it OFF.

## Build Command

From the project root:

```powershell
C:\Users\albedair\.platformio\penv\Scripts\pio.exe run
```

## Upload Command

From the project root:

```powershell
C:\Users\albedair\.platformio\penv\Scripts\pio.exe run --target upload
```

## Serial Monitor

```powershell
C:\Users\albedair\.platformio\penv\Scripts\pio.exe device monitor
```

Default monitor speed:

- `115200`

## Current Production Commit History

- `00c01ab` - Initial production baseline
- `89f9cf6` - `prod-2026-06-24-19-47`

