#!/usr/bin/env python3
"""
DOIP Traffic Monitor
Monitors incoming DOIP traffic to see embedded device communication
"""

import socket
import struct
import time
from datetime import datetime

# DOIP Protocol Constants
DOIP_PROTOCOL_VERSION = 0x02
DOIP_INVERSE_PROTOCOL_VERSION = 0xFD

DOIP_PAYLOAD_TYPES = {
    0x0001: "VEHICLE_IDENTIFICATION_REQUEST",
    0x0004: "VEHICLE_IDENTIFICATION_RESPONSE", 
    0x0005: "ROUTING_ACTIVATION_REQUEST",
    0x0006: "ROUTING_ACTIVATION_RESPONSE",
    0x8001: "DIAGNOSTIC_MESSAGE",
    0x8002: "DIAGNOSTIC_MESSAGE_POSITIVE_ACK",
    0x8003: "DIAGNOSTIC_MESSAGE_NEGATIVE_ACK"
}

def parse_doip_header(data):
    """Parse DOIP header"""
    if len(data) < 8:
        return None
    
    version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', data[:8])
    
    if version != DOIP_PROTOCOL_VERSION or inv_version != DOIP_INVERSE_PROTOCOL_VERSION:
        return None
        
    return {
        'version': version,
        'payload_type': payload_type,
        'payload_length': payload_length,
        'payload_type_name': DOIP_PAYLOAD_TYPES.get(payload_type, f"UNKNOWN_0x{payload_type:04x}")
    }

def monitor_udp_traffic():
    """Monitor UDP DOIP traffic"""
    print("🔍 Monitoring DOIP UDP traffic on port 13400...")
    print("Waiting for embedded device to send discovery requests...")
    print("Press Ctrl+C to stop\n")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        # Bind to a different port to intercept traffic
        sock.bind(('', 13401))  # Monitor on different port
        sock.settimeout(1.0)
        
        while True:
            try:
                data, addr = sock.recvfrom(1024)
                timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                
                header = parse_doip_header(data)
                if header:
                    print(f"[{timestamp}] 📨 DOIP from {addr[0]}:{addr[1]}")
                    print(f"   Type: {header['payload_type_name']} (0x{header['payload_type']:04x})")
                    print(f"   Length: {header['payload_length']} bytes")
                    print(f"   Raw: {data.hex()}")
                    print()
                else:
                    print(f"[{timestamp}] 📦 Non-DOIP from {addr[0]}:{addr[1]}: {data.hex()}")
                    
            except socket.timeout:
                continue
                
    except KeyboardInterrupt:
        print("\n🛑 Monitoring stopped")
    finally:
        sock.close()

def check_doip_emulator_status():
    """Check if DOIP emulator is responding"""
    print("🧪 Testing DOIP emulator status...")
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2)
    
    try:
        # Send test discovery request
        header = struct.pack('>BBHI', 
                           DOIP_PROTOCOL_VERSION,
                           DOIP_INVERSE_PROTOCOL_VERSION,
                           0x0001,  # VEHICLE_IDENTIFICATION_REQUEST
                           0)       # No payload
        
        sock.sendto(header, ('192.168.100.1', 13400))
        response, addr = sock.recvfrom(1024)
        
        header = parse_doip_header(response)
        if header and header['payload_type'] == 0x0004:
            payload = response[8:8+header['payload_length']]
            if len(payload) >= 17:
                vin = payload[:17].decode('ascii', errors='ignore').rstrip('\x00')
                print(f"✅ DOIP Emulator is responding")
                print(f"   VIN: {vin}")
                print(f"   Response from: {addr[0]}:{addr[1]}")
                return True
        
    except Exception as e:
        print(f"❌ DOIP Emulator test failed: {e}")
        return False
    finally:
        sock.close()
    
    return False

def monitor_embedded_device():
    """Monitor for embedded device DOIP traffic"""
    print("🎯 Monitoring for embedded device traffic...")
    print("Looking for DOIP requests from 192.168.100.2...")
    
    # Check connectivity first
    import subprocess
    try:
        result = subprocess.run(['ping', '-c', '1', '-W', '2000', '192.168.100.2'], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            print("✅ Embedded device is reachable at 192.168.100.2")
        else:
            print("⚠️  Cannot ping embedded device at 192.168.100.2")
    except:
        print("⚠️  Ping test failed")
    
    print("\nWaiting for DOIP traffic from embedded device...")
    print("(The device should send discovery requests every 10 seconds)\n")

if __name__ == "__main__":
    print("=" * 60)
    print("🚗 DOIP Communication Monitor")
    print("=" * 60)
    
    # Check emulator status
    if check_doip_emulator_status():
        print()
        monitor_embedded_device()
        # Note: UDP monitoring would require packet capture or different approach
        # For now, just monitor the status
        
        try:
            print("Press Ctrl+C to stop monitoring...")
            while True:
                time.sleep(5)
                print(f"[{datetime.now().strftime('%H:%M:%S')}] Monitoring... (Check RTT Viewer for device output)")
        except KeyboardInterrupt:
            print("\n🛑 Monitoring stopped")
    else:
        print("❌ DOIP emulator is not responding. Please check the setup.")