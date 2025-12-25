/**
 * truenas.h — TrueNAS REST API Client + Scheduled Wake System
 *
 * Provides:
 *   - shutdownTrueNAS()       POST /api/v2.0/system/shutdown
 *   - rebootTrueNAS()         POST /api/v2.0/system/reboot
 *   - getTrueNASStatus()      GET  /api/v2.0/system/info
 *   - getPoolStatus()         GET  /api/v2.0/pool
 *   - addScheduledWake()      Store a timed WoL event in LittleFS
 *   - removeScheduledWake()   Delete a schedule
 *   - listSchedules()         Human-readable schedule list
 *   - checkScheduledWakes()   Called every loop() tick — fires WoL at due time
 *   - loadSchedules()         Read persisted schedules from LittleFS on boot
 *   - saveSchedules()         Write schedules to LittleFS
 *
 * Include ONCE in main.cpp after config.h:
 *   #include "truenas.h"
 *
 * Requires in config.h:
 *   TRUENAS_IP, TRUENAS_API_KEY (or TRUENAS_USERNAME + TRUENAS_PASSWORD),
 *   TRUENAS_USE_HTTPS, TRUENAS_DEVICE_NAME
 *
 * @version 1.0.0
 * @license MIT
 */

#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>

// ─────────────────────────────────────────────────────────────────
// Externals — defined in main.cpp, used here
// ─────────────────────────────────────────────────────────────────
extern void logEvent(String event);
extern void sendWakeOnLan(const char* macStr);
extern bool pingHost(const char* ipStr);
extern int  findDevice(const char* name);
extern int  getCurrentDay();
extern int  getCurrentHour();
extern int  getCurrentMinute();
extern String getCurrentTimeStr();

// Forward-declare the device list types so this header compiles standalone
extern struct DeviceConfig devices[MAX_DEVICES];

// ─────────────────────────────────────────────────────────────────
// Scheduled Wake Storage
// ─────────────────────────────────────────────────────────────────
#define MAX_SCHEDULES    10
#define SCHEDULES_FILE   "/schedules.txt"

struct ScheduledWake {
    char deviceName[32];  // Must match a name in devices[]
    int  hour;            // 0–23
    int  minute;          // 0–59
    bool active;
    int  lastTriggeredDay; // Prevents double-fire within same calendar day
};

static ScheduledWake _schedules[MAX_SCHEDULES];
static int           _scheduleCount = 0;

// ─────────────────────────────────────────────────────────────────
// Internal: TrueNAS HTTPS/HTTP call
// ─────────────────────────────────────────────────────────────────
static String _truenasCall(const char* endpoint,
                            const char* method,
                            const char* body = nullptr) {
    String result = "";

    WiFiClientSecure secClient;
    HTTPClient       http;

    // TrueNAS uses a self-signed certificate — skip verification for LAN use
    secClient.setInsecure();
    secClient.setTimeout(12000);

    String scheme = TRUENAS_USE_HTTPS ? "https" : "http";
    String url    = scheme + "://" + TRUENAS_IP + "/api/v2.0" + endpoint;

    if (!http.begin(secClient, url)) {
        Serial.println("[TrueNAS] HTTPClient.begin() failed");
        return "ERROR: Could not connect to TrueNAS at " + String(TRUENAS_IP);
    }

    http.setTimeout(12000);

    // Auth — API key takes priority over username/password
#ifdef TRUENAS_API_KEY
    http.addHeader("Authorization", "Bearer " + String(TRUENAS_API_KEY));
#else
    // Basic auth: base64(username:password) via mbedtls
    String creds = String(TRUENAS_USERNAME) + ":" + String(TRUENAS_PASSWORD);
    unsigned char b64buf[128];
    size_t        b64len = 0;
    mbedtls_base64_encode(b64buf, sizeof(b64buf), &b64len,
                          (const unsigned char*)creds.c_str(), creds.length());
    b64buf[b64len] = '\0';
    http.addHeader("Authorization", "Basic " + String((char*)b64buf));
#endif

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept",       "application/json");

    int code = -1;
    if (strcmp(method, "POST") == 0) {
        code = http.POST(body ? body : "{}");
    } else {
        code = http.GET();
    }

    if (code > 0) {
        result = http.getString();
        Serial.printf("[TrueNAS] %s %s → HTTP %d\n", method, endpoint, code);
    } else {
        result = "ERROR: HTTP " + String(code) + " — " + http.errorToString(code);
        Serial.printf("[TrueNAS] Request failed: %s\n", result.c_str());
    }

    http.end();
    return result;
}

