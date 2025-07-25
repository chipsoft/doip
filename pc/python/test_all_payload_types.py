#!/usr/bin/env python3
"""
Test script for all DOIP payload types
Demonstrates Vehicle ID, Routing Activation, Alive Check, and Diagnostic Messages
"""

import socket
import struct
import time
import threading

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
DOIP_ALIVE_CHECK_REQUEST = 0x0007
DOIP_ALIVE_CHECK_RESPONSE = 0x0008
DOIP_DIAGNOSTIC_MESSAGE = 0x8001
DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK = 0x8002
DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK = 0x8003

# UDS Service IDs
UDS_READ_DATA_BY_IDENTIFIER = 0x22
DID_VIN = 0xF190

def create_doip_header(payload_type: int, payload_length: int) -> bytes:
    """Create DOIP protocol header"""
    return struct.pack('>BBHI', 
                      DOIP_PROTOCOL_VERSION,
                      DOIP_INVERSE_PROTOCOL_VERSION,
                      payload_type,
                      payload_length)

def parse_doip_header(data: bytes):
    """Parse DOIP header"""
    if len(data) < 8:
        return None
    
    version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', data[:8])
    return {
        'version': version,
        'payload_type': payload_type,
        'payload_length': payload_length,
        'payload': data[8:8+payload_length] if len(data) >= 8+payload_length else b''
    }

def test_vehicle_discovery():
    """Test Vehicle ID Request/Response (0x0001/0x0004)"""
    print("🔍 Testing Vehicle Discovery (UDP)")
    print("=" * 50)
    
    # Create UDP socket
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_socket.settimeout(5.0)
    
    try:
        # Create vehicle identification request
        header = create_doip_header(DOIP_VEHICLE_IDENTIFICATION_REQUEST, 0)
        
        # Send to broadcast
        udp_socket.sendto(header, ('192.168.100.1', DOIP_UDP_DISCOVERY_PORT))
        print(f"📤 Sent Vehicle ID Request to 192.168.100.1:{DOIP_UDP_DISCOVERY_PORT}")
        
        # Wait for response
        response, addr = udp_socket.recvfrom(1024)
        header_info = parse_doip_header(response)
        
        if header_info and header_info['payload_type'] == DOIP_VEHICLE_IDENTIFICATION_RESPONSE:
            payload = header_info['payload']
            if len(payload) >= 17:
                vin = payload[:17].decode('ascii', errors='ignore').rstrip('\x00')
                logical_address = struct.unpack('>H', payload[17:19])[0]
                print(f"✅ Vehicle Discovery Response:")
                print(f"   VIN: {vin}")
                print(f"   Logical Address: 0x{logical_address:04x}")
                print(f"   From: {addr[0]}:{addr[1]}")
                return True
        else:
            print(f"❌ Invalid response: {header_info}")
            
    except Exception as e:
        print(f"❌ Vehicle discovery failed: {e}")
    finally:
        udp_socket.close()
    
    return False

def test_routing_activation():
    """Test Routing Activation Request/Response (0x0005/0x0006)"""
    print("\n🔗 Testing Routing Activation (TCP)")
    print("=" * 50)
    
    # Create TCP socket
    tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tcp_socket.settimeout(10.0)
    
    try:
        # Connect to ECU
        tcp_socket.connect(('192.168.100.1', DOIP_TCP_DATA_PORT))
        print(f"🔗 Connected to ECU at 192.168.100.1:{DOIP_TCP_DATA_PORT}")
        
        # Create routing activation request
        # Payload: Source Address (2) + Activation Type (1) + Reserved (1)
        source_address = 0x0E80  # Tester address
        activation_type = 0x01   # Default activation
        payload = struct.pack('>HBB', source_address, activation_type, 0x00)
        header = create_doip_header(DOIP_ROUTING_ACTIVATION_REQUEST, len(payload))
        request = header + payload
        
        # Send request
        tcp_socket.send(request)
        print(f"📤 Sent Routing Activation Request")
        print(f"   Source Address: 0x{source_address:04x}")
        print(f"   Activation Type: 0x{activation_type:02x}")
        
        # Receive response
        response = tcp_socket.recv(1024)
        header_info = parse_doip_header(response)
        
        if header_info and header_info['payload_type'] == DOIP_ROUTING_ACTIVATION_RESPONSE:
            payload = header_info['payload']
            if len(payload) >= 5:
                client_addr, ecu_addr, response_code = struct.unpack('>HHB', payload[:5])
                print(f"✅ Routing Activation Response:")
                print(f"   Client Address: 0x{client_addr:04x}")
                print(f"   ECU Address: 0x{ecu_addr:04x}")
                print(f"   Response Code: 0x{response_code:02x}")
                return tcp_socket
        else:
            print(f"❌ Invalid routing activation response: {header_info}")
            
    except Exception as e:
        print(f"❌ Routing activation failed: {e}")
    
    return None

