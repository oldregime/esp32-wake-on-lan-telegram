/**
 * ESP32 Wake-on-LAN Telegram Bot
 * 
 * A power-efficient WoL system controllable via Telegram
 * 
 * @author Your Name
 * @version 1.0.0
 * @license MIT
 * 
 * Hardware: ESP32 Dev Board
 * Framework: Arduino + PlatformIO
 * 
 * Setup: Copy config.h.example to config.h and add your credentials
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// Load configuration (secrets stored in config.h which is gitignored)
#include "config.h"

// ============== GLOBAL OBJECTS ==============
WiFiUDP udp;
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ============== DEVICE LIST ==============
// Add your devices here after running scan_network.py
// Format: {"DEVICE_NAME", "AA:BB:CC:DD:EE:FF", "192.168.1.xx"}
DeviceConfig devices[MAX_DEVICES] = {
    // Example: {"GamingPC", "AA:BB:CC:DD:EE:FF", "192.168.1.101"},
    // Add your devices below...
};

int deviceCount = 0;  // Set to number of devices you added above

// ============== HELPER FUNCTIONS ==============
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
    
    int ports[] = {80, 443, 22, 3389};
    for (int p : ports) {
        if (pingClient.connect(ip, p, PING_TIMEOUT)) {
            pingClient.stop();
            return true;
        }
    }
    pingClient.stop();
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

String getDeviceListStr() {
    String response = "Configured Devices:\n\n";
    for (int i = 0; i < deviceCount; i++) {
        response += String(i + 1) + ". " + devices[i].name + "\n";
        response += "   MAC: " + String(devices[i].mac) + "\n";
        response += "   IP: " + String(devices[i].ip) + "\n\n";
    }
    return response;
}

String getAllDevicesStatusStr() {
    String response = "Device Status:\n\n";
    for (int i = 0; i < deviceCount; i++) {
        bool online = pingHost(devices[i].ip);
        response += String(i + 1) + ". " + devices[i].name + ": ";
        response += online ? "ONLINE" : "OFFLINE";
        response += "\n";
    }
    return response;
}

// ============== WIFI ==============
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

// ============== TELEGRAM HANDLERS ==============
void handleNewMessages(int numNewMessages) {
    for (int i = 0; i < numNewMessages; i++) {
        String chat_id = bot.messages[i].chat_id;
        String text = bot.messages[i].text;
        String from_name = bot.messages[i].from_name;
        
        // Security: Only allow configured user ID
        if (chat_id != ALLOWED_ID) {
            bot.sendMessage(chat_id, "Unauthorized user!");
            Serial.printf("[AUTH] Rejected message from %s\n", chat_id.c_str());
            continue;
        }
        
        Serial.printf("[Telegram] From: %s | Message: %s\n", from_name.c_str(), text.c_str());
        
        if (text == "/start") {
            String welcome = "ESP32 Wake-on-LAN Bot\n\n";
            welcome += "Control your PCs remotely!\n\n";
            welcome += "Commands:\n";
            welcome += "/list - Show all devices\n";
            welcome += "/status - Check all devices\n";
            welcome += "/status <name> - Check specific device\n";
            welcome += "/wake <name> - Wake specific device\n";
            welcome += "/wakeall - Wake all devices\n";
            welcome += "/help - Show help\n";
            bot.sendMessage(chat_id, welcome);
        }
        else if (text == "/help") {
            String help = "ESP32 WoL Bot Commands:\n\n";
            help += "/list - List all configured PCs\n";
            help += "/status - Ping all PCs\n";
            help += "/status <name> - Check specific PC\n";
            help += "/wake <name> - Send WoL to PC\n";
            help += "/wakeall - Send WoL to ALL PCs\n";
            bot.sendMessage(chat_id, help);
        }
        else if (text == "/list") {
            if (deviceCount == 0) {
                bot.sendMessage(chat_id, "No devices configured.\nEdit config.h to add devices.");
            } else {
                bot.sendMessage(chat_id, getDeviceListStr());
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
                status += online ? "ONLINE" : "OFFLINE";
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
                for (int i = 0; i < deviceCount; i++) {
                    sendWakeOnLan(devices[i].mac);
                    delay(100);
                }
                bot.sendMessage(chat_id, "Magic packets sent to all devices!");
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
                bot.sendMessage(chat_id, "Magic packet sent!");
            } else {
                bot.sendMessage(chat_id, "Device not found: " + deviceName);
            }
        }
        else {
            bot.sendMessage(chat_id, "Unknown command. Send /help for available commands.");
        }
    }
}

// ============== SETUP ==============
void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n===========================================");
    Serial.println("ESP32 Wake-on-LAN Telegram Bot v1.0");
    Serial.println("===========================================\n");
    
    connectWiFi();
    udp.begin(WOL_PORT);
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    
    Serial.printf("[Bot] Started with %d devices configured.\n", deviceCount);
    Serial.println("[Bot] Waiting for commands...\n");
}

// ============== LOOP ==============
void loop() {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    
    while (numNewMessages) {
        Serial.println("[Bot] Got response from Telegram");
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    
    // Periodic WiFi health check (every 4 hours)
    static unsigned long lastReconnectCheck = 0;
    if (millis() - lastReconnectCheck > 14400000) {
        lastReconnectCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Reconnecting...");
            WiFi.disconnect();
            connectWiFi();
        }
    }
    
    delay(100);
}
