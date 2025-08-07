#!/usr/bin/env python3
"""
DOIP Large Message Test Client
Tests large message handling capabilities of DOIP ECU emulators
"""

import socket
import struct
import time
import sys
import random
from typing import Optional, Tuple

# DOIP Protocol Constants
DOIP_TCP_DATA_PORT = 13400
DOIP_PROTOCOL_VERSION = 0x02
DOIP_INVERSE_PROTOCOL_VERSION = 0xFD

# DOIP Payload Types
DOIP_ROUTING_ACTIVATION_REQUEST = 0x0005
DOIP_ROUTING_ACTIVATION_RESPONSE = 0x0006
DOIP_DIAGNOSTIC_MESSAGE = 0x8001
DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK = 0x8002
DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK = 0x8003

# UDS Service IDs
UDS_LARGE_MESSAGE_TEST = 0x3E
UDS_POSITIVE_RESPONSE_MASK = 0x40

class DOIPLargeMessageTester:
    def __init__(self, ecu_ip: str = "192.168.100.1", ecu_port: int = DOIP_TCP_DATA_PORT):
        self.ecu_ip = ecu_ip
        self.ecu_port = ecu_port
        self.socket = None
        self.tester_address = 0x0E80
        self.max_message_size = 256 * 1024  # 256KB
    
    def create_doip_header(self, payload_type: int, payload_length: int) -> bytes:
        """Create DOIP protocol header"""
        return struct.pack('>BBHI', 
                          DOIP_PROTOCOL_VERSION,
                          DOIP_INVERSE_PROTOCOL_VERSION,
                          payload_type,
                          payload_length)
    
    def parse_doip_header(self, data: bytes) -> Optional[Tuple[int, int, int]]:
        """Parse DOIP header and return (version, payload_type, payload_length)"""
        if len(data) < 8:
            return None
        
        version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', data[:8])
        
        if version != DOIP_PROTOCOL_VERSION or inv_version != DOIP_INVERSE_PROTOCOL_VERSION:
            print(f"Invalid DOIP version: {version:02x}, {inv_version:02x}")
            return None
            
        return version, payload_type, payload_length
    
    def receive_complete_doip_message(self) -> Optional[bytes]:
        """Receive a complete DOIP message, handling TCP fragmentation"""
        try:
            # First, receive the 8-byte DOIP header
            header_data = bytearray()
            while len(header_data) < 8:
                chunk = self.socket.recv(8 - len(header_data))
                if not chunk:
                    return None
                header_data.extend(chunk)
            
            # Parse the header to get payload length
            header_info = self.parse_doip_header(bytes(header_data))
            if not header_info:
                print("Invalid DOIP header received")
                return None
                
            version, payload_type, payload_length = header_info
            
            # Check message size limit
            total_message_size = 8 + payload_length
            if total_message_size > self.max_message_size:
                print(f"Message too large: {total_message_size} bytes (max: {self.max_message_size})")
                return None
            
            # Receive the payload
            payload_data = bytearray()
            while len(payload_data) < payload_length:
                chunk = self.socket.recv(payload_length - len(payload_data))
                if not chunk:
                    return None
                payload_data.extend(chunk)
            
            complete_message = header_data + payload_data
            print(f"Received complete DOIP message: {len(complete_message)} bytes")
            
            return bytes(complete_message)
            
        except Exception as e:
            print(f"Error receiving DOIP message: {e}")
            return None
    
    def connect_to_ecu(self, target_ecu_address: int = 0x0001) -> bool:
        """Connect to ECU and perform routing activation"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(10.0)
            self.socket.connect((self.ecu_ip, self.ecu_port))
            print(f"Connected to ECU at {self.ecu_ip}:{self.ecu_port}")
            
            # Send routing activation request
            payload = struct.pack('>HB', self.tester_address, 0x00)  # Source address + activation type
            header = self.create_doip_header(DOIP_ROUTING_ACTIVATION_REQUEST, len(payload))
            request = header + payload
            
            self.socket.send(request)
            print("Routing activation request sent")
            
            # Receive routing activation response
            response = self.receive_complete_doip_message()
            if not response:
                print("No routing activation response received")
                return False
            
            header_info = self.parse_doip_header(response)
            if header_info and header_info[1] == DOIP_ROUTING_ACTIVATION_RESPONSE:
                print("Routing activation successful")
                return True
            else:
                print("Routing activation failed")
                return False
                
        except Exception as e:
            print(f"Connection error: {e}")
            return False
    
    def send_large_message_test(self, target_ecu_address: int, test_data_size: int) -> bool:
        """Send large message test to ECU"""
        try:
            # Generate test data with pattern
            test_data = bytearray()
            pattern_size = 256
            pattern = bytes([i % 256 for i in range(pattern_size)])
            
            # Fill test data with repeating pattern
            full_patterns = test_data_size // pattern_size
            remainder = test_data_size % pattern_size
            
            for _ in range(full_patterns):
                test_data.extend(pattern)
            
            if remainder > 0:
                test_data.extend(pattern[:remainder])
            
            # Add a unique signature at the end for verification
            signature = struct.pack('>I', test_data_size)
            test_data[-4:] = signature
            
            print(f"Generated test data: {len(test_data)} bytes with signature: {signature.hex()}")
            
            # Create UDS diagnostic message
            uds_data = struct.pack('B', UDS_LARGE_MESSAGE_TEST) + test_data
            payload = struct.pack('>HH', self.tester_address, target_ecu_address) + uds_data
            header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
            
            message = header + payload
            total_size = len(message)
            
            print(f"Sending large message test: {total_size} bytes total")
            print(f"  - DOIP header: 8 bytes")
            print(f"  - DOIP payload: {len(payload)} bytes")
            print(f"  - UDS data: {len(uds_data)} bytes")
            print(f"  - Test data: {len(test_data)} bytes")
            
            start_time = time.time()
            self.socket.send(message)
            send_time = time.time() - start_time
            
            print(f"Message sent in {send_time:.3f} seconds ({total_size / send_time / 1024:.1f} KB/s)")
            
            # Receive response
            print("Waiting for response...")
            start_time = time.time()
            response = self.receive_complete_doip_message()
            receive_time = time.time() - start_time
            
            if not response:
                print("No response received")
                return False
            
            print(f"Response received in {receive_time:.3f} seconds ({len(response) / receive_time / 1024:.1f} KB/s)")
            
            # Parse response
            header_info = self.parse_doip_header(response)
            if not header_info or header_info[1] != DOIP_DIAGNOSTIC_MESSAGE:
                print("Invalid response type")
                return False
            
            # Extract UDS response
            payload_start = 8
            payload = response[payload_start:]
            
            if len(payload) < 4:  # SA + TA minimum
                print("Response payload too short")
                return False
            
            source_address = struct.unpack('>H', payload[0:2])[0]
            target_address = struct.unpack('>H', payload[2:4])[0]
            uds_response = payload[4:]
            
            print(f"Response: SA=0x{source_address:04x}, TA=0x{target_address:04x}")
            
            if len(uds_response) < 1:
                print("UDS response too short")
                return False
            
            service_response = uds_response[0]
            if service_response == (UDS_LARGE_MESSAGE_TEST + UDS_POSITIVE_RESPONSE_MASK):
                # Parse echo response: Length (4 bytes) + Original Data
                if len(uds_response) >= 5:
                    echoed_length = struct.unpack('>I', uds_response[1:5])[0]
                    echoed_data = uds_response[5:]
                    
                    print(f"Positive response: Echoed length = {echoed_length}, Echoed data = {len(echoed_data)} bytes")
                    
                    # Verify echoed data matches original
                    if echoed_length == len(test_data) and echoed_data == test_data:
                        print("✓ Large message test PASSED - Data echoed correctly")
                        return True
                    else:
                        print("✗ Large message test FAILED - Data mismatch")
                        if echoed_length != len(test_data):
                            print(f"  Length mismatch: expected {len(test_data)}, got {echoed_length}")
                        if echoed_data != test_data:
                            print("  Data content mismatch")
                        return False
                else:
                    print("✗ Large message test FAILED - Invalid response format")
                    return False
            else:
                print(f"✗ Large message test FAILED - Negative response: 0x{service_response:02x}")
                return False
                
        except Exception as e:
            print(f"Large message test error: {e}")
            return False
    
    def run_test_suite(self, target_ecu_address: int = 0x0001):
        """Run comprehensive large message test suite"""
        if not self.connect_to_ecu(target_ecu_address):
            return False
        
        test_sizes = [
            1024,      # 1KB - Basic test
            1400,      # Close to MTU
            1600,      # Just over MTU
            4096,      # 4KB
            8192,      # 8KB
            16384,     # 16KB
            32768,     # 32KB
            65536,     # 64KB
            131072,    # 128KB
            # 262144,    # 256KB - Enable for full test
        ]
        
        print(f"\n=== DOIP Large Message Test Suite ===")
        print(f"Target ECU: 0x{target_ecu_address:04x}")
        print(f"Test sizes: {[f'{size//1024}KB' if size >= 1024 else f'{size}B' for size in test_sizes]}")
        print("=" * 45)
        
        passed = 0
        failed = 0
        
        for i, size in enumerate(test_sizes):
            print(f"\nTest {i+1}/{len(test_sizes)}: {size} bytes ({size//1024}KB)" if size >= 1024 else f"\nTest {i+1}/{len(test_sizes)}: {size} bytes")
            print("-" * 30)
            
            if self.send_large_message_test(target_ecu_address, size):
                passed += 1
                print(f"✓ Test {i+1} PASSED")
            else:
                failed += 1
                print(f"✗ Test {i+1} FAILED")
            
            # Small delay between tests
            time.sleep(0.5)
        
        print("\n" + "=" * 45)
        print(f"Test Results: {passed} passed, {failed} failed")
        print(f"Success rate: {passed/(passed+failed)*100:.1f}%")
        
        return failed == 0
    
    def disconnect(self):
        """Disconnect from ECU"""
        if self.socket:
            self.socket.close()
            self.socket = None
            print("Disconnected from ECU")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 doip_large_message_test.py <ecu_ip> [ecu_address]")
        print("Examples:")
        print("  python3 doip_large_message_test.py 192.168.100.1")
        print("  python3 doip_large_message_test.py 192.168.100.1 0x0001")
        print("  python3 doip_large_message_test.py 192.168.100.1 0x0002  # Multi-ECU")
        sys.exit(1)
    
    ecu_ip = sys.argv[1]
    ecu_address = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x0001
    
    print(f"DOIP Large Message Tester")
    print(f"Target: {ecu_ip}:{DOIP_TCP_DATA_PORT}")
    print(f"ECU Address: 0x{ecu_address:04x}")
    
    tester = DOIPLargeMessageTester(ecu_ip)
    
    try:
        success = tester.run_test_suite(ecu_address)
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\nTest interrupted by user")
    finally:
        tester.disconnect()

if __name__ == "__main__":
    main()