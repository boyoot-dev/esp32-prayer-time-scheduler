#include "web_ui.h"

#include <LittleFS.h>

#include "app_config.h"
#include "clock_sync.h"
#include "event_log.h"
#include "net.h"
#include "network_settings.h"
#include "relay_rules.h"
#include "schedule_manager.h"
#include "thingsboard_link.h"
#include "time_safety.h"

namespace
{
WebServer *webServer = nullptr;
RTC_DS3231 *clockRtc = nullptr;
bool *automationEnabled = nullptr;
File uploadFile;

const char *DAY_NAMES[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

String formatDate(const DateTime &now)
{
    int monthIndex = now.month() - 1;
    if (monthIndex < 0 || monthIndex > 11)
        monthIndex = 0;

    return String(DAY_NAMES[now.dayOfTheWeek()]) + ", " + String(now.day()) + " " + MONTH_NAMES[monthIndex] + " " + String(now.year());
}

String escapeHtml(String value)
{
    value.replace("&", "&amp;");
    value.replace("<", "&lt;");
    value.replace(">", "&gt;");
    value.replace("\"", "&quot;");
    value.replace("'", "&#39;");
    return value;
}

String relayStateText(int state)
{
    return state == RELAY_ON_STATE ? "ON" : "OFF";
}

bool isAutomationEnabled()
{
    return automationEnabled != nullptr && *automationEnabled;
}

void restoreAllRelaysFromCurrentModes(const DateTime &now)
{
    int currentMinsPastMidnight = (now.hour() * 60) + now.minute();
    bool canUseAutomation = isAutomationEnabled() && schedule.loaded && isRtcTimeSane(now);
    restoreRelayStatesForNow(currentMinsPastMidnight, schedule.prayerMins, now, canUseAutomation);
}

void saveRulesFromRequest()
{
    clearAllRules();

    for (int r = 0; r < RELAY_COUNT; r++)
    {
        int ruleCounter = 0;
        for (int i = 0; i < MAX_RULES; i++)
        {
            String prefix = "r" + String(r) + "i" + String(i);
            if (webServer->hasArg(prefix + "_p"))
            {
                int pIdx = webServer->arg(prefix + "_p").toInt();
                if (pIdx != -1 && ruleCounter < MAX_RULES)
                {
                    relayRules[r][ruleCounter].prayerIndex = pIdx;
                    relayRules[r][ruleCounter].offsetMins = webServer->arg(prefix + "_o").toInt();
                    relayRules[r][ruleCounter].targetState = webServer->arg(prefix + "_s").toInt() == RELAY_ON_STATE ? RELAY_ON_STATE : RELAY_OFF_STATE;
                    ruleCounter++;
                }
            }
        }
    }

    saveRulesToStorage();
    DateTime now = currentTime(*clockRtc);
    appendEventLog(now, "Automation rules saved (web)");
    restoreAllRelaysFromCurrentModes(now);
    reportRelayRules();
}

void sendPageHeader()
{
    webServer->sendContent(R"raw(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Prayer Automation Hub</title>
    <style>
        *{box-sizing:border-box} body{margin:0;font-family:Arial,sans-serif;background:#eef2f6;color:#111827;padding:14px;text-align:left} .app{max-width:560px;margin:0 auto 24px} h1{font-size:29px;line-height:1.08;margin:22px 4px 18px;text-align:center;letter-spacing:0} h2{font-size:21px;margin:0 0 14px} .card{background:#fff;border:1px solid #e7ecf2;border-radius:18px;box-shadow:0 8px 22px rgba(15,23,42,.08);padding:18px;margin:14px 0}.top-card{text-align:center}.date{font-size:15px;font-weight:700;color:#475569;margin-bottom:10px}.clock{font-size:34px;line-height:1;font-weight:800;color:#2563eb}.note{font-size:13px;color:#64748b;margin:8px 0 14px}.status-line{display:flex;justify-content:center;gap:8px;align-items:center;font-size:14px;font-weight:700;margin-top:8px}.status-ok{color:#15803d}.status-bad{color:#dc2626}.prayer-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;margin-top:18px}.prayer-name{font-size:13px;color:#64748b;font-weight:800}.prayer-time{font-size:18px;font-weight:800;margin-top:5px}.upload-row{display:grid;gap:12px}.file-input{width:100%;font-size:15px}.btn{border:0;border-radius:12px;min-height:44px;padding:10px 12px;font-size:14px;font-weight:800;color:#fff;background:#2563eb;cursor:pointer}.btn.active{outline:3px solid rgba(37,99,235,.25)}.btn-secondary{background:#475569}.btn-green{background:#16a34a}.btn-red{background:#e11d48}.btn-light{background:#e2e8f0;color:#0f172a}.relay-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}.relay-badges{display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end}.relay-status{font-size:12px;font-weight:900;border-radius:999px;padding:6px 10px;background:#e2e8f0;color:#475569}.relay-status.on{background:#dcfce7;color:#15803d}.relay-status.off{background:#fee2e2;color:#be123c}.manual-controls{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-bottom:12px}.rules-table,.rules-table tbody{display:block;width:100%}.rule-row{display:grid;grid-template-columns:1fr 86px 1fr 44px;gap:8px;align-items:end;padding:12px 0;border-top:1px solid #eef2f7}.rule-row td{display:block;padding:0}.rule-row select,.rule-row input{width:100%;height:42px;border:1px solid #cbd5e1;border-radius:10px;background:#fff;padding:0 10px;font-size:15px}.offset-wrap{display:flex;align-items:center;gap:6px}.offset-wrap input{min-width:0}.unit{font-size:12px;color:#64748b;font-weight:800}.del-btn{width:42px;height:42px;border:0;border-radius:12px;background:#ef4444;color:#fff;font-size:20px;font-weight:800}.log-box{font-family:Consolas,monospace;font-size:12px;line-height:1.45;white-space:pre-wrap;max-height:260px;overflow:auto;background:#f8fafc;border:1px solid #e2e8f0;border-radius:12px;padding:12px;color:#334155}.field{display:grid;gap:4px;font-size:13px;font-weight:800;color:#475569}.field input{height:42px;border:1px solid #cbd5e1;border-radius:10px;padding:0 10px;font-size:15px}.rules-notice{margin:0 0 12px;padding:10px 12px;border-radius:10px;background:#fef3c7;color:#92400e;font-size:13px;font-weight:700}.save-card{position:sticky;bottom:10px;z-index:2;padding:12px;background:rgba(255,255,255,.94);backdrop-filter:blur(6px)}@media(min-width:520px){body{padding:22px}h1{font-size:34px}.card{padding:22px}.clock{font-size:38px}.prayer-time{font-size:20px}.rule-row{grid-template-columns:1fr 100px 1fr 48px}}
    </style>
    <script>
    function setRelayMode(relayIdx, mode) {
        fetch('/relay?relay=' + relayIdx + '&mode=' + mode)
            .then(function(resp) {
                if(!resp.ok) throw new Error('relay');
                location.reload();
            })
            .catch(function() { alert('Relay command failed.'); });
    }
    // Modes and states can change from ThingsBoard while this page is open.
    var MODE_LABELS = ['AUTO', 'MANUAL ON', 'MANUAL OFF'];
    function applyStatus(status) {
        status.relays.forEach(function(relay, i) {
            var modeEl = document.getElementById('relayMode' + i);
            var stateEl = document.getElementById('relayStatus' + i);
            if(!modeEl || !stateEl) return;
            modeEl.textContent = MODE_LABELS[relay.mode];
            stateEl.textContent = relay.on ? 'ON' : 'OFF';
            stateEl.className = 'relay-status ' + (relay.on ? 'on' : 'off');
            for(var m = 0; m < 3; m++) {
                var btn = document.getElementById('modeBtn' + i + '_' + m);
                if(btn) btn.classList.toggle('active', m === relay.mode);
            }
            if(window.pageRules && relay.rules !== window.pageRules[i]) {
                // Refresh the table unless it has unsaved edits; never discard those.
                if(localRulesText(i) === window.pageRules[i]) {
                    showRules(i, relay.rules);
                    window.pageRules[i] = relay.rules;
                } else {
                    document.getElementById('rulesNotice' + i).hidden = false;
                }
            }
        });
    }
    // The table in the device's compact rules text ("1+10:1;1+20:0"), in slot order as
    // Save would store it. Form state values: 0 = ON (active-low LOW), 1 = OFF.
    function localRulesText(relayIdx) {
        var parts = [];
        for(var slot = 0; slot < 10; slot++) {
            var id = 'r' + relayIdx + 'i' + slot;
            if(!document.getElementById(id + '_tr')) continue;
            var p = document.getElementsByName(id + '_p')[0].value;
            var o = parseInt(document.getElementsByName(id + '_o')[0].value, 10) || 0;
            var s = document.getElementsByName(id + '_s')[0].value;
            parts.push(p + (o >= 0 ? '+' : '') + o + ':' + (s == 0 ? 1 : 0));
        }
        return parts.join(';');
    }
    function showRules(relayIdx, text) {
        var table = document.getElementById('ruleTable' + relayIdx);
        while(table.rows.length) table.deleteRow(0);
        if(!text) return;
        text.split(';').forEach(function(part) {
            var m = /^(\d)([+-]\d+):([01])$/.exec(part);
            if(m) addRuleRow(relayIdx, Number(m[1]), Number(m[2]), m[3] === '1' ? 0 : 1);
        });
    }
    function pollStatus() {
        fetch('/status').then(function(resp) { return resp.json(); }).then(applyStatus).catch(function() {});
    }
    function formatClock(seconds) {
        var h = Math.floor(seconds / 3600) % 24;
        var m = Math.floor(seconds / 60) % 60;
        var s = seconds % 60;
        return String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
    }
    function initLiveClock(startSeconds) {
        var el = document.getElementById('liveClock');
        if(!el) return;
        if(window.liveClockTimer) clearInterval(window.liveClockTimer);
        var seconds = startSeconds;
        el.textContent = formatClock(seconds);
        window.liveClockTimer = setInterval(function() {
            seconds++;
            if(seconds >= 86400) { location.reload(); return; }
            el.textContent = formatClock(seconds);
        }, 1000);
    }
    function isEgyptDST(month, day) {
        if(month < 4 || month > 10) return false;
        if(month > 4 && month < 10) return true;
        if(month == 4) return day >= 24;
        if(month == 10) return day < 29;
        return false;
    }
    function browserStandardTime() {
        var now = new Date();
        if(isEgyptDST(now.getMonth() + 1, now.getDate())) {
            now = new Date(now.getTime() - 3600000);
        }
        return now;
    }
    function syncClockFromBrowser() {
        var now = browserStandardTime();
        var seconds = now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds();
        initLiveClock(seconds);
        fetch('/time-sync?y=' + now.getFullYear() + '&mo=' + (now.getMonth() + 1) + '&d=' + now.getDate() + '&h=' + now.getHours() + '&mi=' + now.getMinutes() + '&s=' + now.getSeconds()).catch(function() {});
    }
    function addRuleRow(relayIdx, prayerIdx, offset, state) {
        var table = document.getElementById('ruleTable' + relayIdx);
        var rowCount = table.getElementsByClassName('rule-row').length;
        if(rowCount >= 10) { alert('Limit reached.'); return; }
        var ruleSlot = 0;
        while(ruleSlot < 10 && document.getElementById('r' + relayIdx + 'i' + ruleSlot + '_tr')) { ruleSlot++; }
        if(ruleSlot >= 10) { alert('Limit reached.'); return; }
        var rowId = 'r' + relayIdx + 'i' + ruleSlot;
        var row = table.insertRow(-1); row.className = 'rule-row'; row.id = rowId + '_tr';
        var pNames = ['Fajr', 'Dhuhr', 'Asr', 'Maghrib', 'Isha'];
        var cell1 = row.insertCell(0);
        cell1.innerHTML = '<select name="' + rowId + '_s"><option value="0"' + (state==0?' selected':'') + '>Turn ON</option><option value="1"' + (state==1?' selected':'') + '>Turn OFF</option></select>';
        var cell2 = row.insertCell(1);
        cell2.innerHTML = '<div class="offset-wrap"><input type="number" name="' + rowId + '_o" value="' + offset + '"><span class="unit">min</span></div>';
        var cell3 = row.insertCell(2);
        var s2 = '<select name="' + rowId + '_p">';
        for(var p=0; p<5; p++) { s2 += '<option value="' + p + '"' + (prayerIdx==p?' selected':'') + '>' + pNames[p] + '</option>'; }
        s2 += '</select>'; cell3.innerHTML = s2;
        var cell4 = row.insertCell(3);
        cell4.innerHTML = '<button type="button" class="del-btn" onclick="document.getElementById(\'' + rowId + '_tr\').remove()">X</button>';
    }
    </script>
</head>
<body><div class="app">
    <h1>Prayer Automation Hub</h1>
)raw");
}

void sendClockCard(const DateTime &now)
{
    int secondsPastMidnight = (now.hour() * 3600) + (now.minute() * 60) + now.second();
    bool rtcOk = isRtcTimeSane(now);
    bool autoOk = isAutomationEnabled() && rtcOk && schedule.loaded;

    String clockHtml = "<div class='card top-card'>";
    clockHtml += "<div id='todayDate' class='date'>Today: " + formatDate(now) + "</div>";
    clockHtml += "<div id='liveClock' class='clock'>--:--:--</div>";
    clockHtml += "<script>initLiveClock(" + String(secondsPastMidnight) + ");</script>";
    clockHtml += "<p class='note'>UTC+2 - Standard Winter Time</p>";
    if (usingNtpFallback())
        clockHtml += "<div class='status-line'>RTC <span class='status-bad'>Not working - using network time</span></div>";
    else
        clockHtml += "<div class='status-line'>RTC <span class='" + String(rtcOk ? "status-ok'>Sane" : "status-bad'>Invalid") + "</span></div>";
    clockHtml += "<div class='status-line'>Calendar <span class='" + String(schedule.loaded ? "status-ok'>Synced" : "status-bad'>Missing file") + "</span></div>";
    clockHtml += "<div class='status-line'>Automation <span class='" + String(autoOk ? "status-ok'>Enabled" : "status-bad'>Disabled") + "</span></div>";
    NetworkStatus net = getNetworkStatus();
    clockHtml += "<div class='status-line'>WiFi <span class='" + String(net.wifiConnected ? "status-ok'>" + net.ip : "status-bad'>Not connected") + "</span> <a href='/network'>Settings</a></div>";
    webServer->sendContent(clockHtml);

    if (schedule.loaded)
    {
        webServer->sendContent("<div class='prayer-grid'>");
        const char *names[] = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"};
        for (int i = 0; i < PRAYER_COUNT; i++)
        {
            webServer->sendContent("<div><div class='prayer-name'>" + String(names[i]) + "</div><div class='prayer-time'>" + formatMinsToTime(schedule.prayerMins[i]) + "</div></div>");
        }
        webServer->sendContent("</div>");
    }

    webServer->sendContent("</div>");
}

void sendUploadForm()
{
    webServer->sendContent(R"raw(
    <div class='card'>
        <h2>Schedule CSV</h2>
        <form method='POST' action='/upload' enctype='multipart/form-data' class='upload-row'>
            <input class='file-input' type='file' name='update' accept='.csv'>
            <input type='submit' class='btn btn-green' value='Upload New File'>
        </form>
    </div>
    <form method='POST' action='/'>
)raw");
}

void sendRelayCards()
{
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        int relayState = getRelayState(r);
        bool relayOn = relayState == RELAY_ON_STATE;
        RelayMode mode = getRelayMode(r);
        String card = "<div class='card'>";
        card += "<div class='relay-head'><h2>Relay " + String(r + 1) + "</h2><div class='relay-badges'>";
        card += "<span id='relayMode" + String(r) + "' class='relay-status'>" + String(relayModeLabel(mode)) + "</span>";
        card += "<span id='relayStatus" + String(r) + "' class='relay-status " + String(relayOn ? "on'>ON" : "off'>OFF") + "</span></div></div>";
        card += "<div class='manual-controls'>";
        card += "<button type='button' id='modeBtn" + String(r) + "_0' class='btn btn-light" + String(mode == RELAY_MODE_AUTO ? " active" : "") + "' onclick='setRelayMode(" + String(r) + ",0)'>AUTO</button>";
        card += "<button type='button' id='modeBtn" + String(r) + "_1' class='btn btn-green" + String(mode == RELAY_MODE_MANUAL_ON ? " active" : "") + "' onclick='setRelayMode(" + String(r) + ",1)'>ON</button>";
        card += "<button type='button' id='modeBtn" + String(r) + "_2' class='btn btn-red" + String(mode == RELAY_MODE_MANUAL_OFF ? " active" : "") + "' onclick='setRelayMode(" + String(r) + ",2)'>OFF</button>";
        card += "</div>";
        card += "<div id='rulesNotice" + String(r) + "' class='rules-notice' hidden>Rules were changed from ThingsBoard while you were editing. <a href='/'>Reload</a> to see them; saving here would overwrite them.</div>";
        card += "<table class='rules-table' id='ruleTable" + String(r) + "'></table>";
        card += "<button type='button' class='btn btn-secondary' onclick='addRuleRow(" + String(r) + ",0,0,0)'>Add Rule</button>";
        card += "</div>";
        webServer->sendContent(card);
    }

    webServer->sendContent("<div class='card save-card'><input type='submit' name='save_rules' class='btn' value='Save Automation Rules'></div></form>");
}

void sendEventLogCard()
{
    String logText = readEventLog();
    if (logText.length() == 0)
        logText = "No events yet.";

    webServer->sendContent("<div class='card'><h2>Event Log</h2><div class='log-box'>" + escapeHtml(logText) + "</div></div>");
}

void sendSavedRulesScript()
{
    char rulesText[RULES_TEXT_MAX];
    String rulesScript = "<script>var pageRules = [";
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        formatRelayRules(r, rulesText, sizeof(rulesText));
        rulesScript += String(r ? "," : "") + "'" + rulesText + "'";
    }
    rulesScript += "];</script>";
    webServer->sendContent(rulesScript);

    webServer->sendContent("<script>window.onload = function() {syncClockFromBrowser();setInterval(pollStatus, 3000);");
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        for (int i = 0; i < MAX_RULES; i++)
        {
            if (relayRules[r][i].prayerIndex != -1)
            {
                String initScript = "addRuleRow(" + String(r) + "," + String(relayRules[r][i].prayerIndex) + "," + String(relayRules[r][i].offsetMins) + "," + String(relayRules[r][i].targetState) + ");";
                webServer->sendContent(initScript);
            }
        }
    }
    webServer->sendContent("};</script></div></body></html>");
    webServer->sendContent("");
}

