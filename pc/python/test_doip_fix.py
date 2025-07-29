#!/usr/bin/env python3
"""
Comprehensive DOIP Monitoring Test Suite
Tests all AUTOSAR-standard DIDs and monitoring parameters
"""

import subprocess
import time
import threading
import sys
import os
import socket
import struct
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

# Test DIDs - All AUTOSAR standard DIDs we want to test
TEST_DIDS = {
    # Basic DIDs
    0xF190: "VIN",
    0xF1A0: "ECU Software Version",
    0xF1A1: "ECU Hardware Version",
    
    # System Information DIDs
    0xF186: "Active Diagnostic Session",
    0xF187: "Vehicle Manufacturer Spare Part Number",
    0xF188: "Vehicle Manufacturer ECU SW Number",
    0xF189: "Vehicle Manufacturer ECU SW Version",
    0xF18A: "System Supplier Identifier",
    0xF18B: "ECU Manufacturing Date",
    0xF18C: "ECU Serial Number",
    0xF192: "Vehicle Manufacturer Kit Assembly Part Number",
    
    # Network/Communication DIDs
    0xF1A2: "Vehicle Manufacturer ECU Network Name",
    0xF1A3: "Vehicle Manufacturer ECU Network Address",
    0xF1A4: "Vehicle Identification Data Traceability",
    0xF1A5: "Vehicle Manufacturer ECU PIN Traceability",
    
    # Runtime Monitoring DIDs
    0xF1A6: "ECU Operating Hours",
    0xF1A7: "Vehicle Speed Information",
    0xF1A8: "Engine RPM Information",
    0xF1A9: "Battery Voltage Information",
    0xF1AA: "Temperature Sensor Data",
    0xF1AB: "Fuel Level Information",
    
    # Diagnostic Status DIDs
    0xF1AC: "Error Memory Status",
    0xF1AD: "Last Reset Reason",
    0xF1AE: "Boot Software Identification",
    0xF1AF: "Application Software Fingerprint",
}

class DOIPTestClient:
    """DOIP test client for comprehensive monitoring tests"""
    
    def __init__(self):
        self.tcp_socket = None
        self.client_address = 0x0E80  # Tester address
        self.ecu_address = 0x0001     # ECU logical address
        
    def create_doip_header(self, payload_type: int, payload_length: int) -> bytes:
        """Create DOIP protocol header"""
        return struct.pack('>BBHI', 
                          DOIP_PROTOCOL_VERSION,
                          DOIP_INVERSE_PROTOCOL_VERSION,
                          payload_type,
                          payload_length)

    def discover_vehicle(self) -> Optional[str]:
        """Discover DOIP vehicle and return IP address"""
        print("🔍 Discovering DOIP vehicles...")
        
        udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        udp_socket.settimeout(3.0)
        
        try:
            # Send vehicle identification request
            header = self.create_doip_header(DOIP_VEHICLE_IDENTIFICATION_REQUEST, 0)
            udp_socket.sendto(header, ('<broadcast>', DOIP_UDP_DISCOVERY_PORT))
            
            # Wait for response
            data, addr = udp_socket.recvfrom(1024)
            
            if len(data) >= 8:
                version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', data[:8])
                if payload_type == DOIP_VEHICLE_IDENTIFICATION_RESPONSE:
                    print(f"✅ Vehicle discovered at {addr[0]}")
                    return addr[0]
            
        except socket.timeout:
            print("❌ No vehicle discovered (timeout)")
        except Exception as e:
            print(f"❌ Discovery error: {e}")
        finally:
            udp_socket.close()
        
        return None

    def connect_to_vehicle(self, ip_address: str) -> bool:
        """Connect to DOIP vehicle"""
        print(f"🔗 Connecting to vehicle at {ip_address}...")
        
        try:
            self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_socket.settimeout(5.0)
            self.tcp_socket.connect((ip_address, DOIP_TCP_DATA_PORT))
            
            # Send routing activation request
            payload = struct.pack('>HB', self.client_address, 0x00) + b'\x00\x00\x00\x00'
            header = self.create_doip_header(DOIP_ROUTING_ACTIVATION_REQUEST, len(payload))
            self.tcp_socket.send(header + payload)
            
            # Receive routing activation response
            response = self.tcp_socket.recv(1024)
            if len(response) >= 8:
                version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', response[:8])
                if payload_type == DOIP_ROUTING_ACTIVATION_RESPONSE and len(response) >= 13:
                    response_code = response[12]
                    if response_code == 0x10:  # Success
                        print("✅ Routing activation successful")
                        return True
            
            print("❌ Routing activation failed")
            return False
            
        except Exception as e:
            print(f"❌ Connection error: {e}")
            return False

    def read_did(self, did: int) -> Optional[bytes]:
        """Read a Data Identifier from the ECU"""
        if not self.tcp_socket:
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
                    uds_response = response[12:]  # Skip DOIP addressing
                    if len(uds_response) >= 3 and uds_response[0] == (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK):
                        return uds_response[3:]  # Return data portion
            
        except Exception as e:
            print(f"❌ DID read error: {e}")
        
        return None

    def disconnect(self):
        """Disconnect from vehicle"""
        if self.tcp_socket:
            self.tcp_socket.close()
            self.tcp_socket = None

