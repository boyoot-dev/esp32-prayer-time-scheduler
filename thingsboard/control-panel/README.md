# Control Panel widget (ThingsBoard)

One ThingsBoard "HTML Card" widget for a whole Prayer Timer board:

- Online/Offline (from the firmware's 8 s `heartbeat`) and RTC / Schedule / Automation chips
- Today's prayer times
- Each relay's live ON/OFF state and AUTO / ON / OFF mode buttons
- Firmware version, IP, WiFi signal, uptime, and a Reboot button

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
| Reboot | `reboot` | `{}` |

Both are two-way RPCs (`POST /api/plugins/rpc/twoway/{deviceId}`), so errors from the
board, or no answer at all, show up as a message under the panel. A successful
`set_relay_mode` only means the board queued the change. The button stays outlined until
the matching `relayN_mode` telemetry arrives (up to 10 s), so the panel always shows what
the board actually did. Reboot asks for confirmation first, because restarting switches
every relay OFF for the 10 s startup safety delay.

## Telemetry and attributes it reads

| Kind | Keys |
|---|---|
| Telemetry | `heartbeat`, `relay1_state`…`relay4_state`, `relay1_mode`…`relay4_mode`, `rtc_ok`, `schedule_loaded`, `automation_enabled`, `current_fw_version` |
| Client attributes | `fajr`, `dhuhr`, `asr`, `maghrib`, `isha`, `ip`, `rssi`, `uptime_s` |

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