void handleRoot()
{
    if (webServer->hasArg("save_rules"))
    {
        saveRulesFromRequest();
        webServer->sendHeader("Location", "/");
        webServer->send(303, "text/html", "<html><body>Redirecting...</body></html>");
        return;
    }

    DateTime now = currentTime(*clockRtc);

    webServer->sendHeader("Cache-Control", "no-cache");
    webServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer->send(200, "text/html", "");

    sendPageHeader();
    sendClockCard(now);
    sendUploadForm();
    sendRelayCards();
    sendEventLogCard();
    sendSavedRulesScript();
}

void handleRelayControl()
{
    if (!webServer->hasArg("relay"))
    {
        webServer->send(400, "text/plain", "Missing relay");
        return;
    }

    int relayIndex = webServer->arg("relay").toInt();
    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
    {
        webServer->send(400, "text/plain", "Invalid relay");
        return;
    }

    RelayMode mode = RELAY_MODE_AUTO;
    if (webServer->hasArg("mode"))
    {
        int rawMode = webServer->arg("mode").toInt();
        if (rawMode == RELAY_MODE_MANUAL_ON)
            mode = RELAY_MODE_MANUAL_ON;
        else if (rawMode == RELAY_MODE_MANUAL_OFF)
            mode = RELAY_MODE_MANUAL_OFF;
    }
    else if (webServer->hasArg("state"))
    {
        mode = webServer->arg("state").toInt() == RELAY_ON_STATE ? RELAY_MODE_MANUAL_ON : RELAY_MODE_MANUAL_OFF;
    }
    else
    {
        webServer->send(400, "text/plain", "Missing mode");
        return;
    }

    DateTime now = currentTime(*clockRtc);
    int currentMinsPastMidnight = (now.hour() * 60) + now.minute();
    bool canUseAutomation = isAutomationEnabled() && schedule.loaded && isRtcTimeSane(now);
    changeRelayMode(relayIndex, mode, currentMinsPastMidnight, schedule.prayerMins, now, canUseAutomation, "web");
    webServer->send(200, "text/plain", relayStateText(getRelayState(relayIndex)));
}