// ─────────────────────────────────────────────────────────────────
// Simple JSON field extractor (avoids full ArduinoJson parse for
// the small fields we care about — keeps heap usage low)
// ─────────────────────────────────────────────────────────────────
static String _jsonField(const String& json, const String& key) {
    String search = "\"" + key + "\":";
    int start = json.indexOf(search);
    if (start < 0) return "";
    start += search.length();
    // Skip whitespace
    while (start < (int)json.length() && json[start] == ' ') start++;

    if (json[start] == '"') {
        // String value
        int end = json.indexOf('"', start + 1);
        return json.substring(start + 1, end);
    } else {
        // Numeric / bool / null value
        int end = start;
        while (end < (int)json.length() &&
               json[end] != ',' && json[end] != '}' && json[end] != ']') end++;
        return json.substring(start, end);
    }
}

// ─────────────────────────────────────────────────────────────────
// Public API: Shutdown TrueNAS
// ─────────────────────────────────────────────────────────────────
String shutdownTrueNAS() {
    logEvent("TRUENAS_API: Shutdown command issued via Telegram");
    String resp = _truenasCall("/system/shutdown", "POST", "{\"delay\": 0}");

    if (resp.startsWith("ERROR")) {
        return "❌ Shutdown failed:\n" + resp;
    }
    return "✅ Shutdown command sent to TrueNAS.\n"
           "The system will power off in a few seconds.\n"
           "Use /wake " + String(TRUENAS_DEVICE_NAME) + " to power it back on.";
}

// ─────────────────────────────────────────────────────────────────
// Public API: Reboot TrueNAS
// ─────────────────────────────────────────────────────────────────
String rebootTrueNAS() {
    logEvent("TRUENAS_API: Reboot command issued via Telegram");
    String resp = _truenasCall("/system/reboot", "POST", "{\"delay\": 0}");

    if (resp.startsWith("ERROR")) {
        return "❌ Reboot failed:\n" + resp;
    }
    return "🔄 Reboot command sent to TrueNAS.\n"
           "The system will restart in ~30-60 seconds.\n"
           "Use /status " + String(TRUENAS_DEVICE_NAME) + " to check when it's back online.";
}

// ─────────────────────────────────────────────────────────────────
// Public API: TrueNAS System Info
// ─────────────────────────────────────────────────────────────────
String getTrueNASStatus() {
    String resp = _truenasCall("/system/info", "GET");

    if (resp.startsWith("ERROR")) {
        return "❌ Could not reach TrueNAS API:\n" + resp;
    }

    String hostname  = _jsonField(resp, "hostname");
    String version   = _jsonField(resp, "version");
    String uptime    = _jsonField(resp, "uptime_seconds");
    String cores     = _jsonField(resp, "cores");
    String physmem   = _jsonField(resp, "physmem");

    // Convert uptime_seconds to human-readable
    String uptimeStr = "";
    long   sec       = uptime.toInt();
    if (sec > 0) {
        long d = sec / 86400; sec %= 86400;
        long h = sec / 3600;  sec %= 3600;
        long m = sec / 60;
        uptimeStr = String(d) + "d " + String(h) + "h " + String(m) + "m";
    } else {
        uptimeStr = "N/A";
    }

    // Convert physmem bytes to GB
    String memStr = "";
    long long mem = physmem.toInt();
    if (mem > 0) {
        memStr = String((float)mem / 1073741824.0f, 1) + " GB";
    }

    String msg = "🖥 TrueNAS System Info\n\n";
    msg += "🏷  Hostname : " + (hostname.length() ? hostname : "N/A") + "\n";
    msg += "📦 Version  : " + (version.length()  ? version  : "N/A") + "\n";
    msg += "⏱  Uptime   : " + uptimeStr + "\n";
    msg += "🔢 CPU Cores: " + (cores.length()    ? cores    : "N/A") + "\n";
    msg += "💾 RAM      : " + (memStr.length()   ? memStr   : "N/A") + "\n";
    msg += "🕐 Queried  : " + getCurrentTimeStr();

    logEvent("TRUENAS_API: System info retrieved");
    return msg;
}