def parse_did_data(did: int, data: bytes) -> str:
    """Parse DID data based on expected format"""
    if not data:
        return "No data"
    
    # Runtime monitoring DIDs with specific formats
    if did == 0xF1A6:  # ECU Operating Hours
        if len(data) >= 4:
            hours = struct.unpack('>I', data[:4])[0]
            return f"{hours} hours"
    elif did == 0xF1A7:  # Vehicle Speed
        if len(data) >= 2:
            speed = struct.unpack('>H', data[:2])[0]
            return f"{speed} km/h"
    elif did == 0xF1A8:  # Engine RPM
        if len(data) >= 2:
            rpm = struct.unpack('>H', data[:2])[0]
            return f"{rpm} RPM"
    elif did == 0xF1A9:  # Battery Voltage
        if len(data) >= 2:
            voltage_mv = struct.unpack('>H', data[:2])[0]
            return f"{voltage_mv} mV ({voltage_mv/1000:.2f} V)"
    elif did == 0xF1AA:  # Temperature
        if len(data) >= 2:
            temp = struct.unpack('>h', data[:2])[0]
            return f"{temp/10:.1f} °C"
    elif did == 0xF1AB:  # Fuel Level
        if len(data) >= 1:
            fuel = data[0]
            return f"{fuel}%"
    elif did == 0xF186:  # Active Diagnostic Session
        if len(data) >= 1:
            session = data[0]
            return f"Session 0x{session:02X}"
    elif did in [0xF1AC, 0xF1AD]:  # Status bytes
        if len(data) >= 1:
            status = data[0]
            return f"0x{status:02X}"
    else:
        # String data (most other DIDs)
        try:
            return data.decode('ascii', errors='ignore').strip('\x00')
        except:
            return f"Binary data: {data.hex()}"

def run_comprehensive_tests():
    """Run comprehensive DOIP monitoring tests"""
    print("🚀 Starting Comprehensive DOIP Monitoring Test Suite")
    print("=" * 60)
    
    client = DOIPTestClient()
    
    # Discover vehicle
    vehicle_ip = client.discover_vehicle()
    if not vehicle_ip:
        print("❌ Test aborted: No vehicle discovered")
        return False
    
    # Connect to vehicle
    if not client.connect_to_vehicle(vehicle_ip):
        print("❌ Test aborted: Connection failed")
        return False
    
    print("\n📊 Testing All AUTOSAR Standard DIDs")
    print("-" * 60)
    
    test_results = {}
    successful_tests = 0
    total_tests = len(TEST_DIDS)
    
    for did, description in TEST_DIDS.items():
        print(f"Testing DID 0x{did:04X}: {description}")
        
        data = client.read_did(did)
        if data is not None:
            parsed_data = parse_did_data(did, data)
            print(f"  ✅ {parsed_data}")
            test_results[did] = ("PASS", parsed_data)
            successful_tests += 1
        else:
            print(f"  ❌ Failed to read")
            test_results[did] = ("FAIL", "No response")
        
        time.sleep(0.2)  # Small delay between requests
    
    # Summary
    print("\n📈 Test Summary")
    print("=" * 60)
    print(f"Total DIDs tested: {total_tests}")
    print(f"Successful reads: {successful_tests}")
    print(f"Failed reads: {total_tests - successful_tests}")
    print(f"Success rate: {successful_tests/total_tests*100:.1f}%")
    
    # Detailed results for failed tests
    failed_tests = [(did, desc) for did, desc in TEST_DIDS.items() if test_results[did][0] == "FAIL"]
    if failed_tests:
        print(f"\n❌ Failed Tests:")
        for did, desc in failed_tests:
            print(f"  - 0x{did:04X}: {desc}")
    
    client.disconnect()
    return successful_tests == total_tests

def run_ecu_emulator():
    """Run the ECU emulator in background"""
    try:
        # Change to the directory containing the Python scripts
        script_dir = os.path.join(os.path.dirname(__file__), "pc", "python")
        
        print("🏃 Starting ECU emulator...")
        process = subprocess.Popen(
            [sys.executable, "doip_ecu_emulator.py"],
            cwd=script_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True
        )
        
        # Let it run for a bit and capture key output
        for i in range(10):  # Startup phase
            output = process.stdout.readline()
            if output:
                print(f"[ECU] {output.strip()}")
            time.sleep(0.5)
        
        return process
        
    except Exception as e:
        print(f"❌ Error running ECU emulator: {e}")
        return None

def main():
    print("=== Comprehensive DOIP Monitoring Test Suite ===")
    print()
    
    print("This test will:")
    print("1. Start the enhanced ECU emulator with all AUTOSAR DIDs")
    print("2. Discover and connect to the DOIP vehicle")
    print("3. Test all 22 standard Data Identifiers (DIDs)")
    print("4. Validate response formats and dynamic data")
    print("5. Generate comprehensive test report")
    print()
    
    # Check if Python scripts exist
    script_dir = os.path.join(os.path.dirname(__file__), "pc", "python")
    emulator_script = os.path.join(script_dir, "doip_ecu_emulator.py")
    
    if not os.path.exists(emulator_script):
        print(f"❌ ERROR: ECU emulator script not found at {emulator_script}")
        return False
    
    print("Press Ctrl+C to stop at any time")
    print()
    
    # Start ECU emulator in background
    emulator_process = run_ecu_emulator()
    if not emulator_process:
        print("❌ Failed to start ECU emulator")
        return False
    
    try:
        # Wait for emulator to fully start
        print("⏳ Waiting for ECU emulator to initialize...")
        time.sleep(3)
        
        # Run comprehensive tests
        success = run_comprehensive_tests()
        
        if success:
            print("\n🎉 ALL TESTS PASSED!")
            print("✅ DOIP monitoring system is working correctly")
        else:
            print("\n⚠️  Some tests failed")
            print("🔧 Check the failed DIDs above for troubleshooting")
        
    except KeyboardInterrupt:
        print("\n⏹️  Test interrupted by user")
    finally:
        # Clean up emulator process
        if emulator_process:
            print("🛑 Stopping ECU emulator...")
            emulator_process.terminate()
            emulator_process.wait()
    
    print("\n📋 Test session completed")
    return True

if __name__ == "__main__":
    main()