void handleTimeSync()
{
    if (!webServer->hasArg("y") || !webServer->hasArg("mo") || !webServer->hasArg("d") || !webServer->hasArg("h") || !webServer->hasArg("mi") || !webServer->hasArg("s"))
    {
        webServer->send(400, "text/plain", "Missing time fields");
        return;
    }

    int year = webServer->arg("y").toInt();
    int month = webServer->arg("mo").toInt();
    int day = webServer->arg("d").toInt();
    int hour = webServer->arg("h").toInt();
    int minute = webServer->arg("mi").toInt();
    int second = webServer->arg("s").toInt();

    if (year < RTC_MIN_VALID_YEAR || year > RTC_MAX_VALID_YEAR || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
    {
        webServer->send(400, "text/plain", "Invalid time fields");
        return;
    }

    DateTime synced(year, month, day, hour, minute, second);
    applyClockSync(*clockRtc, *automationEnabled, synced, "browser");
    webServer->send(200, "text/plain", "OK");
}

void handleFileUpload()
{
    HTTPUpload &upload = webServer->upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        uploadFile = LittleFS.open("/schedule.csv", "w");
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (uploadFile && upload.currentSize > 0)
            uploadFile.write(upload.buf, upload.currentSize);
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (uploadFile)
        {
            uploadFile.close();
            DateTime now = currentTime(*clockRtc);
            bool loaded = loadTodaySchedule(now.month(), now.day());
            if (automationEnabled != nullptr)
                *automationEnabled = isRtcTimeSane(now) && loaded;
            appendEventLog(now, String("Schedule CSV uploaded; automation ") + (isAutomationEnabled() ? "enabled" : "disabled"));
            restoreAllRelaysFromCurrentModes(now);
        }
    }
}