// ─────────────────────────────────────────────────────────────────
// Public API: Pool Status
// ─────────────────────────────────────────────────────────────────
String getPoolStatus() {
    String resp = _truenasCall("/pool", "GET");

    if (resp.startsWith("ERROR")) {
        return "❌ Could not reach TrueNAS API:\n" + resp;
    }

    // Parse pool array — find all "name" and "status" pairs
    String msg       = "💿 TrueNAS Pool Status\n\n";
    int    poolCount = 0;
    int    pos       = 0;

    while (true) {
        int nameIdx = resp.indexOf("\"name\":", pos);
        if (nameIdx < 0) break;

        String poolName   = _jsonField(resp.substring(nameIdx), "name");
        int    statusIdx  = resp.indexOf("\"status\":", nameIdx);
        int    healthIdx  = resp.indexOf("\"healthy\":", nameIdx);
        int    sizeIdx    = resp.indexOf("\"size\":", nameIdx);
        int    freeIdx    = resp.indexOf("\"free\":", nameIdx);

        String poolStatus  = statusIdx  >= 0 ? _jsonField(resp.substring(statusIdx),  "status")  : "UNKNOWN";
        String poolHealthy = healthIdx  >= 0 ? _jsonField(resp.substring(healthIdx),  "healthy") : "?";
        String poolSize    = sizeIdx    >= 0 ? _jsonField(resp.substring(sizeIdx),    "size")    : "0";
        String poolFree    = freeIdx    >= 0 ? _jsonField(resp.substring(freeIdx),    "free")    : "0";

        String emoji = (poolStatus == "ONLINE" && poolHealthy == "true") ? "🟢" :
                       (poolStatus == "ONLINE") ? "🟡" : "🔴";

        // Convert bytes to TB/GB
        auto bytesHuman = [](String bytesStr) -> String {
            long long b = bytesStr.toInt();
            if (b >= (long long)1e12) return String((float)b / 1e12, 1) + " TB";
            if (b >= (long long)1e9)  return String((float)b / 1e9,  1) + " GB";
            if (b >= (long long)1e6)  return String((float)b / 1e6,  1) + " MB";
            return bytesStr + " B";
        };

        msg += emoji + " " + poolName + "\n";
        msg += "   Status : " + poolStatus + "\n";
        msg += "   Healthy: " + poolHealthy + "\n";
        msg += "   Size   : " + bytesHuman(poolSize) + "\n";
        msg += "   Free   : " + bytesHuman(poolFree) + "\n\n";

        poolCount++;
        pos = nameIdx + 1;

        // Guard — stop after 8 pools to avoid message size limits
        if (poolCount >= 8) break;
    }

    if (poolCount == 0) {
        msg += "No pools found or could not parse response.";
    }

    logEvent("TRUENAS_API: Pool status retrieved — " + String(poolCount) + " pool(s)");
    return msg;
}

// ─────────────────────────────────────────────────────────────────
// Scheduled Wake — Persistence (LittleFS)
// ─────────────────────────────────────────────────────────────────
void saveSchedules() {
    File f = LittleFS.open(SCHEDULES_FILE, "w");
    if (!f) {
        Serial.println("[SCHED] Failed to open schedules file for writing");
        return;
    }
    f.println("# ESP32 WoL Scheduled Wakes — auto-generated");
    f.println("# Format: deviceName,HH,MM");
    for (int i = 0; i < _scheduleCount; i++) {
        if (_schedules[i].active) {
            f.printf("%s,%d,%d\n",
                     _schedules[i].deviceName,
                     _schedules[i].hour,
                     _schedules[i].minute);
        }
    }
    f.close();
    Serial.printf("[SCHED] Saved %d schedule(s)\n", _scheduleCount);
}

