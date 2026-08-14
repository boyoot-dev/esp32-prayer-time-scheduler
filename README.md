# Prayer Automation Hub

An ESP32-based, standalone relay automation controller that switches pumps or similar loads according to daily prayer-time schedules — no internet, no cloud, no app. It hosts its own WiFi access point and a local web UI for configuration, manual override, and troubleshooting.

> This README is written for future-you and for AI coding agents picking this project back up. It documents not just *what* the code does, but *why* it's structured this way, so changes can be made safely.

For a more terse, structured technical reference of the same information, see [PROJECT.md](PROJECT.md) (kept in sync manually — if the two disagree, trust the source code, then update whichever doc is stale).

---

## Table of Contents

- [What it does](#what-it-does)
- [Hardware](#hardware)
- [Repository layout](#repository-layout)
- [Getting started](#getting-started)
- [Configuration](#configuration)
- [Using the device](#using-the-device)
- [Schedule CSV format](#schedule-csv-format)
- [Relay automation rules](#relay-automation-rules)
- [AUTO / Manual override modes](#auto--manual-override-modes)
- [Web UI reference (HTTP API)](#web-ui-reference-http-api)
- [Flash storage layout (LittleFS)](#flash-storage-layout-littlefs)
- [Safety design](#safety-design)
- [Security notes / known limitations](#security-notes--known-limitations)
- [Firmware architecture](#firmware-architecture)
- [Development notes](#development-notes)
- [Roadmap / ideas for later](#roadmap--ideas-for-later)
- [License](#license)

---

## What it does

The device is designed to run unattended, controlling relays (e.g. water pump for ablution/wudu, irrigation, lighting) around the five daily Islamic prayer times (Fajr, Dhuhr, Asr, Maghrib, Isha), which shift by a few minutes every day across the year.

Instead of hardcoding times or requiring an internet connection for a prayer-time API, the workflow is:

1. You generate a full-year prayer-time calendar for your location (any source — an Islamic prayer-time site/app/spreadsheet) as a CSV.
2. You upload that CSV once (or whenever you want) through the device's local web UI.
3. You define automation rules per relay (e.g. "turn Relay 3 ON 10 minutes after Dhuhr, turn it OFF 20 minutes after Dhuhr").
4. The ESP32 keeps time via a battery-backed DS3231 RTC, reads today's row from the CSV every day at midnight, and fires relay state changes at the configured offsets — entirely locally, with no WiFi internet required.

It's built to be conservative about safety: relays default OFF, there's a startup delay, a watchdog, RTC sanity checks, and reboot-safe state restoration (see [Safety design](#safety-design)).

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32 Dev Module (generic devkit) |
| Framework | Arduino, via PlatformIO |
| RTC | DS3231 (battery-backed), via [Adafruit RTClib](https://github.com/adafruit/RTClib) `2.1.4` |
| Storage | Internal flash, LittleFS filesystem |
| Relay outputs | 4 channels, **active-low** |

### Default pin map

| Relay | GPIO |
|---|---|
| Relay 1 | 2 |
| Relay 2 | 4 |
| Relay 3 | 5 |
| Relay 4 | 18 |

Active-low means `digitalWrite(pin, LOW)` energizes the relay (ON) and `HIGH` de-energizes it (OFF) — matches common 4-channel relay boards that switch on a LOW signal. Pin numbers and relay count are defined in [include/app_config.h](include/app_config.h) and can be changed there before flashing (all other code reads `RELAY_PINS`/`RELAY_COUNT`, nothing else needs editing for a different pin map).

There's no wiring diagram or photo in this repo yet — if you add one, put it under a `docs/` folder and link it here.

## Repository layout

```
.
├── platformio.ini          PlatformIO project/board/library config
├── include/
│   ├── app_config.h         All tunable constants: pins, timings, WiFi creds, limits
│   ├── event_log.h
│   ├── relay_rules.h
│   ├── schedule_manager.h
│   ├── time_safety.h
│   └── web_ui.h
├── src/
│   ├── main.cpp              Boot sequence + main loop (orchestrator)
│   ├── relay_rules.cpp       Relay I/O, modes, rules, persistence, AUTO logic
│   ├── schedule_manager.cpp  Parses /schedule.csv, finds today's row
│   ├── web_ui.cpp             Entire local web UI (HTML/CSS/JS inlined) + HTTP handlers
│   ├── event_log.cpp          Append-only capped log in LittleFS
│   └── time_safety.cpp        RTC sanity checks, timestamp formatting
├── lib/                      Empty — for private PlatformIO libraries (PlatformIO stub README only)
├── test/                     Empty — PlatformIO Unity test runner slot (PlatformIO stub README only)
├── .agents/                   Empty (present, currently unused)
└── PROJECT.md                 Dense technical reference, same subject as this README
```

There is intentionally no `data/` directory for LittleFS image upload — `/schedule.csv` is not baked into the firmware image; it's written to flash at runtime exclusively via the web UI's upload form. A fresh flash + fresh filesystem has *no* schedule until you upload one.

## Getting started

### Prerequisites

- [PlatformIO](https://platformio.org/) — either the VS Code extension or the CLI (`pio`).
- A USB connection to the ESP32 dev board.
- A DS3231 RTC module wired to the ESP32's I²C pins (SDA/SCL — default Wire pins for your board), with its coin-cell battery installed so time survives power loss.
- 4-channel active-low relay board wired to GPIO 2, 4, 5, 18 (or your customized pins).

### Clone

```powershell
git clone git@github.com:boyoot-dev/esp32-prayer-time-scheduler.git
cd esp32-prayer-time-scheduler
```

### Build

```powershell
C:\Users\albedair\.platformio\penv\Scripts\pio.exe run
```

(Or, if `pio` is on your PATH: `pio run`. If PlatformIO is installed somewhere else on a future machine, adjust the path or just use the VS Code PlatformIO extension's build button.)

### Upload firmware

```powershell
C:\Users\albedair\.platformio\penv\Scripts\pio.exe run --target upload
```

Uses `upload_port = COM3` / `upload_speed = 115200` from [platformio.ini](platformio.ini) — update `upload_port` if the board enumerates on a different COM port on your machine.

### Serial monitor

```powershell
C:\Users\albedair\.platformio\penv\Scripts\pio.exe device monitor
```

Runs at `monitor_speed = 115200`. Useful for watching boot logs (RTC status, LittleFS mount, WiFi AP IP, etc.) — the same events are also written to the in-flash event log visible in the web UI.

### First boot checklist

1. Flash the firmware.
2. Watch the serial monitor — confirm `LittleFS ready`, RTC status, and `SoftAP IP: 192.168.4.1`.
3. From a phone/laptop, connect to WiFi SSID `PrayerTimer-Setup` (see [Configuration](#configuration) for the password).
4. Browse to `http://192.168.4.1`.
5. The page auto-syncs the ESP32's RTC from your browser's clock on load (see [Browser Time Sync](#web-ui-reference-http-api)) — do this once to get a valid RTC.
6. Upload a `schedule.csv` covering the current date (see format below).
7. Add automation rules per relay and click **Save Automation Rules**.

## Configuration

All tunables live in [include/app_config.h](include/app_config.h):

```cpp
constexpr const char *WIFI_SSID = "PrayerTimer-Setup";
constexpr const char *WIFI_PASSWORD = "password123";

constexpr int RELAY_COUNT = 4;
constexpr int PRAYER_COUNT = 5;
constexpr int MAX_RULES = 10;          // per relay

constexpr int RELAY_PINS[RELAY_COUNT] = {2, 4, 5, 18};
constexpr int RELAY_ON_STATE = LOW;
constexpr int RELAY_OFF_STATE = HIGH;

constexpr unsigned long STARTUP_SAFE_DELAY_MS = 10000;   // 10s relays held OFF after boot
constexpr int WATCHDOG_TIMEOUT_SECONDS = 15;

constexpr int RTC_MIN_VALID_YEAR = 2024;
constexpr int RTC_MAX_VALID_YEAR = 2035;   // RTC readings outside this range are treated as invalid
constexpr int EVENT_LOG_MAX_ENTRIES = 100;
```

Any change here requires a re-flash (`pio run --target upload`) — these are `constexpr`, not runtime-configurable from the web UI.

`RTC_MAX_VALID_YEAR = 2035` is a ceiling baked into the firmware, not just documentation — if this device is still running past 2035, the RTC sanity check will start rejecting valid time and disabling automation. Bump this constant well before then.

## Using the device

The device serves everything from itself at `http://192.168.4.1` once connected to its access point — no internet, no external DNS, no app required. The single-page web UI shows:

- Live clock (client-side ticking, corrected on page load/reload) and today's date
- RTC status (Sane/Invalid), schedule file status (Synced/Missing), automation status (Enabled/Disabled)
- Today's 5 prayer times (once a schedule is loaded)
- CSV upload form
- Per-relay: current mode badge, ON/OFF badge, AUTO/ON/OFF buttons, and an editable rule table
- A single "Save Automation Rules" button that saves **all** relays' rule tables at once
- An event log panel for troubleshooting

## Schedule CSV format

Uploaded as `/schedule.csv` via the web UI. One row per calendar day, no header row expected. Parsing is in [src/schedule_manager.cpp](src/schedule_manager.cpp):

```
month,day,fajr_minutes,dhuhr_minutes,asr_minutes,maghrib_minutes,isha_minutes
```

- `month`: 1–12
- `day`: 1–31
- All five prayer times are **minutes past midnight** (e.g. `04:35` → `275`), local standard time (see [DST handling](#web-ui-reference-http-api) below).

Example (excerpt):

```csv
1,1,275,730,930,1040,1180
1,2,275,730,930,1041,1181
1,3,276,730,931,1042,1182
```

Parsing details worth knowing before you touch this code:

- Lines are matched to today's date by `month`/`day` only — **no year field**. The same file is reused every year; you must re-upload if the calendar's actual prayer-time minutes shift (e.g. from a moon-sighting/authority change) — the code does not interpolate or correct for anything.
- The parser skips blank lines and any line starting with `-` or containing `Content-` (defensive against stray multipart/form boundary text getting saved if an upload is malformed) — see [src/schedule_manager.cpp:28](src/schedule_manager.cpp#L28).
- If a line has fewer than 6 commas it's silently skipped as corrupt; there is **no validation** that parsed values are sane minute values (0–1439) at parse time — bad data will just produce a weird schedule, not an error. `formatMinsToTime` will render nonsense but the firmware won't crash.
- If no row matches today's `month`/`day`, `schedule.loaded` stays `false` and automation is disabled for the day (see [Safety design](#safety-design)).
- The whole file is scanned linearly on every schedule load (once per boot, once per day-of-month rollover, once per CSV upload, once per browser time-sync). Fine for a ~365-row file on ESP32; would need rework if this ever needs sub-day granularity or multi-year data.

## Relay automation rules

Each of the 4 relays can have up to `MAX_RULES` (10) rules. A rule is:

```cpp
struct RelayRule {
    int prayerIndex;   // 0=Fajr, 1=Dhuhr, 2=Asr, 3=Maghrib, 4=Isha
    int offsetMins;    // can be negative (before the prayer) or positive (after)
    int targetState;   // RELAY_ON_STATE or RELAY_OFF_STATE
};
```

**Rules are one-shot state-change commands fired at a specific minute, not durations.** If a relay only has an ON rule and no later OFF rule, it stays ON indefinitely once triggered — there is no implicit timeout.

Typical "run pump for 10 minutes after Dhuhr" pattern needs **two** rules on the same relay:

| Rule | Offset | Prayer | Action |
|---|---|---|---|
| 1 | `+10` min | Dhuhr | Turn ON |
| 2 | `+20` min | Dhuhr | Turn OFF |

Rules are edited in the web UI as a per-relay table and all saved together via `POST /` with `save_rules` — see [handleRoot / saveRulesFromRequest in web_ui.cpp](src/web_ui.cpp#L64). The whole `relayRules` array is persisted as a raw binary blob to `/rules.bin` (see [Flash storage layout](#flash-storage-layout-littlefs)).

Rule matching for continuous automation happens two ways:

1. **Minute-tick firing** — `applyRelayRules()` runs once per changed minute in the main loop and fires any rule whose `prayerTime + offset == currentMinute` exactly, only for relays currently in `AUTO` mode. If the device is off or the loop stalls through that exact minute, the rule is missed *for the tick-based path* — see the next point for why that's still recovered.
2. **State reconstruction on demand** — `computeAutoRelayStateAtTime()` (used on boot, day rollover, CSV upload, mode switch, and time-sync) scans **all** rules for a relay and picks whichever qualifying rule (`target <= currentMinute`) has the latest `target` minute, i.e. it derives "what state *should* this relay be in right now" from scratch rather than replaying history. This is what makes the reboot-recovery behavior in [Safety design](#safety-design) work, and it's also why a relay can "jump" straight to the correct state immediately after a schedule upload or manual→AUTO mode switch, without waiting for the next exact-minute tick.

## AUTO / Manual override modes

Each relay independently holds one of three persistent modes (`RelayMode` enum in [include/relay_rules.h](include/relay_rules.h)):

| Mode | Behavior |
|---|---|
| `AUTO` (0) | Follows the rule table via the logic above |
| `MANUAL ON` (1) | Forced ON; automation ignores this relay entirely |
| `MANUAL OFF` (2) | Forced OFF; automation ignores this relay entirely |

Set from the web UI (`GET /relay?relay=N&mode=M`) or programmatically via `setRelayMode()`. Mode + last known state are saved to `/relay_state.bin` on every change, so manual overrides survive a reboot/power cycle.

## Web UI reference (HTTP API)

All routes are registered in [configureWebServer() in web_ui.cpp](src/web_ui.cpp#L429). There is no authentication on any of these — see [Security notes](#security-notes--known-limitations).

| Method | Path | Purpose | Params |
|---|---|---|---|
| `GET` | `/` | Renders the full HTML page (status, clock, prayer times, upload form, relay cards, event log) | — |
| `POST` | `/` | Same page render, but if `save_rules` is present in the form body, saves all relay rule tables first | form fields `r{relay}i{index}_p` / `_o` / `_s` per rule row, plus `save_rules` |
| `GET` | `/relay` | Change a relay's mode (and immediately apply/restore its state) | `relay` (0–3, required), `mode` (0=AUTO/1=MANUAL_ON/2=MANUAL_OFF) or legacy `state` (relay-state value) |
| `GET` | `/time-sync` | Adjusts the DS3231 RTC to the given time, reloads today's schedule for that date, and re-evaluates automation-enabled status | `y, mo, d, h, mi, s` (all required, range-validated) |
| `POST` | `/upload` | Multipart file upload; overwrites `/schedule.csv`, then reloads today's row and restores relay states | multipart field `update` |

Notable behaviors baked into these handlers:

- `/` returns HTML with `Cache-Control: no-cache` and streams content in chunks (`sendContent`, `CONTENT_LENGTH_UNKNOWN`) rather than building one big string — keeps ESP32 RAM usage down for a fairly large inlined page.
- **Browser time sync** happens automatically via JS on every page load (`syncClockFromBrowser()` in the inlined `<script>`, calling `/time-sync`). The browser computes Egypt **standard** time itself (subtracting an hour during the DST window before sending), so the RTC is always kept in UTC+2 standard time regardless of the visiting browser's own DST state. The DST window is hardcoded (`isEgyptDST()`, duplicated in both [src/main.cpp](src/main.cpp#L19) and inline JS in [src/web_ui.cpp](src/web_ui.cpp#L133)): April 24 through October 28 inclusive-ish (see exact boundary logic in code) — **this is Egypt-specific and will silently give wrong results in any other timezone/DST regime.** If you deploy this outside Egypt, both copies of `isEgyptDST()` need to be replaced or generalized.
- `/time-sync` only logs an event if automation was previously disabled or the time delta from the previous RTC reading exceeds 120 seconds — avoids spamming the event log from routine page-load re-syncs that only correct clock drift.
- `/relay` and `/time-sync` are plain `GET` requests with no CSRF protection — trivially triggerable by any device that can reach `192.168.4.1`, including via a malicious link if a browser on the AP network visits an attacker-controlled page. Low risk given the AP is closed and local-only, but worth knowing if this design is reused for something more exposed.

## Flash storage layout (LittleFS)

| Path | Written by | Format |
|---|---|---|
| `/schedule.csv` | Web UI upload (`POST /upload`) | Plain text CSV, see [above](#schedule-csv-format) |
| `/rules.bin` | `saveRulesToStorage()` | Raw memory dump of the entire `relayRules[RELAY_COUNT][MAX_RULES]` array — **not versioned**, no magic number. Changing the `RelayRule` struct layout, `RELAY_COUNT`, or `MAX_RULES` will corrupt/misread existing files silently. |
| `/relay_state.bin` | `saveRelaySettingsToStorage()` | Fixed struct with a magic number (`0x50545231`) and version (`1`) — safely rejects and falls back to defaults (`AUTO`, `OFF`) if the file is missing, wrong size, or the magic/version don't match. See [src/relay_rules.cpp:17](src/relay_rules.cpp#L17). |
| `/events.log` | `appendEventLog()` | Plain text, one line per event, capped at `EVENT_LOG_MAX_ENTRIES` (100) — oldest entries are dropped once the cap is hit |

If you ever change `RelayRule`'s fields or `MAX_RULES`/`RELAY_COUNT`, either bump `/rules.bin` to a versioned format (mirroring how `/relay_state.bin` already does it) or plan to erase/reformat the filesystem on next flash (`pio run --target erase` or reflashing with a filesystem format), otherwise the firmware will read garbage rules on first boot with new firmware.

## Safety design

The firmware leans conservative because it's driving relays unattended, often controlling a pump:

- **Relays are held OFF the instant pins are configured**, before anything else runs (`setupRelayPins()` in `setup()`).
- **10-second startup delay** (`STARTUP_SAFE_DELAY_MS`) before any relay state is restored, specifically to avoid relay chatter during flaky power recovery (e.g. brownouts causing rapid reboot loops). The watchdog is fed throughout this delay so it doesn't itself trigger a reboot.
- **Automation is blocked** unless: the RTC is present, hasn't lost power since last set, and reports a year within `[RTC_MIN_VALID_YEAR, RTC_MAX_VALID_YEAR]` — see `isRtcTimeSane()` in [src/time_safety.cpp](src/time_safety.cpp). If any of these fail, all relays are forced OFF and stay OFF until a valid browser time-sync occurs.
- **Automation is also blocked** if there's no schedule row for today (missing/incomplete CSV) — relays held OFF, not left in whatever state they were in.
- **Manual override always wins** over automation for that relay.
- **Reboot-safe AUTO reconstruction**: on boot (and on day rollover, CSV upload, and time-sync), AUTO relays don't wait for the next scheduled tick — `computeAutoRelayStateAtTime()` figures out what state each AUTO relay *should* be in right now, given all its rules and the current time, and applies it immediately. So a reboot at 15:21 with an ON rule at 15:20 still results in the relay being ON.
- **Hardware watchdog**, 15s timeout, fed at the top and bottom of every `loop()` iteration (and during the startup delay) — a firmware hang longer than 15s triggers an automatic ESP32 reboot.
- **Capped event log** in flash records boot, RTC state transitions, schedule load/miss, time syncs, CSV uploads, rule saves, mode changes, and every relay ON/OFF transition with its cause — primary tool for diagnosing "why did the pump/relay do that" after the fact, viewable directly in the web UI.

## Security notes / known limitations

This is a closed, local-only device by design (no internet access, no cloud dependency), which is the main mitigating factor for the items below. They're flagged here so nobody mistakes them for oversights if this project is extended or exposed more broadly:

- **Hardcoded WiFi AP credentials.** SSID `PrayerTimer-Setup` / password `password123` are compiled into the firmware ([include/app_config.h](include/app_config.h)) and are the same on every device built from this source, forever, unless changed and reflashed. Anyone with the password (which is now public in this repo's history) can join the AP.
- **No authentication on the web UI or any HTTP endpoint.** Anyone connected to the AP can view status, change relay modes, upload a new schedule, or resync the clock — there's no login, token, or IP allowlist.
- **No CSRF protection.** `/relay` and `/time-sync` are simple `GET` requests with no token, so any page loaded by a browser on the AP network could trigger relay changes just by referencing those URLs.
- **No TLS.** Everything is plain HTTP; irrelevant for confidentiality here since there's nothing sensitive in transit, but worth knowing if this pattern is copied into a project that does carry sensitive data.
- **Egypt-only DST logic**, duplicated in two places (C++ and inline JS) — see [Web UI reference](#web-ui-reference-http-api). Wrong outside Egypt's DST rules, and the two copies can drift out of sync if only one is edited.
- **`/rules.bin` has no format versioning** — see [Flash storage layout](#flash-storage-layout-littlefs). A firmware update that changes the rule struct without a matching filesystem wipe will misread saved rules.

None of this matters much for the intended use case (a single trusted household device on a closed AP), but treat all of the above as things to fix first if this is ever bridged onto a shared/home WiFi network, exposed to the internet, or given to less-trusted users.

## Firmware architecture

Roughly layered as: `main.cpp` orchestrates boot and the main loop, and delegates all actual work to focused modules.

- **[src/main.cpp](src/main.cpp)** — boot sequence (LittleFS mount → relay pins → watchdog → startup delay → load persisted rules/modes → RTC init → load today's schedule → start AP + web server), plus the main `loop()`: feed watchdog, handle web clients, once per second check RTC sanity / day rollover / apply due rules once per changed minute.
- **[src/relay_rules.cpp](src/relay_rules.cpp)** — everything relay-related: pin setup, active-low normalization, mode storage, rule storage, `computeAutoRelayStateAtTime()` (the core "what should this relay be doing right now" logic), and `applyRelayRules()` (the once-a-minute tick firing).
- **[src/schedule_manager.cpp](src/schedule_manager.cpp)** — CSV parsing and today's-row lookup, plus `formatMinsToTime()` for display.
- **[src/web_ui.cpp](src/web_ui.cpp)** — the entire web UI: HTML/CSS/JS is inlined as raw string literals and generated server-side (no separate frontend build step, no external assets, no SPA framework — deliberately simple for an embedded target). Also owns all HTTP route handlers.
- **[src/event_log.cpp](src/event_log.cpp)** — append + cap + read for `/events.log`.
- **[src/time_safety.cpp](src/time_safety.cpp)** — RTC sanity check and timestamp formatting shared by the rest of the codebase.

Module boundaries are enforced only by convention (each `.cpp` has a matching `.h` in `include/`, all included via `#include "name.h"`), not by namespaces — global state like `schedule`, `relayRules`, `relayModes` is shared directly across translation units via `extern` declarations.

## Development notes

- **No unit tests currently exist** (`test/` only has PlatformIO's stub README). Given the hardware dependencies (RTC, GPIO, flash), tests would likely need to target the pure-logic pieces that don't touch hardware directly — e.g. `computeAutoRelayStateAtTime()`, `loadTodaySchedule()`'s CSV parsing, `isRtcTimeSane()` — via PlatformIO's native/host test environment rather than on-device.
- **No CI configured.** Builds/verifies locally only via `pio run`.
- **`.vscode/`** has PlatformIO-generated `c_cpp_properties.json`, `extensions.json`, `launch.json` for IntelliSense/debugging — regenerate via the PlatformIO extension if they go stale after a library/board change.
- **`lib/`** and **`.agents/`** exist but are currently empty (PlatformIO scaffold directories / unused).
- When editing `include/app_config.h` constants (pins, counts, timeouts), remember these are compile-time constants — every change needs a re-flash, and changes to `RELAY_COUNT`/`MAX_RULES` also invalidate the existing unversioned `/rules.bin` on already-deployed devices (see [Flash storage layout](#flash-storage-layout-littlefs)).
- If you add a wiring diagram, hardware photos, or a real prayer-time CSV generation script/source, this README references good places for them (`docs/`, and a `Schedule CSV format` cross-link) — update those sections instead of leaving new undocumented files.

## Roadmap / ideas for later

Not committed to, just things that stood out while documenting the current design — revisit and prune this list as priorities become clear:

- Version `/rules.bin` the same way `/relay_state.bin` already is, to make future struct changes safe.
- Make the DST rule configurable (or at least documented as a single source of truth instead of two copies) if this is ever used outside Egypt.
- Add basic auth or a setup-time-only AP password change flow, if the device is ever expected to run somewhere less trusted than a private household.
- Consider validating parsed CSV minute values (0–1439) at load time instead of silently accepting out-of-range numbers.
- Add a native/host PlatformIO test environment for the pure-logic functions listed under [Development notes](#development-notes).

## License

No license file is currently included in this repository, which means it defaults to standard copyright — all rights reserved, and others aren't legally permitted to reuse, modify, or redistribute this code even though it's on GitHub. Add a `LICENSE` file (e.g. MIT) if you want to permit reuse.
