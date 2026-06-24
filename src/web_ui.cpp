#include "web_ui.h"

#include <LittleFS.h>

#include "app_config.h"
#include "relay_rules.h"
#include "schedule_manager.h"

namespace
{
WebServer *webServer = nullptr;
RTC_DS3231 *clockRtc = nullptr;
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
                    relayRules[r][ruleCounter].targetState = webServer->arg(prefix + "_s").toInt();
                    ruleCounter++;
                }
            }
        }
    }

    saveRulesToStorage();
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
        *{box-sizing:border-box} body{margin:0;font-family:Arial,sans-serif;background:#eef2f6;color:#111827;padding:14px;text-align:left} .app{max-width:560px;margin:0 auto 24px} h1{font-size:29px;line-height:1.08;margin:22px 4px 18px;text-align:center;letter-spacing:0} h2{font-size:21px;margin:0 0 14px} .card{background:#fff;border:1px solid #e7ecf2;border-radius:18px;box-shadow:0 8px 22px rgba(15,23,42,.08);padding:18px;margin:14px 0}.top-card{text-align:center}.date{font-size:15px;font-weight:700;color:#475569;margin-bottom:10px}.clock{font-size:34px;line-height:1;font-weight:800;color:#2563eb}.note{font-size:13px;color:#64748b;margin:8px 0 14px}.status-line{display:flex;justify-content:center;gap:8px;align-items:center;font-size:14px;font-weight:700}.status-ok{color:#15803d}.status-bad{color:#dc2626}.prayer-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;margin-top:18px}.prayer-name{font-size:13px;color:#64748b;font-weight:800}.prayer-time{font-size:18px;font-weight:800;margin-top:5px}.upload-row{display:grid;gap:12px}.file-input{width:100%;font-size:15px}.btn{border:0;border-radius:12px;min-height:44px;padding:10px 14px;font-size:15px;font-weight:800;color:#fff;background:#2563eb;cursor:pointer}.btn-secondary{background:#475569}.btn-green{background:#16a34a}.btn-red{background:#e11d48}.btn-light{background:#e2e8f0;color:#0f172a}.relay-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:14px}.relay-status{font-size:12px;font-weight:900;border-radius:999px;padding:6px 10px;background:#e2e8f0;color:#475569}.relay-status.on{background:#dcfce7;color:#15803d}.relay-status.off{background:#fee2e2;color:#be123c}.manual-controls{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px}.rules-table,.rules-table tbody{display:block;width:100%}.rule-row{display:grid;grid-template-columns:1fr 86px 1fr 44px;gap:8px;align-items:end;padding:12px 0;border-top:1px solid #eef2f7}.rule-row td{display:block;padding:0}.rule-row select,.rule-row input{width:100%;height:42px;border:1px solid #cbd5e1;border-radius:10px;background:#fff;padding:0 10px;font-size:15px}.offset-wrap{display:flex;align-items:center;gap:6px}.offset-wrap input{min-width:0}.unit{font-size:12px;color:#64748b;font-weight:800}.del-btn{width:42px;height:42px;border:0;border-radius:12px;background:#ef4444;color:#fff;font-size:20px;font-weight:800}.save-card{position:sticky;bottom:10px;z-index:2;padding:12px;background:rgba(255,255,255,.94);backdrop-filter:blur(6px)}@media(min-width:520px){body{padding:22px}h1{font-size:34px}.card{padding:22px}.clock{font-size:38px}.prayer-time{font-size:20px}.rule-row{grid-template-columns:1fr 100px 1fr 48px}}
    </style>
    <script>
    function updateRelayBadge(relayIdx, state) {
        var badge = document.getElementById('relayStatus' + relayIdx);
        if(!badge) return;
        badge.textContent = state == 0 ? 'ON' : 'OFF';
        badge.className = 'relay-status ' + (state == 0 ? 'on' : 'off');
    }
    function setRelay(relayIdx, state) {
        fetch('/relay?relay=' + relayIdx + '&state=' + state)
            .then(function(resp) {
                if(!resp.ok) throw new Error('relay');
                updateRelayBadge(relayIdx, state);
            })
            .catch(function() { alert('Relay command failed.'); });
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
    String clockHtml = "<div class='card top-card'>";
    clockHtml += "<div id='todayDate' class='date'>Today: " + formatDate(now) + "</div>";
    clockHtml += "<div id='liveClock' class='clock'>--:--:--</div>";
    clockHtml += "<script>initLiveClock(" + String(secondsPastMidnight) + ");</script>";
    clockHtml += "<p class='note'>UTC+2 - Standard Winter Time</p>";
    clockHtml += "<div class='status-line'>Calendar <span class='" + String(schedule.loaded ? "status-ok'>Synced" : "status-bad'>Missing file") + "</span></div>";
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
        bool relayOn = relayState == 0;
        String card = "<div class='card'>";
        card += "<div class='relay-head'><h2>Relay " + String(r + 1) + "</h2>";
        card += "<span id='relayStatus" + String(r) + "' class='relay-status " + String(relayOn ? "on'>ON" : "off'>OFF") + "</span></div>";
        card += "<div class='manual-controls'>";
        card += "<button type='button' class='btn btn-green' onclick='setRelay(" + String(r) + ",0)'>Manual ON</button>";
        card += "<button type='button' class='btn btn-red' onclick='setRelay(" + String(r) + ",1)'>Manual OFF</button>";
        card += "</div>";
        card += "<table class='rules-table' id='ruleTable" + String(r) + "'></table>";
        card += "<button type='button' class='btn btn-secondary' onclick='addRuleRow(" + String(r) + ",0,0,0)'>Add Rule</button>";
        card += "</div>";
        webServer->sendContent(card);
    }

    webServer->sendContent("<div class='card save-card'><input type='submit' name='save_rules' class='btn' value='Save Automation Rules'></div></form>");
}