void loadSchedules() {
    _scheduleCount = 0;
    memset(_schedules, 0, sizeof(_schedules));

    File f = LittleFS.open(SCHEDULES_FILE, "r");
    if (!f) {
        Serial.println("[SCHED] No saved schedules found");
        return;
    }

    while (f.available() && _scheduleCount < MAX_SCHEDULES) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("#") || line.length() == 0) continue;

        int c1 = line.indexOf(',');
        int c2 = line.indexOf(',', c1 + 1);
        if (c1 < 0 || c2 < 0) continue;

        String devName = line.substring(0, c1);
        int    hh      = line.substring(c1 + 1, c2).toInt();
        int    mm      = line.substring(c2 + 1).toInt();

        if (devName.length() == 0 || devName.length() > 31) continue;
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59) continue;

        strncpy(_schedules[_scheduleCount].deviceName,
                devName.c_str(), sizeof(_schedules[_scheduleCount].deviceName) - 1);
        _schedules[_scheduleCount].hour             = hh;
        _schedules[_scheduleCount].minute           = mm;
        _schedules[_scheduleCount].active           = true;
        _schedules[_scheduleCount].lastTriggeredDay = -1;
        _scheduleCount++;
    }
    f.close();
    Serial.printf("[SCHED] Loaded %d schedule(s)\n", _scheduleCount);
}

// ─────────────────────────────────────────────────────────────────
// Scheduled Wake — Add
// Returns: human-readable confirmation string for Telegram
// ─────────────────────────────────────────────────────────────────
String addScheduledWake(const char* deviceName, int hh, int mm) {
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        return "❌ Invalid time. Use 24-hour HH:MM (e.g. 09:30, 22:00).";
    }
    if (strlen(deviceName) == 0 || strlen(deviceName) > 31) {
        return "❌ Invalid device name.";
    }

    // Check device exists in firmware list
    if (findDevice(deviceName) < 0) {
        return "❌ Device \"" + String(deviceName) + "\" not found in your device list.\n"
               "Use /list to see configured devices.";
    }

    // Check if a schedule already exists for this device — update it
    for (int i = 0; i < _scheduleCount; i++) {
        if (strcmp(_schedules[i].deviceName, deviceName) == 0) {
            _schedules[i].hour             = hh;
            _schedules[i].minute           = mm;
            _schedules[i].active           = true;
            _schedules[i].lastTriggeredDay = -1;
            saveSchedules();
            char timeBuf[8]; snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hh, mm);
            logEvent("SCHED_UPDATE: " + String(deviceName) + " @ " + String(timeBuf));
            return "✅ Updated schedule for " + String(deviceName) + " → daily WoL at " +
                   String(timeBuf) + "\n\nUse /schedules to see all active schedules.";
        }
    }

    // Add new schedule
    if (_scheduleCount >= MAX_SCHEDULES) {
        return "❌ Maximum " + String(MAX_SCHEDULES) + " schedules reached.\n"
               "Remove one first with /clearschedule <device>.";
    }

    strncpy(_schedules[_scheduleCount].deviceName,
            deviceName, sizeof(_schedules[_scheduleCount].deviceName) - 1);
    _schedules[_scheduleCount].hour             = hh;
    _schedules[_scheduleCount].minute           = mm;
    _schedules[_scheduleCount].active           = true;
    _schedules[_scheduleCount].lastTriggeredDay = -1;
    _scheduleCount++;

    saveSchedules();

    char timeBuf[8]; snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hh, mm);
    logEvent("SCHED_ADD: " + String(deviceName) + " @ " + String(timeBuf));
    return "✅ Scheduled daily WoL: " + String(deviceName) + " at " + String(timeBuf) + "\n\n"
           "The ESP32 will send a magic packet every day at this time.\n"
           "Use /schedules to see all active schedules.";
}