void handleUploadSuccess()
{
    webServer->sendHeader("Location", "/");
    webServer->send(303, "text/html", "<html><body>Redirecting...</body></html>");
}

// Polled by the main page every 3 s. Rules use the compact text form, which is only
// digits and "+-:;", so it needs no JSON escaping.
void handleStatus()
{
    char rulesText[RULES_TEXT_MAX];
    String json = "{\"relays\":[";
    for (int r = 0; r < RELAY_COUNT; r++)
    {
        formatRelayRules(r, rulesText, sizeof(rulesText));
        json += String(r ? "," : "") + "{\"mode\":" + String(getRelayMode(r)) + ",\"on\":" + (getRelayState(r) == RELAY_ON_STATE ? "true" : "false") + ",\"rules\":\"" + rulesText + "\"}";
    }
    json += "]}";
    webServer->sendHeader("Cache-Control", "no-cache");
    webServer->send(200, "application/json", json);
}

void redirectTo(const char *location)
{
    webServer->sendHeader("Location", location);
    webServer->send(303, "text/html", "<html><body>Redirecting...</body></html>");
}

String textField(const char *label, const char *name, const String &value, const char *type = "text", const char *placeholder = "")
{
    return "<label class='field'>" + String(label) + "<input type='" + type + "' name='" + name + "' value='" + escapeHtml(value) + "' placeholder='" + placeholder + "'></label>";
}