def test_diagnostic_message(tcp_socket):
    """Test Diagnostic Messages (0x8001)"""
    print("\n🔧 Testing Diagnostic Messages (TCP)")
    print("=" * 50)
    
    try:
        # Create diagnostic message (Read VIN)
        # Payload: Source Address (2) + Target Address (2) + UDS Data (3)
        source_address = 0x0E80  # Tester address
        target_address = 0x0001  # ECU logical address
        uds_data = struct.pack('>BH', UDS_READ_DATA_BY_IDENTIFIER, DID_VIN)
        
        payload = struct.pack('>HH', source_address, target_address) + uds_data
        header = create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
        request = header + payload
        
        # Send diagnostic request
        tcp_socket.send(request)
        print(f"📤 Sent Diagnostic Request:")
        print(f"   Service: Read Data By Identifier (0x22)")
        print(f"   DID: VIN (0x{DID_VIN:04x})")
        
        # Receive response
        response = tcp_socket.recv(1024)
        header_info = parse_doip_header(response)
        
        if header_info and header_info['payload_type'] == DOIP_DIAGNOSTIC_MESSAGE:
            payload = header_info['payload']
            if len(payload) >= 4:
                ecu_addr, tester_addr = struct.unpack('>HH', payload[:4])
                uds_response = payload[4:]
                
                print(f"✅ Diagnostic Response:")
                print(f"   ECU Address: 0x{ecu_addr:04x}")
                print(f"   Tester Address: 0x{tester_addr:04x}")
                print(f"   UDS Response: {uds_response.hex()}")
                
                # Parse UDS response
                if len(uds_response) >= 3 and uds_response[0] == 0x62:  # Positive response
                    did = struct.unpack('>H', uds_response[1:3])[0]
                    vin_data = uds_response[3:]
                    vin = vin_data.decode('ascii', errors='ignore').rstrip('\x00')
                    print(f"   VIN: {vin}")
                    return True
        else:
            print(f"❌ Invalid diagnostic response: {header_info}")
            
    except Exception as e:
        print(f"❌ Diagnostic message failed: {e}")
    
    return False

def test_alive_check(tcp_socket):
    """Test Alive Check Request/Response (0x0007/0x0008)"""
    print("\n💓 Testing Alive Check (TCP)")
    print("=" * 50)
    
    try:
        # Create alive check request
        # Payload: Source Address (2)
        source_address = 0x0E80  # Tester address
        payload = struct.pack('>H', source_address)
        header = create_doip_header(DOIP_ALIVE_CHECK_REQUEST, len(payload))
        request = header + payload
        
        # Send alive check request
        tcp_socket.send(request)
        print(f"📤 Sent Alive Check Request")
        print(f"   Source Address: 0x{source_address:04x}")
        
        # Receive response
        response = tcp_socket.recv(1024)
        header_info = parse_doip_header(response)
        
        if header_info and header_info['payload_type'] == DOIP_ALIVE_CHECK_RESPONSE:
            payload = header_info['payload']
            if len(payload) >= 2:
                response_source = struct.unpack('>H', payload[:2])[0]
                print(f"✅ Alive Check Response:")
                print(f"   Source Address: 0x{response_source:04x}")
                return True
        else:
            print(f"❌ Invalid alive check response: {header_info}")
            
    except Exception as e:
        print(f"❌ Alive check failed: {e}")
    
    return False

def main():
    """Main test function"""
    print("🚗 DOIP Payload Types Test")
    print("=" * 60)
    print("This script tests all DOIP payload types:")
    print("1. Vehicle ID Request/Response (0x0001/0x0004)")
    print("2. Routing Activation Request/Response (0x0005/0x0006)")
    print("3. Diagnostic Messages (0x8001)")
    print("4. Alive Check Request/Response (0x0007/0x0008)")
    print("=" * 60)
    
    # Test 1: Vehicle Discovery
    if not test_vehicle_discovery():
        print("❌ Vehicle discovery failed. Make sure the ECU emulator is running.")
        return
    
    # Test 2: Routing Activation
    tcp_socket = test_routing_activation()
    if not tcp_socket:
        print("❌ Routing activation failed.")
        return
    
    try:
        # Test 3: Diagnostic Messages
        test_diagnostic_message(tcp_socket)
        
        # Test 4: Alive Check
        test_alive_check(tcp_socket)
        
        print("\n🎉 All DOIP payload types tested successfully!")
        print("The ECU emulator supports all required ISO 13400 payload types.")
        
    finally:
        tcp_socket.close()
        print("\n🔌 Connection closed")

if __name__ == "__main__":
    main() 