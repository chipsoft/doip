#!/usr/bin/env python3
"""
DOIP Real-Time Monitoring Dashboard
Provides continuous monitoring of all AUTOSAR DOIP parameters with live updates
"""

import socket
import struct
import time
import sys
import os
import threading
from datetime import datetime
from typing import Dict, List, Tuple, Optional

# DOIP Protocol Constants
DOIP_UDP_DISCOVERY_PORT = 13400
DOIP_TCP_DATA_PORT = 13400
DOIP_PROTOCOL_VERSION = 0x02
DOIP_INVERSE_PROTOCOL_VERSION = 0xFD

# DOIP Payload Types
DOIP_VEHICLE_IDENTIFICATION_REQUEST = 0x0001
DOIP_VEHICLE_IDENTIFICATION_RESPONSE = 0x0004
DOIP_ROUTING_ACTIVATION_REQUEST = 0x0005
DOIP_ROUTING_ACTIVATION_RESPONSE = 0x0006
DOIP_DIAGNOSTIC_MESSAGE = 0x8001

# UDS Service IDs
UDS_READ_DATA_BY_IDENTIFIER = 0x22
UDS_POSITIVE_RESPONSE_MASK = 0x40

# Priority DIDs for dashboard display
DASHBOARD_DIDS = {
    # Critical Runtime Parameters
    0xF1A7: {"name": "Vehicle Speed", "unit": "km/h", "format": "int", "priority": 1},
    0xF1A8: {"name": "Engine RPM", "unit": "RPM", "format": "int", "priority": 1},
    0xF1A9: {"name": "Battery Voltage", "unit": "V", "format": "voltage", "priority": 1},
    0xF1AA: {"name": "Temperature", "unit": "°C", "format": "temp", "priority": 1},
    0xF1AB: {"name": "Fuel Level", "unit": "%", "format": "int", "priority": 1},
    
    # System Status
    0xF1A6: {"name": "Operating Hours", "unit": "hrs", "format": "long", "priority": 2},
    0xF186: {"name": "Diagnostic Session", "unit": "", "format": "hex", "priority": 2},
    0xF1AC: {"name": "Error Status", "unit": "", "format": "hex", "priority": 2},
    
    # Identification
    0xF190: {"name": "VIN", "unit": "", "format": "string", "priority": 3},
    0xF18C: {"name": "Serial Number", "unit": "", "format": "string", "priority": 3},
    0xF1A0: {"name": "SW Version", "unit": "", "format": "string", "priority": 3},
}