String thingsBoardStatusHtml()
{
    if (isThingsBoardConnected())
        return "status-ok'>Connected";
    return isThingsBoardStarted() ? "status-bad'>Not connected" : "status-bad'>Not configured";
}

void sendThingsBoardForm()
{
    ThingsBoardSettings saved = loadThingsBoardSettings();
    String form = "<form method='POST' action='/thingsboard' class='card upload-row'><h2>ThingsBoard</h2>";
    form += "<p class='note'>Copy the access token from the device page in ThingsBoard. Leave it empty to turn the link off.";
    if (isThingsBoardStarted())
        form += " Saving restarts the board; relays go OFF for about 10 seconds.";
    form += "</p>";
    form += textField("Device access token", "token", "", "password", saved.token.length() ? "(unchanged)" : "");
    form += textField("MQTT host", "host", saved.host);
    form += textField("MQTT port", "port", String(saved.port), "number");
    form += "<label class='field'><span><input type='checkbox' name='tls' value='1'" + String(saved.useTls ? " checked" : "") + "> TLS (port 8883)</span></label>";
    form += textField("Firmware update host (plain HTTP)", "http_host", saved.httpHost);
    form += textField("Firmware update port", "http_port", String(saved.httpPort), "number");
    form += "<label class='field'><span><input type='checkbox' name='clear_token' value='1'> Remove saved token</span></label>";
    form += "<input type='submit' class='btn' value='Save ThingsBoard Settings'></form>";
    webServer->sendContent(form);
}

