/**
 * ESP32 Wake-on-LAN Telegram Bot v2.1
 * 
 * Features:
 * - Wake-on-LAN for multiple devices
 * - Telegram bot control
 * - Device status checking
 * - Event logging to LittleFS
 * - Wake count statistics
 * - Daily attendance reporting (10:30 AM)
 * - TrueNAS backup system check (8:30 PM)
 * - Device discovery with confirmation
 * 
 * @version 2.1.0
 * @license MIT
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <LittleFS.h>
#include <time.h>
#include <vector>

#include "config.h"

WiFiUDP udp;
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

unsigned long bootTime = 0;
int totalWakeAttempts = 0;
int totalAttendanceChecks = 0;
int totalTruenasChecks = 0;
int totalDiscoveryScans = 0;
bool attendanceDoneToday = false;
bool truenasCheckDoneToday = false;
int lastAttendanceDay = -1;
int lastTruenasDay = -1;

#define MAX_DISCOVERED 10
DeviceConfig discoveredDevices[MAX_DISCOVERED];
int discoveredCount = 0;
bool awaitingAddConfirmation = false;
String pendingChatId = "";

DeviceConfig devices[MAX_DEVICES] = {
    {"Router", "A8:88:1F:1F:AB:4A", "192.168.29.1", 0},
    {"ESP32", "40:91:51:FC:80:64", "192.168.29.145", 0},
    {"WindowsPC3", "98:FA:9B:F4:54:ED", "192.168.29.167", 0},
    {"ServerPC4", "DC:C2:C9:5A:76:2E", "192.168.29.179", 0},
    {"WindowsPC5", "9C:B6:54:F3:6E:60", "192.168.29.193", 0},
    {"WindowsPC6", "B4:2E:99:EB:23:7D", "192.168.29.197", 0},
    {"LinuxPC7", "A0:B3:CC:F9:5E:45", "192.168.29.20", 0},
    {"WindowsPC8", "D8:9E:F3:04:95:DD", "192.168.29.235", 0},
    {"Device9", "FA:62:B1:4B:25:07", "192.168.29.25", 0},
    {"WindowsPC10", "54:BF:64:6D:DA:D8", "192.168.29.45", 0},
    {"WindowsPC11", "C8:D9:D2:10:2B:4D", "192.168.29.54", 0},
    {"Device12", "70:4D:7B:6A:EF:09", "192.168.29.58", 0},
    {"WindowsPC13", "00:25:AB:98:85:13", "192.168.29.80", 0},
};
int deviceCount = 13;

#define LOG_FILE "/wol_events.log"
#define STATS_FILE "/wol_stats.txt"
#define DEVICES_FILE "/devices.txt"
#define MAX_LOG_SIZE 50000

void initLittleFS() {
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mount failed, formatting...");
        return;
    }
    Serial.println("[FS] LittleFS mounted successfully");
}

void logEvent(String event) {
    File file = LittleFS.open(LOG_FILE, "a");
    if (!file) {
        Serial.println("[LOG] Failed to open log file");
        return;
    }
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S] ", &timeinfo);
        event = String(timestamp) + event;
    }
    
    file.println(event);
    file.close();
    
    file = LittleFS.open(LOG_FILE, "r");
    if (file && file.size() > MAX_LOG_SIZE) {
        file.close();
        File oldFile = LittleFS.open(LOG_FILE, "r");
        File newFile = LittleFS.open("/temp.log", "w");
        if (oldFile && newFile) {
            oldFile.seek(file.size() / 2);
            while (oldFile.available()) {
                newFile.write(oldFile.read());
            }
            oldFile.close();
            newFile.close();
            LittleFS.remove(LOG_FILE);
            LittleFS.rename("/temp.log", LOG_FILE);
        }
    }
    Serial.printf("[LOG] %s\n", event.c_str());
}

void saveAllData() {
    File statsFile = LittleFS.open(STATS_FILE, "w");
    if (statsFile) {
        statsFile.println("# ESP32 WoL Statistics");
        statsFile.printf("totalWakeAttempts=%d\n", totalWakeAttempts);
        statsFile.printf("totalAttendanceChecks=%d\n", totalAttendanceChecks);
        statsFile.printf("totalTruenasChecks=%d\n", totalTruenasChecks);
        statsFile.printf("totalDiscoveryScans=%d\n", totalDiscoveryScans);
        statsFile.printf("bootTime=%lu\n", bootTime);
        statsFile.printf("attendanceDoneToday=%d\n", attendanceDoneToday ? 1 : 0);
        statsFile.printf("truenasCheckDoneToday=%d\n", truenasCheckDoneToday ? 1 : 0);
        
        for (int i = 0; i < deviceCount; i++) {
            statsFile.printf("device.%s.wakeCount=%d\n", devices[i].name, devices[i].wakeCount);
        }
        statsFile.close();
    }
    
    File devFile = LittleFS.open(DEVICES_FILE, "w");
    if (devFile) {
        devFile.printf("# Device Config - Auto-generated\n");
        devFile.printf("# Format: name,MAC,IP\n");
        for (int i = 0; i < deviceCount; i++) {
            devFile.printf("%s,%s,%s\n", devices[i].name, devices[i].mac, devices[i].ip);
        }
        devFile.close();
        Serial.println("[FS] Device list saved");
    }
}

void loadStats() {
    File file = LittleFS.open(STATS_FILE, "r");
    if (!file) {
        Serial.println("[STATS] No saved stats found");
        return;
    }
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("totalWakeAttempts=")) {
            totalWakeAttempts = line.substring(18).toInt();
        }
        else if (line.startsWith("totalAttendanceChecks=")) {
            totalAttendanceChecks = line.substring(22).toInt();
        }
        else if (line.startsWith("totalTruenasChecks=")) {
            totalTruenasChecks = line.substring(19).toInt();
        }
        else if (line.startsWith("totalDiscoveryScans=")) {
            totalDiscoveryScans = line.substring(20).toInt();
        }
        else if (line.startsWith("attendanceDoneToday=")) {
            attendanceDoneToday = line.substring(19).toInt() == 1;
        }
        else if (line.startsWith("truenasCheckDoneToday=")) {
            truenasCheckDoneToday = line.substring(22).toInt() == 1;
        }
        else if (line.startsWith("device.")) {
            int dotPos = line.indexOf('.', 7);
            int eqPos = line.indexOf('=');
            if (dotPos > 0 && eqPos > 0) {
                String devName = line.substring(7, dotPos);
                int count = line.substring(eqPos + 1).toInt();
                
                for (int i = 0; i < deviceCount; i++) {
                    if (String(devices[i].name) == devName) {
                        devices[i].wakeCount = count;
                        break;
                    }
                }
            }
        }
    }
    file.close();
    Serial.println("[STATS] Loaded saved statistics");
}

String getUptimeStr() {
    unsigned long uptime = millis() - bootTime;
    unsigned long seconds = uptime / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    char buf[64];
    snprintf(buf, sizeof(buf), "%lu days, %lu hours, %lu minutes", 
             days, hours % 24, minutes % 60);
    return String(buf);
}

void initTime() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.print("[NTP] Syncing time");
    
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        Serial.print(".");
        delay(500);
        attempts++;
    }
    
    if (getLocalTime(&timeinfo)) {
        Serial.println(" Done!");
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("[NTP] Current time: %s\n", timeStr);
        lastAttendanceDay = timeinfo.tm_mday;
        lastTruenasDay = timeinfo.tm_mday;
    } else {
        Serial.println(" Failed!");
    }
}

String getCurrentTimeStr() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return String(buf);
    }
    return "Time not synced";
}

int getCurrentHour() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) return timeinfo.tm_hour;
    return -1;
}

int getCurrentMinute() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) return timeinfo.tm_min;
    return -1;
}

int getCurrentDay() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) return timeinfo.tm_mday;
    return -1;
}

bool parseMac(const char* macStr, uint8_t* mac) {
    int values[6];
    if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; i++) mac[i] = (uint8_t)values[i];
        return true;
    }
    return false;
}

void sendWakeOnLan(const char* macStr) {
    uint8_t packet[102];
    uint8_t mac[6];
    
    if (!parseMac(macStr, mac)) {
        Serial.println("[WOL] Invalid MAC format");
        return;
    }
    
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(packet + 6 + i * 6, mac, 6);
    }
    
    IPAddress broadcastIp(255, 255, 255, 255);
    udp.beginPacket(broadcastIp, WOL_PORT);
    udp.write(packet, sizeof(packet));
    udp.endPacket();
    
    Serial.printf("[WOL] Magic packet sent to %s\n", macStr);
}

bool pingHost(const char* ipStr) {
    WiFiClient pingClient;
    pingClient.setTimeout(PING_TIMEOUT);
    
    IPAddress ip;
    if (!ip.fromString(ipStr)) return false;
    
    int ports[] = {22, 80, 443, 3389, 445};
    for (int p : ports) {
        if (pingClient.connect(ip, p, PING_TIMEOUT)) {
            pingClient.stop();
            return true;
        }
    }
    return false;
}

int findDevice(const char* name) {
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(devices[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

bool isIpKnown(const char* ip) {
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(devices[i].ip, ip) == 0) {
            return true;
        }
    }
    return false;
}

bool isMacKnown(const char* mac) {
    if (strcmp(mac, "XX:XX:XX:XX:XX:XX") == 0) return false;
    for (int i = 0; i < deviceCount; i++) {
        if (strcmp(devices[i].mac, mac) == 0) {
            return true;
        }
    }
    return false;
}

void scanForNewDevices() {
    discoveredCount = 0;
    memset(discoveredDevices, 0, sizeof(discoveredDevices));
    
    Serial.printf("[Discovery] Starting network scan from %s...\n", WiFi.localIP().toString().c_str());
    
    for (int i = 1; i < 255 && discoveredCount < MAX_DISCOVERED; i++) {
        IPAddress scanIp = WiFi.localIP();
        scanIp[3] = i;
        String ipStr = scanIp.toString();
        
        if (isIpKnown(ipStr.c_str())) {
            continue;
        }
        
        WiFiClient pingClient;
        pingClient.setTimeout(500);
        
        if (pingClient.connect(scanIp, 80, 500)) {
            discoveredDevices[discoveredCount].name = "";
            discoveredDevices[discoveredCount].mac = "XX:XX:XX:XX:XX:XX";
            discoveredDevices[discoveredCount].ip = strdup(ipStr.c_str());
            discoveredDevices[discoveredCount].wakeCount = 0;
            discoveredCount++;
            Serial.printf("[Discovery] Found new device at %s\n", ipStr.c_str());
            pingClient.stop();
        }
        
        delay(3);
    }
    
    Serial.printf("[Discovery] Scan complete. Found %d new devices\n", discoveredCount);
}

String showDiscoveryResults() {
    if (discoveredCount == 0) {
        return "🔍 No new devices found on the network.\nAll devices are already in your list.";
    }
    
    String response = "🔍 DISCOVERED NEW DEVICES:\n\n";
    for (int i = 0; i < discoveredCount; i++) {
        response += String(i + 1) + ". IP: " + String(discoveredDevices[i].ip) + "\n";
        response += "   MAC: Not detectable (will need manual entry for WoL)\n\n";
    }
    response += "❓ Do you want to add these devices?\n";
    response += "Reply 'yes' to add all, 'no' to cancel\n";
    response += "Or use /add <name> <mac> <ip> to add individually";
    
    return response;
}

void addAllDiscoveredDevices() {
    if (deviceCount + discoveredCount > MAX_DEVICES) {
        bot.sendMessage(pendingChatId, "❌ Cannot add all devices - would exceed max of " + String(MAX_DEVICES));
        awaitingAddConfirmation = false;
        return;
    }
    
    int added = 0;
    for (int i = 0; i < discoveredCount; i++) {
        char nameBuf[32];
        snprintf(nameBuf, sizeof(nameBuf), "NewDevice%d", deviceCount + 1);
        
        devices[deviceCount].name = strdup(nameBuf);
        devices[deviceCount].mac = discoveredDevices[i].mac;
        devices[deviceCount].ip = discoveredDevices[i].ip;
        devices[deviceCount].wakeCount = 0;
        
        logEvent("DEVICE_ADDED: " + String(nameBuf) + " (" + String(discoveredDevices[i].ip) + ")");
        added++;
        deviceCount++;
    }
    
    saveAllData();
    
    String msg = "✅ Added " + String(added) + " device(s) to your list!\n";
    msg += "Note: MAC addresses are set to XX:XX:XX:XX:XX:XX\n";
    msg += "Use /setmac <name> <mac> to update MAC for Wake-on-LAN";
    bot.sendMessage(pendingChatId, msg);
    
    awaitingAddConfirmation = false;
    pendingChatId = "";
    discoveredCount = 0;
}

void cancelDiscovery() {
    awaitingAddConfirmation = false;
    pendingChatId = "";
    discoveredCount = 0;
    memset(discoveredDevices, 0, sizeof(discoveredDevices));
    bot.sendMessage(pendingChatId, "❌ Discovery cancelled.");
}

void setDeviceMac(const char* name, const char* mac) {
    int idx = findDevice(name);
    if (idx < 0) {
        bot.sendMessage(pendingChatId, "❌ Device not found: " + String(name));
        return;
    }
    
    uint8_t macBytes[6];
    if (!parseMac(mac, macBytes)) {
        bot.sendMessage(pendingChatId, "❌ Invalid MAC format. Use XX:XX:XX:XX:XX:XX");
        return;
    }
    
    devices[idx].mac = strdup(mac);
    saveAllData();
    logEvent("MAC_SET: " + String(name) + " = " + String(mac));
    bot.sendMessage(pendingChatId, "✅ MAC updated for " + String(name) + " = " + String(mac));
}

String takeAttendance() {
    String report = "📊 DAILY ATTENDANCE REPORT\n";
    report += "📅 " + getCurrentTimeStr() + "\n\n";
    
    int online = 0;
    int offline = 0;
    
    for (int i = 0; i < deviceCount; i++) {
        bool isOnline = pingHost(devices[i].ip);
        
        report += String(i + 1) + ". " + devices[i].name + ": ";
        report += isOnline ? "✅ PRESENT" : "❌ ABSENT";
        report += "\n";
        
        if (isOnline) online++;
        else offline++;
        
        delay(100);
    }
    
    report += "\n📈 Summary: " + String(online) + " online, " + String(offline) + " offline";
    if (deviceCount > 0) {
        report += " (" + String((online * 100) / deviceCount) + "% attendance)";
    }
    
    logEvent("ATTENDANCE: " + String(online) + "/" + String(deviceCount) + " devices online");
    totalAttendanceChecks++;
    saveAllData();
    
    return report;
}

void checkTrueNASBackup() {
    int truenasIdx = findDevice(TRUENAS_DEVICE_NAME);
    if (truenasIdx < 0) {
        Serial.println("[TrueNAS] Device not found in list!");
        return;
    }
    
    Serial.println("[TrueNAS] Starting backup system check...");
    logEvent("TRUENAS_CHECK: Starting backup system check");
    
    bool isOnline = pingHost(devices[truenasIdx].ip);
    String report = "🔔 TRUENAS BACKUP CHECK REPORT\n";
    report += "📅 " + getCurrentTimeStr() + "\n\n";
    report += "Device: " + String(devices[truenasIdx].name) + "\n";
    report += "IP: " + String(devices[truenasIdx].ip) + "\n\n";
    
    if (isOnline) {
        report += "Status: 🟢 ONLINE\n";
        report += "System is ready for backup.\n";
        logEvent("TRUENAS_CHECK: System is ONLINE");
    } else {
        report += "Status: 🔴 OFFLINE\n";
        report += "Powering on system now...\n";
        logEvent("TRUENAS_CHECK: System was OFFLINE, sending WoL");
        
        sendWakeOnLan(devices[truenasIdx].mac);
        devices[truenasIdx].wakeCount++;
        totalWakeAttempts++;
        saveAllData();
        
        delay(2000);
        
        bool wakeAttempt = pingHost(devices[truenasIdx].ip);
        if (wakeAttempt) {
            report += "Wake Status: ✅ SYSTEM STARTED\n";
            logEvent("TRUENAS_CHECK: System started successfully");
        } else {
            report += "Wake Status: ⚠️ WAKE SENT (may take a minute)\n";
            logEvent("TRUENAS_CHECK: Wake packet sent, system may still be booting");
        }
    }
    
    report += "\n📊 Stats:\n";
    report += "Total WoL attempts today: " + String(devices[truenasIdx].wakeCount) + "\n";
    
    totalTruenasChecks++;
    saveAllData();
    
    bot.sendMessage(ALLOWED_ID, report);
    Serial.println("[TrueNAS] Report sent to Telegram");
}

void checkScheduledTasks() {
    int day = getCurrentDay();
    int hour = getCurrentHour();
    int minute = getCurrentMinute();
    
    if (day != lastAttendanceDay) {
        attendanceDoneToday = false;
        truenasCheckDoneToday = false;
        lastAttendanceDay = day;
        lastTruenasDay = day;
    }
    
    if (!attendanceDoneToday && hour == ATTENDANCE_HOUR && 
        minute >= ATTENDANCE_MINUTE && minute <= ATTENDANCE_END_MINUTE) {
        
        Serial.println("[SCHEDULE] Running daily attendance check...");
        String report = takeAttendance();
        bot.sendMessage(ALLOWED_ID, report);
        attendanceDoneToday = true;
        saveAllData();
    }
    
    if (!truenasCheckDoneToday && hour == TRUENAS_CHECK_HOUR && 
        minute >= TRUENAS_CHECK_MINUTE && minute <= TRUENAS_CHECK_END_MINUTE) {
        
        Serial.println("[SCHEDULE] Running TrueNAS backup check...");
        checkTrueNASBackup();
        truenasCheckDoneToday = true;
        saveAllData();
    }
}

void connectWiFi() {
    Serial.print("[WiFi] Connecting to ");
    Serial.println(WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Connected!");
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WiFi] Failed to connect!");
    }
}

String getDeviceListStr() {
    String response = "📋 Configured Devices:\n\n";
    for (int i = 0; i < deviceCount; i++) {
        response += String(i + 1) + ". " + devices[i].name + "\n";
        response += "   MAC: " + String(devices[i].mac) + "\n";
        response += "   IP: " + String(devices[i].ip) + "\n";
        response += "   Wakes: " + String(devices[i].wakeCount) + "\n\n";
    }
    return response;
}

String getAllDevicesStatusStr() {
    String response = "📊 Device Status:\n\n";
    for (int i = 0; i < deviceCount; i++) {
        bool online = pingHost(devices[i].ip);
        response += String(i + 1) + ". " + devices[i].name + ": ";
        response += online ? "🟢 ONLINE" : "🔴 OFFLINE";
        response += "\n";
    }
    return response;
}

String getStatsStr() {
    String response = "📈 ESP32 WoL Statistics\n\n";
    response += "⏱ Uptime: " + getUptimeStr() + "\n";
    response += "🕐 Current Time: " + getCurrentTimeStr() + "\n\n";
    response += "📊 Totals:\n";
    response += "  Total Wake Attempts: " + String(totalWakeAttempts) + "\n";
    response += "  Attendance Checks: " + String(totalAttendanceChecks) + "\n";
    response += "  TrueNAS Checks: " + String(totalTruenasChecks) + "\n";
    response += "  Discovery Scans: " + String(totalDiscoveryScans) + "\n\n";
    response += "💻 Per-Device Wake Count:\n";
    
    for (int i = 0; i < deviceCount; i++) {
        response += "  " + String(devices[i].name) + ": " + String(devices[i].wakeCount) + "\n";
    }
    
    return response;
}

String getRecentLogs(int count) {
    File file = LittleFS.open(LOG_FILE, "r");
    if (!file) {
        return "No logs found.";
    }
    
    std::vector<String> lines;
    while (file.available()) {
        lines.push_back(file.readStringUntil('\n'));
    }
    file.close();
    
    String response = "📝 Recent Events (last " + String(count) + "):\n\n";
    int start = lines.size() > count ? lines.size() - count : 0;
    
    for (int i = start; i < lines.size(); i++) {
        response += lines[i] + "\n";
    }
    
    return response;
}

void handleNewMessages(int numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
        String chat_id = bot.messages[i].chat_id;
        String text = bot.messages[i].text;
        String from_name = bot.messages[i].from_name;
        
        if (chat_id != ALLOWED_ID) {
            bot.sendMessage(chat_id, "⛔ Unauthorized user!");
            Serial.printf("[AUTH] Rejected message from %s\n", chat_id.c_str());
            continue;
        }
        
        Serial.printf("[Telegram] From: %s | Message: %s\n", from_name.c_str(), text.c_str());
        
        if (awaitingAddConfirmation && chat_id == pendingChatId) {
            text.toLowerCase();
            if (text == "yes" || text == "y" || text == "add" || text == "a") {
                addAllDiscoveredDevices();
                continue;
            } else if (text == "no" || text == "n" || text == "cancel" || text == "c") {
                bot.sendMessage(chat_id, "❌ Discovery cancelled.");
                cancelDiscovery();
                continue;
            } else {
                bot.sendMessage(chat_id, "Please reply 'yes' to add devices or 'no' to cancel.");
                continue;
            }
        }
        
        if (text == "/start") {
            String welcome = "🤖 ESP32 Wake-on-LAN Bot v2.1\n\n";
            welcome += "Control your PCs remotely!\n\n";
            welcome += "Commands:\n";
            welcome += "/list - Show all devices\n";
            welcome += "/status - Check all devices\n";
            welcome += "/status <name> - Check specific device\n";
            welcome += "/wake <name> - Wake specific device\n";
            welcome += "/wakeall - Wake all devices\n";
            welcome += "/discoverdevice - Scan for new devices\n";
            welcome += "/attendance - Take attendance now\n";
            welcome += "/uptime - Show ESP32 uptime\n";
            welcome += "/stats - Show wake statistics\n";
            welcome += "/logs - Show recent events\n";
            welcome += "/truenas - Check TrueNAS status\n";
            welcome += "/setmac <name> <mac> - Update device MAC\n";
            welcome += "/help - Show help\n";
            bot.sendMessage(chat_id, welcome);
        }
        else if (text == "/help") {
            String help = "ESP32 WoL Bot Commands:\n\n";
            help += "/list - List all configured PCs\n";
            help += "/status - Check all PCs status\n";
            help += "/status <name> - Check specific PC\n";
            help += "/wake <name> - Send WoL to PC\n";
            help += "/wakeall - Send WoL to ALL PCs\n";
            help += "/discoverdevice - Scan for new devices\n";
            help += "/attendance - Take attendance now\n";
            help += "/uptime - Show ESP32 uptime\n";
            help += "/stats - Show wake statistics\n";
            help += "/logs - Show recent event logs\n";
            help += "/truenas - Check TrueNAS backup system\n";
            help += "/setmac <name> <mac> - Update device MAC\n";
            bot.sendMessage(chat_id, help);
        }
        else if (text == "/list") {
            if (deviceCount == 0) {
                bot.sendMessage(chat_id, "No devices configured.");
            } else {
                bot.sendMessage(chat_id, getDeviceListStr());
            }
        }
        else if (text == "/uptime") {
            String msg = "⏱ ESP32 Uptime\n\n";
            msg += "Uptime: " + getUptimeStr() + "\n";
            msg += "Current Time: " + getCurrentTimeStr() + "\n";
            msg += "Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
            bot.sendMessage(chat_id, msg);
        }
        else if (text == "/stats") {
            bot.sendMessage(chat_id, getStatsStr());
        }
        else if (text == "/attendance") {
            bot.sendMessage(chat_id, "📊 Taking attendance...");
            String report = takeAttendance();
            bot.sendMessage(chat_id, report);
        }
        else if (text == "/logs") {
            bot.sendMessage(chat_id, getRecentLogs(10));
        }
        else if (text == "/truenas") {
            bot.sendMessage(chat_id, "🔔 Checking TrueNAS backup system...");
            checkTrueNASBackup();
        }
        else if (text == "/discoverdevice") {
            if (awaitingAddConfirmation) {
                bot.sendMessage(chat_id, "⚠️ A discovery is already in progress. Reply 'yes' or 'no' to previous question.");
                continue;
            }
            
            bot.sendMessage(chat_id, "🔍 Scanning network for new devices...\nThis may take 10-20 seconds.");
            
            unsigned long scanStart = millis();
            scanForNewDevices();
            unsigned long scanTime = millis() - scanStart;
            
            totalDiscoveryScans++;
            
            Serial.printf("[Discovery] Scan completed in %lu ms, found %d devices\n", scanTime, discoveredCount);
            
            if (discoveredCount == 0) {
                bot.sendMessage(chat_id, "🔍 No new devices found on the network.\nAll devices are already in your list.");
            } else {
                awaitingAddConfirmation = true;
                pendingChatId = chat_id;
                bot.sendMessage(chat_id, showDiscoveryResults());
            }
        }
        else if (text.startsWith("/setmac ")) {
            String args = text.substring(8);
            args.trim();
            
            int firstSpace = args.indexOf(' ');
            int secondSpace = args.indexOf(' ', firstSpace + 1);
            
            if (firstSpace < 0 || secondSpace < 0) {
                bot.sendMessage(chat_id, "Usage: /setmac <name> <mac>\nExample: /setmac NewDevice1 AA:BB:CC:DD:EE:FF");
            } else {
                String devName = args.substring(0, firstSpace);
                String mac = args.substring(firstSpace + 1);
                mac.trim();
                pendingChatId = chat_id;
                setDeviceMac(devName.c_str(), mac.c_str());
            }
        }
        else if (text == "/status") {
            if (deviceCount == 0) {
                bot.sendMessage(chat_id, "No devices configured.");
            } else {
                bot.sendMessage(chat_id, "Checking all devices...");
                delay(100);
                bot.sendMessage(chat_id, getAllDevicesStatusStr());
            }
        }
        else if (text.startsWith("/status ")) {
            String deviceName = text.substring(8);
            deviceName.trim();
            int idx = findDevice(deviceName.c_str());
            if (idx >= 0) {
                String msg = "Checking " + String(devices[idx].name) + "...";
                bot.sendMessage(chat_id, msg);
                delay(100);
                bool online = pingHost(devices[idx].ip);
                String status = devices[idx].name;
                status += ": ";
                status += online ? "🟢 ONLINE" : "🔴 OFFLINE";
                bot.sendMessage(chat_id, status);
            } else {
                bot.sendMessage(chat_id, "Device not found: " + deviceName);
            }
        }
        else if (text == "/wakeall") {
            if (deviceCount == 0) {
                bot.sendMessage(chat_id, "No devices configured.");
            } else {
                bot.sendMessage(chat_id, "Sending WoL to all devices...");
                logEvent("WAKE_ALL: Command received");
                for (int i = 0; i < deviceCount; i++) {
                    sendWakeOnLan(devices[i].mac);
                    devices[i].wakeCount++;
                    totalWakeAttempts++;
                    delay(100);
                }
                saveAllData();
                logEvent("WAKE_ALL: Magic packets sent to " + String(deviceCount) + " devices");
                bot.sendMessage(chat_id, "✅ Magic packets sent to all devices!");
            }
        }
        else if (text.startsWith("/wake ")) {
            String deviceName = text.substring(6);
            deviceName.trim();
            int idx = findDevice(deviceName.c_str());
            if (idx >= 0) {
                String msg = "Sending WoL to " + String(devices[idx].name) + "...";
                bot.sendMessage(chat_id, msg);
                
                sendWakeOnLan(devices[idx].mac);
                devices[idx].wakeCount++;
                totalWakeAttempts++;
                saveAllData();
                
                logEvent("WAKE: " + String(devices[idx].name) + " (" + String(devices[idx].mac) + ")");
                bot.sendMessage(chat_id, "✅ Magic packet sent to " + String(devices[idx].name));
            } else {
                bot.sendMessage(chat_id, "❌ Device not found: " + deviceName);
            }
        }
        else {
            bot.sendMessage(chat_id, "Unknown command. Send /help for available commands.");
        }
    }
}

void setup() {
    Serial.begin(115200);
    bootTime = millis();
    delay(500);
    
    Serial.println("\n===========================================");
    Serial.println("ESP32 Wake-on-LAN Telegram Bot v2.1");
    Serial.println("With Device Discovery & Confirmation");
    Serial.println("===========================================\n");
    
    initLittleFS();
    loadStats();
    connectWiFi();
    initTime();
    
    udp.begin(WOL_PORT);
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    
    Serial.printf("[Bot] Started with %d devices configured.\n", deviceCount);
    Serial.println("[Bot] TrueNAS Check: 8:30 PM daily");
    Serial.println("[Bot] Attendance Check: 10:30 AM daily");
    Serial.println("[Bot] Waiting for commands...\n");
    
    logEvent("BOOT: ESP32 WoL Bot v2.1 started");
}

void loop() {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    
    while (numNewMessages) {
        Serial.println("[Bot] Got response from Telegram");
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    
    checkScheduledTasks();
    
    static unsigned long lastReconnectCheck = 0;
    if (millis() - lastReconnectCheck > 14400000) {
        lastReconnectCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Reconnecting...");
            logEvent("WiFi reconnecting");
            WiFi.disconnect();
            connectWiFi();
            initTime();
        }
    }
    
    delay(100);
}