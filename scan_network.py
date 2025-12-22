#!/usr/bin/env python3
"""
ESP32 WoL Network Scanner - Enhanced Version
Scans the local network to discover devices with detailed information.
Run from laptop: python3 scan_network.py
"""

import subprocess
import re
import sys
import socket
from concurrent.futures import ThreadPoolExecutor, as_completed
import requests
import time

SUBNET = "192.168.29"
THREADS = 50
VENDOR_CACHE = {}

def get_vendor_from_mac(mac):
    """Get vendor name from MAC address using IEEE OUI database."""
    if mac in VENDOR_CACHE:
        return VENDOR_CACHE[mac]
    
    # Get first 3 bytes (OUI) - normalize to colon format
    oui = mac.replace("-", ":")[:8].upper()
    
    try:
        # Query IEEE database
        url = f"https://standards-oui.ieee.org/oui/oui.csv"
        response = requests.get(url, timeout=10)
        if response.status_code == 200:
            for line in response.text.split('\n'):
                if oui.replace(":", "-").upper() in line:
                    parts = line.split(',')
                    if len(parts) >= 3:
                        vendor = parts[2].strip().strip('"')
                        VENDOR_CACHE[mac] = vendor
                        return vendor
    except:
        pass
    
    # Fallback to local lookup
    VENDOR_CACHE[mac] = "Unknown"
    return "Unknown"

def get_hostname(ip):
    """Get hostname for an IP via reverse DNS."""
    try:
        name, alias, addrs = socket.gethostbyaddr(f"{SUBNET}.{ip}")
        return name.split('.')[0] if name else None
    except:
        return None

def get_netbios_name(ip):
    """Get NetBIOS name (Windows machines)."""
    try:
        result = subprocess.run(
            ["nmblookup", "-A", f"{SUBNET}.{ip}"],
            capture_output=True,
            text=True,
            timeout=3
        )
        # Look for <00> names which are typically the machine name
        match = re.search(r"<\S+>\s+<00>\s+GROUP\s+=\s+\S+\s+([^\s]+)", result.stdout)
        if match:
            return match.group(1).strip()
    except:
        pass
    return None

def get_device_type_from_ports(ports):
    """Guess device type from open ports."""
    types = []
    if 80 in ports or 443 in ports:
        types.append("Web Server")
    if 22 in ports:
        types.append("SSH")
    if 3389 in ports:
        types.append("RDP/Windows")
    if 445 in ports:
        types.append("SMB/Windows")
    if 53 in ports:
        types.append("DNS")
    if 21 in ports:
        types.append("FTP")
    if 25 in ports or 587 in ports:
        types.append("Mail")
    if 1080 in ports:
        types.append("Proxy")
    return ", ".join(types) if types else "PC/Server"

def scan_ports(ip):
    """Scan common ports to identify device type."""
    ports_found = []
    common_ports = [21, 22, 23, 25, 53, 80, 110, 143, 443, 445, 993, 995, 3389, 8080, 8443]
    
    for port in common_ports:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            result = sock.connect_ex((f"{SUBNET}.{ip}", port))
            sock.close()
            if result == 0:
                ports_found.append(port)
        except:
            pass
    return ports_found

def get_mac_arp(ip):
    """Get MAC address for a single IP using arp command."""
    try:
        result = subprocess.run(
            ["arp", "-n", f"{SUBNET}.{ip}", "-a"],
            capture_output=True,
            text=True,
            timeout=2
        )
        mac_match = re.search(r"([0-9a-fA-F]{2}[:\-]){5}[0-9a-fA-F]{2}", output := result.stdout)
        if mac_match:
            mac = mac_match.group(0).replace("-", ":").upper()
            host_match = re.search(r"^([^\s]+)", output)
            hostname = host_match.group(1) if host_match else None
            return (ip, mac, hostname)
    except Exception as e:
        pass
    return None

def ping_host(ip):
    """Ping a host to ensure it's in the ARP cache."""
    try:
        subprocess.run(
            ["ping", "-c", "1", "-W", "1", f"{SUBNET}.{ip}"],
            capture_output=True,
            timeout=2
        )
        return True
    except:
        return False

def get_device_info(ip):
    """Get comprehensive device information."""
    info = {
        'ip': f"{SUBNET}.{ip}",
        'mac': None,
        'hostname': None,
        'vendor': None,
        'netbios': None,
        'ports': [],
        'device_type': None
    }
    
    # Get MAC from ARP
    result = get_mac_arp(ip)
    if result:
        _, info['mac'], info['hostname'] = result
        info['vendor'] = get_vendor_from_mac(info['mac'])
    
    # Get hostname via DNS
    if not info['hostname']:
        info['hostname'] = get_hostname(ip)
    
    # Get NetBIOS name
    info['netbios'] = get_netbios_name(ip)
    
    # Scan ports
    info['ports'] = scan_ports(ip)
    info['device_type'] = get_device_type_from_ports(info['ports'])
    
    return info