void handleThingsBoardSave()
{
    ThingsBoardSettings settings = loadThingsBoardSettings();
    String token = webServer->arg("token");
    token.trim();
    if (webServer->hasArg("clear_token"))
        settings.token = "";
    else if (token.length() > 0)
        settings.token = token;

    String host = webServer->arg("host");
    String httpHost = webServer->arg("http_host");
    host.trim();
    httpHost.trim();
    long port = webServer->arg("port").toInt();
    long httpPort = webServer->arg("http_port").toInt();
    if (host.length() > 0)
        settings.host = host;
    if (httpHost.length() > 0)
        settings.httpHost = httpHost;
    if (port > 0 && port <= 65535)
        settings.port = port;
    if (httpPort > 0 && httpPort <= 65535)
        settings.httpPort = httpPort;
    settings.useTls = webServer->hasArg("tls");

    saveThingsBoardSettings(settings);
    appendEventLog(currentTime(*clockRtc), settings.token.length() ? "ThingsBoard settings saved for " + settings.host : String("ThingsBoard link turned off"));
    requestThingsBoardReconfigure();
    redirectTo("/network?tb_saved=1");
}

void sendNetworkStatusCard(const NetworkSettings &saved)
{
    NetworkStatus net = getNetworkStatus();
    String card = "<div class='card'><h2>Network Status</h2>";
    if (net.wifiConnected)
        card += "<div class='status-line'>WiFi <span class='status-ok'>" + escapeHtml(net.ssid) + " - " + net.ip + " (" + String(net.rssi) + " dBm)</span></div>";
    else
        card += "<div class='status-line'>WiFi <span class='status-bad'>" + String(saved.ssid.length() ? "Not connected" : "Not configured") + "</span></div>";
    card += "<div class='status-line'>Setup AP <span class='status-ok'>" + String(AP_SSID) + " - " + net.apIp + " (" + String(net.apClients) + " connected)</span></div>";
    card += "<div class='status-line'>NTP <span class='" + String(hasNtpSynced() ? "status-ok'>Synced" : "status-bad'>Waiting") + "</span></div>";
    card += "<div class='status-line'>ThingsBoard <span class='" + thingsBoardStatusHtml() + "</span></div>";
    if (net.wifiConnected)
        card += "<p class='note'>On your network: http://" + net.ip + " or http://" + String(NETWORK_HOSTNAME) + ".local</p>";
    card += "</div>";
    webServer->sendContent(card);
}

