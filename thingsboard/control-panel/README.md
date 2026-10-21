# Control Panel widget (ThingsBoard)

One ThingsBoard "HTML Card" widget for a whole Prayer Timer board:

- Online/Offline (from the firmware's 8 s `heartbeat`) and RTC / Schedule / Automation chips
- Today's prayer times
- Each relay's live ON/OFF state and AUTO / ON / OFF mode buttons
- Each relay's rules in plain words, with an editor (the same rules the board's local
  page edits; whichever side saves last wins)
- The last 10 event-log entries
- Firmware version (plus any update waiting for the relays to go idle), IP, WiFi signal,
  uptime, and a Reboot button

## Setup

1. In ThingsBoard, open the device and copy its **id** (Device details → Copy device id;
   this is not the access token).
2. Dashboard → **+ Add widget** → **HTML widgets** → **HTML Card**.
3. Paste [control-panel.html](control-panel.html) into the HTML tab and
   [control-panel.css](control-panel.css) into the CSS tab.
4. Replace both `PASTE-DEVICE-ID-HERE` values (the `data-device-id` attribute and
   `DEVICE_ID` in the script) with the device id.

Add one widget per board.

## What the buttons send

| Button | RPC method | Params |
|---|---|---|
| AUTO / ON / OFF | `set_relay_mode` | `{"relay": 1-4, "mode": "auto"\|"on"\|"off"}` |
| Rules → Save | `set_rules` | `{"relay": 1-4, "rules": "1+10:1;1+20:0"}` |
| Check for update | `fw_check` | `{}` |
| Auto-update switch | `set_auto_update` | `{"enabled": true\|false}` |
| Install (shown when an update waits and auto-update is off) | `fw_install` | `{}` |
| Reboot | `reboot` | `{}` |

All are two-way RPCs (`POST /api/plugins/rpc/twoway/{deviceId}`), so errors from the
board, or no answer at all, show up as a message under the panel. A successful reply only
means the board queued the change. The panel waits for the board to report the result
(`relayN_mode` telemetry, or `relayN_rules` with exactly the text it sent) for up to 10 s,
so it always shows what the board actually did.

**Rules text** is `<prayer><+/-offset>:<1 on|0 off>` joined by `;`, prayers 0-4 =
Fajr, Dhuhr, Asr, Maghrib, Isha. `1+10:1;1+20:0` = ON 10 min after Dhuhr, OFF 20 min
after Dhuhr. Up to 10 rules per relay, offsets -1439..1439. It is this compact because
TbLink publishes each attribute through a 128-byte buffer. Sending `""` clears a relay's
rules. Reboot asks for confirmation first, because restarting switches
every relay OFF for the 10 s startup safety delay.

## Telemetry and attributes it reads

| Kind | Keys |
|---|---|
| Telemetry | `heartbeat`, `relay1_state`…`relay4_state`, `relay1_mode`…`relay4_mode`, `rtc_ok`, `schedule_loaded`, `automation_enabled`, `current_fw_version`, `event` |
| Client attributes | `fajr`, `dhuhr`, `asr`, `maghrib`, `isha`, `relay1_rules`…`relay4_rules`, `ip`, `rssi`, `uptime_s`, `fw_waiting`, `auto_update` |

The event list's first load uses the REST range query
(`/api/plugins/telemetry/DEVICE/{id}/values/timeseries?keys=event&limit=10&orderBy=DESC`);
new events then arrive over the WebSocket.

These match what [src/thingsboard_link.cpp](../../src/thingsboard_link.cpp) publishes.

## Why it's built this way

The widget follows esp32-mqtt-generic-board's board-overview widget, whose approach was
verified against ThingsBoard 4.2.1.1 CE:

- DOM lookups are scoped through the `data-device-id` root element because `self.ctx`
  and `document.currentScript` are unusable in static HTML Card widgets.
- Live values come from one WebSocket `ENTITY_DATA` subscription. `query.latestValues`
  must be present even when empty, or the server throws. The initial snapshot arrives
  under `data.data`, and live pushes arrive under `update` with only the changed keys.
- Offline is decided client-side: no heartbeat point newer than 25 s means Offline.
  ThingsBoard's own `active` attribute only goes stale on a ~60 s server schedule.
  The heartbeat's own timestamp is used, not the time it was received, so a stale
  snapshot on page load doesn't show a dead board as Online.
- The JWT comes from `localStorage`/`sessionStorage` (`jwt_token`), the same way the
  other projects' widgets authenticate REST and WebSocket calls.