def scan_network():
    print(f"[*] Enhanced Network Scanner")
    print(f"[*] Target: {SUBNET}.0/24")
    print(f"[*] This may take 60-90 seconds...\n")
    
    print("[*] Ping sweeping (filling ARP cache)...")
    with ThreadPoolExecutor(max_workers=THREADS) as executor:
        futures = {executor.submit(ping_host, i): i for i in range(1, 255)}
        completed = 0
        for future in as_completed(futures):
            completed += 1
            if completed % 50 == 0:
                print(f"    Pinged {completed}/254 hosts...")
    
    print("[*] Collecting MAC addresses...\n")
    
    devices = []
    with ThreadPoolExecutor(max_workers=THREADS) as executor:
        futures = {executor.submit(get_device_info, i): i for i in range(1, 255)}
        for future in as_completed(futures):
            result = future.result()
            if result and result['mac']:
                devices.append(result)
    
    # Remove duplicates and sort by IP
    seen = set()
    unique_devices = []
    for device in devices:
        if device['mac'] not in seen:
            seen.add(device['mac'])
            unique_devices.append(device)
    
    unique_devices.sort(key=lambda x: x['ip'])
    
    return unique_devices

def print_results(devices):
    """Print detailed results."""
    print("=" * 100)
    print(f"Found {len(devices)} devices on network:")
    print("=" * 100)
    print(f"{'IP Address':<16} {'Hostname':<25} {'MAC Address':<18} {'Vendor':<20} {'Type'}")
    print("-" * 100)
    
    for d in devices:
        hostname = d['hostname'] or d['netbios'] or "-"
        vendor = d['vendor'][:18] if d['vendor'] else "-"
        device_type = d['device_type'] or "-"
        
        # Truncate long hostnames
        if len(hostname) > 24:
            hostname = hostname[:24] + "..."
        
        print(f"{d['ip']:<16} {hostname:<25} {d['mac']:<18} {vendor:<20} {device_type}")
    
    print("=" * 100)

def save_to_file(devices):
    """Save device list to devices.txt and config format."""
    
    # Save detailed report
    with open("devices_detailed.txt", "w") as f:
        f.write("ESP32 WoL - Discovered Devices (Detailed)\n")
        f.write("=" * 80 + "\n\n")
        for i, d in enumerate(devices, 1):
            f.write(f"Device {i}:\n")
            f.write(f"  IP Address: {d['ip']}\n")
            f.write(f"  MAC Address: {d['mac']}\n")
            f.write(f"  Hostname: {d['hostname'] or '-'}\n")
            f.write(f"  NetBIOS Name: {d['netbios'] or '-'}\n")
            f.write(f"  Vendor: {d['vendor'] or '-'}\n")
            f.write(f"  Device Type: {d['device_type'] or '-'}\n")
            f.write(f"  Open Ports: {d['ports'] if d['ports'] else 'None detected'}\n")
            f.write("\n")
    
    # Save ESP32 config format
    with open("devices_config.txt", "w") as f:
        f.write("// Add these devices to your ESP32 config in main.cpp\n")
        f.write("// Format: {\"NAME\", \"MAC\", \"IP\"},\n\n")
        for i, d in enumerate(devices, 1):
            # Use hostname or NetBIOS or generic name
            name = d['hostname'] or d['netbios'] or f"PC{i}"
            # Clean name for C++ (no spaces, special chars)
            name = re.sub(r'[^a-zA-Z0-9_]', '', name)
            if not name[0].isalpha():
                name = "Device" + name
            name = name[:20]  # Max 20 chars for safety
            
            f.write(f'    {{"{name}", "{d["mac"]}", "{d["ip"]}"}},\n')
    
    print(f"\n[*] Detailed report saved to: devices_detailed.txt")
    print(f"[*] ESP32 config saved to: devices_config.txt")
    print(f"\n[*] Copy entries from devices_config.txt to your main.cpp\n")

def main():
    if len(sys.argv) > 1:
        global SUBNET
        SUBNET = sys.argv[1]
    
    print(f"\n[*] Starting enhanced network scan on {SUBNET}.0/24")
    
    devices = scan_network()
    print_results(devices)
    save_to_file(devices)
    
    print("\n[*] Next steps:")
    print("    1. Review devices in devices_detailed.txt")
    print("    2. Copy device entries from devices_config.txt to main.cpp")
    print("    3. Edit src/config.h with your WiFi and Telegram credentials")
    print("    4. Build and upload: cd esp32_project && pio run -t upload")

if __name__ == "__main__":
    main()
