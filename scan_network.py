#!/usr/bin/env python3
"""
ESP32 WoL Network Scanner
Scans the local network to discover devices and their MAC addresses.
Run from laptop: python3 scan_network.py
"""

import subprocess
import re
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

SUBNET = "192.168.29"
THREADS = 50


def get_mac_arp(ip):
    """Get MAC address for a single IP using arp command."""
    try:
        result = subprocess.run(
            ["arp", "-n", f"{SUBNET}.{ip}", "-a"],
            capture_output=True,
            text=True,
            timeout=2,
        )
        output = result.stdout

        # Parse MAC address from output
        # Format: hostname (192.168.29.1) at aa:bb:cc:dd:ee:ff on enp0s...
        mac_match = re.search(r"([0-9a-fA-F]{2}[:\-]){5}[0-9a-fA-F]{2}", output)
        if mac_match:
            mac = mac_match.group(0).replace("-", ":").upper()
            # Get hostname if available
            host_match = re.search(r"^([^\s]+)", output)
            hostname = host_match.group(1) if host_match else f"PC-{ip}"
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
            timeout=2,
        )
        return True
    except:
        return False


def get_hostname(ip):
    """Get hostname for an IP via reverse DNS."""
    try:
        result = subprocess.run(
            ["nslookup", f"{SUBNET}.{ip}"], capture_output=True, text=True, timeout=2
        )
        match = re.search(r"name\s*=\s*(.+)\.", result.stdout)
        if match:
            return match.group(1).strip()
    except:
        pass
    return None


def scan_network():
    print(f"[*] Scanning {SUBNET}.0/24 network...")
    print("[*] This may take 30-60 seconds...\n")

    # First, ping sweep to populate ARP cache
    print("[*] Ping sweeping (filling ARP cache)...")
    with ThreadPoolExecutor(max_workers=THREADS) as executor:
        futures = {executor.submit(ping_host, i): i for i in range(1, 255)}
        completed = 0
        for future in as_completed(futures):
            completed += 1
            if completed % 50 == 0:
                print(f"    Pinged {completed}/254 hosts...")

    print("[*] Collecting MAC addresses from ARP cache...\n")

    # Now read ARP table
    devices = []
    with ThreadPoolExecutor(max_workers=THREADS) as executor:
        futures = {executor.submit(get_mac_arp, i): i for i in range(1, 255)}
        for future in as_completed(futures):
            result = future.result()
            if result:
                ip, mac, hostname = result
                hostname = get_hostname(ip) or hostname
                devices.append((hostname, mac, f"{SUBNET}.{ip}"))

    # Remove duplicates and sort
    seen = set()
    unique_devices = []
    for device in devices:
        if device[1] not in seen:
            seen.add(device[1])
            unique_devices.append(device)

    unique_devices.sort(key=lambda x: x[2])

    print("=" * 70)
    print(f"Found {len(unique_devices)} devices on network:")
    print("=" * 70)
    print(f"{'Hostname':<25} {'MAC Address':<18} {'IP Address':<15}")
    print("-" * 70)

    for hostname, mac, ip in unique_devices:
        print(f"{hostname:<25} {mac:<18} {ip:<15}")

    print("=" * 70)

    # Save to file for ESP32
    save_to_file(unique_devices)

    return unique_devices


def save_to_file(devices):
    """Save device list to devices.txt in format for ESP32."""
    filename = "devices.txt"
    with open(filename, "w") as f:
        f.write("# ESP32 WoL Device List\n")
        f.write("# Format: NAME,MAC,IP\n")
        f.write("# Edit this file to add/remove devices, then update ESP32 code\n\n")
        for hostname, mac, ip in devices:
            safe_name = hostname.replace(",", "_").replace(" ", "_")
            f.write(f"{safe_name},{mac},{ip}\n")

    print(f"\n[*] Device list saved to {filename}")
    print(f"[*] Copy these entries to your ESP32 code's device list")


def main():
    if len(sys.argv) > 1:
        global SUBNET
        SUBNET = sys.argv[1]
        print(f"[*] Using custom subnet: {SUBNET}.0/24")

    devices = scan_network()

    print("\n[*] Next steps:")
    print("    1. Review the device list above")
    print("    2. Edit devices.txt to rename devices (remove special chars)")
    print("    3. Copy the device entries to your ESP32 main.cpp")
    print("    4. Build and upload: cd esp32_project && pio run -t upload")


if __name__ == "__main__":
    main()