void sendSavedRulesScript()
{
    webServer->sendContent("<script>window.onload = function() {syncClockFromBrowser();");
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

    DateTime now = clockRtc->now();

    webServer->sendHeader("Cache-Control", "no-cache");
    webServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
    webServer->send(200, "text/html", "");

    sendPageHeader();
    sendClockCard(now);
    sendUploadForm();
    sendRelayCards();
    sendSavedRulesScript();
}

void handleRelayControl()
{
    if (!webServer->hasArg("relay") || !webServer->hasArg("state"))
    {
        webServer->send(400, "text/plain", "Missing relay or state");
        return;
    }

    int relayIndex = webServer->arg("relay").toInt();
    int targetState = webServer->arg("state").toInt() == 0 ? 0 : 1;

    if (relayIndex < 0 || relayIndex >= RELAY_COUNT)
    {
        webServer->send(400, "text/plain", "Invalid relay");
        return;
    }

    setRelayState(relayIndex, targetState);
    webServer->send(200, "text/plain", targetState == 0 ? "ON" : "OFF");
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

    if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
    {
        webServer->send(400, "text/plain", "Invalid time fields");
        return;
    }

    clockRtc->adjust(DateTime(year, month, day, hour, minute, second));
    loadTodaySchedule(month, day);
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
            DateTime now = clockRtc->now();
            loadTodaySchedule(now.month(), now.day());
        }
    }
}

void handleUploadSuccess()
{
    webServer->sendHeader("Location", "/");
    webServer->send(303, "text/html", "<html><body>Redirecting...</body></html>");
}
}

void configureWebServer(WebServer &server, RTC_DS3231 &rtc)
{
    webServer = &server;
    clockRtc = &rtc;

    server.on("/", HTTP_GET, handleRoot);
    server.on("/", HTTP_POST, handleRoot);
    server.on("/relay", HTTP_GET, handleRelayControl);
    server.on("/time-sync", HTTP_GET, handleTimeSync);
    server.on("/upload", HTTP_POST, handleUploadSuccess, handleFileUpload);
}