void handleNetworkPage()
{
    NetworkSettings saved = loadNetworkSettings();

    webServer->sendHeader("Cache-Control", "no-cache");
    webServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer->send(200, "text/html", "");

    sendPageHeader();
    sendNetworkStatusCard(saved);

    if (webServer->hasArg("error"))
        webServer->sendContent("<div class='card status-bad'>Static IP, gateway and subnet (and DNS, if set) must be valid IPv4 addresses.</div>");
    else if (webServer->hasArg("saved"))
        webServer->sendContent("<div class='card status-ok'>Saved. Joining the network...</div>");
    else if (webServer->hasArg("tb_saved"))
        webServer->sendContent("<div class='card status-ok'>ThingsBoard settings saved.</div>");

    String form = "<form method='POST' action='/network' class='card upload-row'><h2>WiFi Network</h2>";
    form += "<p class='note'>The setup AP stays on either way. Leave the network name empty to run AP only.</p>";
    form += textField("Network name (SSID)", "ssid", saved.ssid);
    form += textField("Password", "password", "", "password", saved.password.length() ? "(unchanged)" : "");
    form += "<label class='field'><span><input type='checkbox' name='static' value='1'" + String(saved.useStaticIp ? " checked" : "") + "> Static IP</span></label>";
    form += textField("IP address", "ip", saved.ip, "text", "192.168.1.50");
    form += textField("Gateway", "gateway", saved.gateway, "text", "192.168.1.1");
    form += textField("Subnet mask", "subnet", saved.subnet, "text", "255.255.255.0");
    form += textField("DNS (optional)", "dns", saved.dns, "text", "same as gateway");
    form += "<input type='submit' class='btn' value='Save and Connect'>";
    form += "<a class='btn btn-light' href='/' style='text-align:center;text-decoration:none;line-height:24px'>Back</a></form>";
    webServer->sendContent(form);
    sendThingsBoardForm();

    webServer->sendContent("</div></body></html>");
    webServer->sendContent("");
}