// ─────────────────────────────────────────────────────────────────
// Scheduled Wake — Remove
// ─────────────────────────────────────────────────────────────────
String removeScheduledWake(const char* deviceName) {
    for (int i = 0; i < _scheduleCount; i++) {
        if (strcmp(_schedules[i].deviceName, deviceName) == 0) {
            // Shift array left
            for (int j = i; j < _scheduleCount - 1; j++) {
                _schedules[j] = _schedules[j + 1];
            }
            memset(&_schedules[_scheduleCount - 1], 0, sizeof(ScheduledWake));
            _scheduleCount--;
            saveSchedules();
            logEvent("SCHED_REMOVE: " + String(deviceName));
            return "✅ Removed schedule for " + String(deviceName) + ".";
        }
    }
    return "❌ No schedule found for \"" + String(deviceName) + "\".\n"
           "Use /schedules to see what's active.";
}

// ─────────────────────────────────────────────────────────────────
// Scheduled Wake — List
// ─────────────────────────────────────────────────────────────────
String listSchedules() {
    if (_scheduleCount == 0) {
        return "📅 No scheduled wakes configured.\n\n"
               "Add one with:\n/schedulewake <device> <HH:MM>\n\n"
               "Example: /schedulewake GamingPC 09:00";
    }

    String msg = "📅 Scheduled Daily Wakes (" +
                 String(_scheduleCount) + "/" + String(MAX_SCHEDULES) + "):\n\n";

    for (int i = 0; i < _scheduleCount; i++) {
        char timeBuf[8];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d",
                 _schedules[i].hour, _schedules[i].minute);
        msg += String(i + 1) + ". 🖥 " + String(_schedules[i].deviceName) +
               "  ⏰ " + String(timeBuf) + " daily\n";
    }
    msg += "\nRemove with: /clearschedule <device>";
    return msg;
}

// ─────────────────────────────────────────────────────────────────
// Scheduled Wake — Check (call from loop())
// Fires WoL when current time matches a schedule
// Uses a 2-minute window and per-day flag to avoid double-fire
// ─────────────────────────────────────────────────────────────────
void checkScheduledWakes(UniversalTelegramBot& bot_ref, const char* allowedId) {
    int day    = getCurrentDay();
    int hour   = getCurrentHour();
    int minute = getCurrentMinute();

    if (day < 0 || hour < 0 || minute < 0) return;  // Time not synced yet

    for (int i = 0; i < _scheduleCount; i++) {
        if (!_schedules[i].active) continue;
        if (_schedules[i].lastTriggeredDay == day) continue;  // Already fired today

        // Match within a 2-minute window to survive polling jitter
        if (hour == _schedules[i].hour &&
            minute >= _schedules[i].minute &&
            minute <= _schedules[i].minute + 1) {

            int devIdx = findDevice(_schedules[i].deviceName);
            if (devIdx < 0) {
                Serial.printf("[SCHED] Device \"%s\" not found — skipping\n",
                              _schedules[i].deviceName);
                _schedules[i].lastTriggeredDay = day;  // Mark done to avoid spam
                continue;
            }

            char timeBuf[8];
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d",
                     _schedules[i].hour, _schedules[i].minute);

            Serial.printf("[SCHED] Firing scheduled WoL: %s @ %s\n",
                          _schedules[i].deviceName, timeBuf);

            sendWakeOnLan(devices[devIdx].mac);
            _schedules[i].lastTriggeredDay = day;

            logEvent("SCHED_FIRED: " + String(_schedules[i].deviceName) + " @ " + String(timeBuf));

            String notify = "⏰ Scheduled Wake Fired!\n\n";
            notify += "🖥 Device : " + String(_schedules[i].deviceName) + "\n";
            notify += "🕐 Time   : " + String(timeBuf) + "\n";
            notify += "📅 Date   : " + getCurrentTimeStr() + "\n\n";
            notify += "Magic packet sent. Use /status " +
                      String(_schedules[i].deviceName) + " to confirm it's online.";

            bot_ref.sendMessage(allowedId, notify);
        }
    }
}
