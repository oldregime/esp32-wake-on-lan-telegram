# ESP32 Wake-on-LAN Telegram Bot

A powerful, power-efficient Wake-on-LAN system built with ESP32 that allows you to remotely wake any computer on your network via Telegram. Perfect for home automation, remote work setups, or anywhere you need to turn on PCs without physical access.

![ESP32 WoL](https://img.shields.io/badge/ESP32-Wake--on--LAN-FF6B6B?style=for-the-badge&logo=espressif)
![Telegram Bot](https://img.shields.io/badge/Telegram-Bot-26A5E4?style=for-the-badge&logo=telegram)
![Power Efficient](https://img.shields.io/badge/Power-Efficient-4ECDC4?style=for-the-badge&logo=power)

## Features

- **🌐 Remote Wake**: Wake any PC on your network from anywhere via Telegram
- **📱 Telegram Control**: Simple bot commands from your phone or desktop
- **🔒 Secure**: Only your Telegram ID can control the ESP32
- **⚡ Power Efficient**: No continuous polling - ESP32 sleeps until you send a command
- **👥 Multi-Device**: Support for up to 20 PCs with a single ESP32
- **🔍 Status Check**: Check if PCs are online or offline on-demand
- **📊 Auto-Reconnect**: Automatic WiFi reconnection every 4 hours

## Hardware Required

- ESP32 Dev Board (ESP32-DevKit or similar)
- USB Cable for programming
- Computer(s) with Wake-on-LAN enabled in BIOS/UEFI
- Local network (all devices on same subnet)

## Quick Start

### 1. Clone & Install

```bash
git clone https://github.com/YOUR_USERNAME/esp32-wol-telegram-bot.git
cd esp32-wol-telegram-bot
pio pkg install
```

### 2. Configure

Copy the template and add your credentials:

```bash
cp src/config.h.example src/config.h
```

Edit `src/config.h`:
```cpp
#define BOT_TOKEN "your:bot_token_from_botfather"
#define ALLOWED_ID "your_telegram_numeric_id"

const char* WIFI_SSID = "YourWiFiName";
const char* WIFI_PASSWORD = "YourWiFiPassword";
```

### 3. Add Your Devices

Edit the device list in `src/main.cpp`:

```cpp
DeviceConfig devices[MAX_DEVICES] = {
    {"GamingPC", "AA:BB:CC:DD:EE:FF", "192.168.1.101"},
    {"Workstation", "11:22:33:44:55:66", "192.168.1.102"},
    // Add more devices...
};
int deviceCount = 2;
```

### 4. Find MAC Addresses (Optional)

Use the included network scanner from any Linux/macOS machine on your network:

```bash
python3 scan_network.py
```

This will discover all devices and their MAC addresses.

### 5. Upload

```bash
pio run -t upload
pio device monitor
```

## Telegram Commands

| Command | Description |
|---------|-------------|
| `/start` | Show welcome message |
| `/help` | Display all commands |
| `/list` | List all configured devices |
| `/status` | Check online status of all devices |
| `/status <name>` | Check specific device (e.g., `/status GamingPC`) |
| `/wake <name>` | Wake specific device (e.g., `/wake GamingPC`) |
| `/wakeall` | Wake all configured devices |

## How It Works

```
┌─────────────────────────────────────────────────────────────┐
│                      Your Network                           │
│                                                             │
│   ┌──────────┐         ┌──────────┐        ┌──────────┐    │
│   │   PC 1   │         │   PC 2   │        │   PC N   │    │
│   │  Gaming  │         │  Work    │        │  Server  │    │
│   └────┬─────┘         └────┬─────┘        └────┬─────┘    │
│        │ WoL Magic Packet   │                   │          │
│        └───────────────────┼───────────────────┘          │
│                            │                              │
│                     ┌──────┴──────┐                       │
│                     │  Broadcast  │                       │
│                     │   (UDP)    │                       │
│                     └──────┬──────┘                       │
│                            │                              │
└────────────────────────────┼───────────────────────────────┘
                             │
                    ┌────────┴────────┐
                    │     ESP32       │
                    │  (Always on)    │
                    │   ~15-70mA     │
                    └────────┬────────┘
                             │
                    ┌────────┴────────┐
                    │    Telegram     │
                    │   (Your Phone)  │
                    └─────────────────┘
```

### Power Efficiency

The ESP32 is designed to be power-efficient:

- **Idle (waiting for commands)**: ~15-70mA
- **Sending WoL packet**: ~80-120mA for ~1 second
- **Checking status**: ~80-120mA for 2-5 seconds

Unlike polling-based solutions, this ESP32 only activates when you send a Telegram message - no continuous background tasks running.

## Project Structure

```
esp32-wol-telegram-bot/
├── src/
│   ├── main.cpp           # Main firmware code
│   └── config.h.example   # Configuration template
├── lib/                   # Libraries (gitignored)
├── test/                  # Test files
├── scan_network.py        # Network scanner script
├── platformio.ini         # PlatformIO configuration
├── README.md              # This file
└── LICENSE                # MIT License
```

## Enabling Wake-on-LAN

### Windows
1. Open Device Manager
2. Find your Network Adapter
3. Properties → Advanced tab
4. Enable "Wake on Magic Packet" and "Wake on Pattern Match"

### Linux
```bash
sudo ethtool -s eth0 wol g
# To make permanent:
echo 'ETHTOOL_OPTS="wol g"' | sudo tee /etc/sysconfig/network-scripts/ifcfg-eth0
```

### macOS
1. System Preferences → Energy Saver
2. Check "Wake for network access"

## Troubleshooting

### ESP32 won't connect to WiFi
- Verify SSID and password are correct
- Ensure WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
- Move ESP32 closer to router

### WoL not working
- Enable WoL in your PC's BIOS/UEFI settings
- Some PCs require Sleep/Hibernate mode, not full shutdown
- Check that your PC's network driver supports WoL

### Telegram bot not responding
- Verify your Telegram User ID matches `ALLOWED_ID`
- Check the bot token is correct
- Ensure ESP32 is connected to the internet

## Contributing

Contributions are welcome! Feel free to submit issues and pull requests.

## License

MIT License - feel free to use, modify, and distribute.

## Acknowledgments

- [UniversalTelegramBot](https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot) library
- [PlatformIO](https://platformio.org/) for ESP32 development
- ESP32 community for extensive documentation

---

**Made with ❤️ using ESP32 and Telegram Bot API**