void handleNetworkSave()
{
    NetworkSettings saved = loadNetworkSettings();
    NetworkSettings settings;
    settings.ssid = webServer->arg("ssid");
    settings.password = webServer->arg("password");
    settings.useStaticIp = webServer->hasArg("static");
    settings.ip = webServer->arg("ip");
    settings.gateway = webServer->arg("gateway");
    settings.subnet = webServer->arg("subnet");
    settings.dns = webServer->arg("dns");
    settings.ssid.trim();
    settings.ip.trim();
    settings.gateway.trim();
    settings.subnet.trim();
    settings.dns.trim();

    // A blank password keeps the saved one, unless the network itself changed.
    if (settings.password.length() == 0 && settings.ssid == saved.ssid)
        settings.password = saved.password;

    IPAddress check;
    bool staticIpValid = check.fromString(settings.ip) && check.fromString(settings.gateway) && check.fromString(settings.subnet) && (settings.dns.length() == 0 || check.fromString(settings.dns));
    if (settings.useStaticIp && !staticIpValid)
    {
        redirectTo("/network?error=1");
        return;
    }

    saveNetworkSettings(settings);
    appendEventLog(currentTime(*clockRtc), settings.ssid.length() ? "WiFi settings saved for " + settings.ssid : String("WiFi settings cleared; AP only"));
    requestNetworkReconnect();
    redirectTo("/network?saved=1");
}
}

void configureWebServer(WebServer &server, RTC_DS3231 &rtc, bool *automationEnabledFlag)
{
    webServer = &server;
    clockRtc = &rtc;
    automationEnabled = automationEnabledFlag;

    server.on("/", HTTP_GET, handleRoot);
    server.on("/", HTTP_POST, handleRoot);
    server.on("/relay", HTTP_GET, handleRelayControl);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/time-sync", HTTP_GET, handleTimeSync);
    server.on("/upload", HTTP_POST, handleUploadSuccess, handleFileUpload);
    server.on("/network", HTTP_GET, handleNetworkPage);
    server.on("/network", HTTP_POST, handleNetworkSave);
    server.on("/thingsboard", HTTP_POST, handleThingsBoardSave);
}