class DOIPDashboard:
    """Real-time DOIP monitoring dashboard"""
    
    def __init__(self):
        self.tcp_socket = None
        self.client_address = 0x0E80
        self.ecu_address = 0x0001
        self.connected = False
        self.monitoring = False
        self.data_cache = {}
        self.update_count = 0
        
    def create_doip_header(self, payload_type: int, payload_length: int) -> bytes:
        """Create DOIP protocol header"""
        return struct.pack('>BBHI', 
                          DOIP_PROTOCOL_VERSION,
                          DOIP_INVERSE_PROTOCOL_VERSION,
                          payload_type,
                          payload_length)

    def discover_and_connect(self) -> bool:
        """Discover vehicle and establish connection"""
        # Discover vehicle
        print("🔍 Discovering DOIP vehicles...")
        
        udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_socket.settimeout(3.0)
        
        vehicle_ip = None
        try:
            header = self.create_doip_header(DOIP_VEHICLE_IDENTIFICATION_REQUEST, 0)
            udp_socket.sendto(header, ('<broadcast>', DOIP_UDP_DISCOVERY_PORT))
            
            data, addr = udp_socket.recvfrom(1024)
            if len(data) >= 8:
                version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', data[:8])
                if payload_type == DOIP_VEHICLE_IDENTIFICATION_RESPONSE:
                    vehicle_ip = addr[0]
                    print(f"✅ Vehicle discovered at {vehicle_ip}")
        except:
            pass
        finally:
            udp_socket.close()
        
        if not vehicle_ip:
            print("❌ No vehicle discovered")
            return False
        
        # Connect to vehicle
        print(f"🔗 Connecting to vehicle...")
        try:
            self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_socket.settimeout(5.0)
            self.tcp_socket.connect((vehicle_ip, DOIP_TCP_DATA_PORT))
            
            # Send routing activation request
            payload = struct.pack('>HB', self.client_address, 0x00) + b'\x00\x00\x00\x00'
            header = self.create_doip_header(DOIP_ROUTING_ACTIVATION_REQUEST, len(payload))
            self.tcp_socket.send(header + payload)
            
            # Receive response
            response = self.tcp_socket.recv(1024)
            if len(response) >= 13:
                response_code = response[12]
                if response_code == 0x10:
                    self.connected = True
                    print("✅ Connected successfully")
                    return True
        except Exception as e:
            print(f"❌ Connection failed: {e}")
        
        return False

    def read_did(self, did: int) -> Optional[bytes]:
        """Read a Data Identifier from the ECU"""
        if not self.tcp_socket or not self.connected:
            return None
        
        try:
            # Create UDS diagnostic request
            uds_data = struct.pack('>BH', UDS_READ_DATA_BY_IDENTIFIER, did)
            payload = struct.pack('>HH', self.client_address, self.ecu_address) + uds_data
            header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
            
            # Send request
            self.tcp_socket.send(header + payload)
            
            # Receive response
            response = self.tcp_socket.recv(1024)
            if len(response) >= 8:
                version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', response[:8])
                if payload_type == DOIP_DIAGNOSTIC_MESSAGE and len(response) >= 12:
                    uds_response = response[12:]
                    if len(uds_response) >= 3 and uds_response[0] == (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK):
                        return uds_response[3:]
        except:
            pass
        
        return None

    def format_data(self, did: int, data: bytes) -> str:
        """Format DID data for display"""
        if not data:
            return "N/A"
        
        did_info = DASHBOARD_DIDS.get(did, {})
        format_type = did_info.get("format", "string")
        
        try:
            if format_type == "int" and len(data) >= 2:
                value = struct.unpack('>H', data[:2])[0]
                return str(value)
            elif format_type == "voltage" and len(data) >= 2:
                value = struct.unpack('>H', data[:2])[0]
                return f"{value/1000:.2f}"
            elif format_type == "temp" and len(data) >= 2:
                value = struct.unpack('>h', data[:2])[0]
                return f"{value/10:.1f}"
            elif format_type == "long" and len(data) >= 4:
                value = struct.unpack('>I', data[:4])[0]
                return str(value)
            elif format_type == "hex" and len(data) >= 1:
                return f"0x{data[0]:02X}"
            else:  # string format
                return data.decode('ascii', errors='ignore').strip('\x00')
        except:
            return "Error"

    def update_data(self):
        """Update all dashboard data"""
        for did in DASHBOARD_DIDS.keys():
            data = self.read_did(did)
            if data is not None:
                formatted_value = self.format_data(did, data)
                self.data_cache[did] = {
                    'value': formatted_value,
                    'timestamp': datetime.now(),
                    'raw_data': data
                }
        self.update_count += 1

    def clear_screen(self):
        """Clear terminal screen"""
        os.system('cls' if os.name == 'nt' else 'clear')

    def display_dashboard(self):
        """Display the monitoring dashboard"""
        self.clear_screen()
        
        # Header
        print("=" * 80)
        print("🚗 DOIP REAL-TIME MONITORING DASHBOARD")
        print("=" * 80)
        print(f"📡 Connection: {'🟢 CONNECTED' if self.connected else '🔴 DISCONNECTED'}")
        print(f"🔄 Updates: {self.update_count}")
        print(f"⏰ Last Update: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print("-" * 80)
        
        # Critical Runtime Parameters (Priority 1)
        print("🏃 CRITICAL PARAMETERS")
        critical_dids = [did for did, info in DASHBOARD_DIDS.items() if info['priority'] == 1]
        
        for i, did in enumerate(critical_dids):
            if i % 2 == 0 and i > 0:
                print()  # New line every 2 items
            
            did_info = DASHBOARD_DIDS[did]
            cache_entry = self.data_cache.get(did, {})
            value = cache_entry.get('value', 'N/A')
            unit = did_info['unit']
            
            # Color coding based on parameter type
            if did == 0xF1A9:  # Battery voltage
                try:
                    voltage = float(value)
                    status = "🟢" if voltage > 12.0 else "🟡" if voltage > 11.0 else "🔴"
                except:
                    status = "⚪"
            elif did == 0xF1AB:  # Fuel level
                try:
                    fuel = int(value)
                    status = "🟢" if fuel > 25 else "🟡" if fuel > 10 else "🔴"
                except:
                    status = "⚪"
            else:
                status = "🟢" if value != 'N/A' else "⚪"
            
            print(f"{status} {did_info['name']:15}: {value:>8} {unit:3}", end="    ")
        
        print("\n")
        print("-" * 80)
        
        # System Status (Priority 2)
        print("🔧 SYSTEM STATUS")
        system_dids = [did for did, info in DASHBOARD_DIDS.items() if info['priority'] == 2]
        
        for did in system_dids:
            did_info = DASHBOARD_DIDS[did]
            cache_entry = self.data_cache.get(did, {})
            value = cache_entry.get('value', 'N/A')
            unit = did_info['unit']
            
            status = "🟢" if value != 'N/A' else "⚪"
            if did == 0xF1AC and value != 'N/A':  # Error status
                status = "🟢" if value == "0x00" else "🔴"
            
            print(f"{status} {did_info['name']:20}: {value:>12} {unit}")
        
        print("-" * 80)
        
        # Vehicle Identification (Priority 3)
        print("🆔 VEHICLE IDENTIFICATION")
        id_dids = [did for did, info in DASHBOARD_DIDS.items() if info['priority'] == 3]
        
        for did in id_dids:
            did_info = DASHBOARD_DIDS[did]
            cache_entry = self.data_cache.get(did, {})
            value = cache_entry.get('value', 'N/A')
            
            # Truncate long strings for display
            display_value = value[:30] + "..." if len(str(value)) > 30 else value
            status = "🟢" if value != 'N/A' else "⚪"
            
            print(f"{status} {did_info['name']:15}: {display_value}")
        
        print("=" * 80)
        print("Press Ctrl+C to stop monitoring")

    def monitor_loop(self):
        """Main monitoring loop"""
        if not self.discover_and_connect():
            print("❌ Failed to connect to DOIP vehicle")
            return
        
        self.monitoring = True
        print("🚀 Starting monitoring dashboard...")
        time.sleep(2)
        
        try:
            while self.monitoring:
                # Update data
                self.update_data()
                
                # Display dashboard
                self.display_dashboard()
                
                # Wait before next update
                time.sleep(2)  # Update every 2 seconds
                
        except KeyboardInterrupt:
            print("\n⏹️  Monitoring stopped by user")
        finally:
            self.monitoring = False
            if self.tcp_socket:
                self.tcp_socket.close()

def main():
    print("🚗 DOIP Real-Time Monitoring Dashboard")
    print("=====================================")
    print()
    print("This dashboard provides real-time monitoring of:")
    print("• Vehicle speed, RPM, battery voltage, temperature")
    print("• Fuel level, operating hours, diagnostic status")
    print("• Vehicle identification and system information")
    print()
    print("Make sure the DOIP ECU emulator is running first!")
    print()
    
    input("Press Enter to start monitoring...")
    
    dashboard = DOIPDashboard()
    dashboard.monitor_loop()

if __name__ == "__main__":
    main()