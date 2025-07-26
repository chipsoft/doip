#!/usr/bin/env python3
"""
Test script to verify DoIP payload structure fix
"""

import struct
import socket
import time

# DOIP Protocol Constants
DOIP_UDP_DISCOVERY_PORT = 13400
DOIP_PROTOCOL_VERSION = 0x02
DOIP_INVERSE_PROTOCOL_VERSION = 0xFD
DOIP_VEHICLE_IDENTIFICATION_REQUEST = 0x0001
DOIP_VEHICLE_IDENTIFICATION_RESPONSE = 0x0004

def create_doip_header(payload_type, payload_length):
    """Create DOIP protocol header"""
    return struct.pack('>BBHI', 
                      DOIP_PROTOCOL_VERSION,
                      DOIP_INVERSE_PROTOCOL_VERSION,
                      payload_type,
                      payload_length)

def test_vehicle_identification_response():
    """Test the corrected vehicle identification response payload structure"""
    
    # Test data
    vin = "WBAVN31010AE12345"
    logical_address = 0x0001
    entity_id = b'\x00\x01\x02\x03\x04\x05'
    
    # Create payload according to ISO 13400-2 standard with optional fields
    vin_bytes = vin.encode('ascii')[:17].ljust(17, b'\x00')
    group_id = b'\x00\x01\x00\x00\x00\x00'  # 6-byte GID
    payload = (vin_bytes + 
              struct.pack('>H', logical_address) +
              entity_id +
              group_id +
              b'\x00' +  # Further Action Required
              b'\x00')   # VIN/GID Sync Status
    
    print("=== DoIP Vehicle Identification Response Test ===")
    print(f"VIN: {vin}")
    print(f"Logical Address: 0x{logical_address:04x}")
    print(f"Entity ID: {entity_id.hex()}")
    print(f"Group ID: {group_id.hex()}")
    print(f"Payload length: {len(payload)} bytes")
    print(f"Expected length: 33 bytes (17 + 2 + 6 + 6 + 1 + 1)")
    print(f"Payload hex: {payload.hex()}")
    
    # Create full DoIP message
    header = create_doip_header(DOIP_VEHICLE_IDENTIFICATION_RESPONSE, len(payload))
    message = header + payload
    
    print(f"Full message length: {len(message)} bytes")
    print(f"Header: {header.hex()}")
    print(f"Message hex: {message.hex()}")
    
    # Verify structure
    if len(payload) == 33:
        print("✅ Payload length is correct (33 bytes)")
    else:
        print(f"❌ Payload length is incorrect: {len(payload)} bytes")
    
    if len(message) == 41:  # 8 (header) + 33 (payload)
        print("✅ Total message length is correct (41 bytes)")
    else:
        print(f"❌ Total message length is incorrect: {len(message)} bytes")
    
    return message

def test_udp_discovery():
    """Test UDP discovery with the corrected payload"""
    print("\n=== Testing UDP Discovery ===")
    
    # Create discovery request
    request_payload = b''  # Empty payload for vehicle identification request
    header = create_doip_header(DOIP_VEHICLE_IDENTIFICATION_REQUEST, len(request_payload))
    request = header + request_payload
    
    print(f"Discovery request: {request.hex()}")
    print(f"Request length: {len(request)} bytes")
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5.0)
    
    try:
        # Send broadcast request
        broadcast_addr = ('255.255.255.255', DOIP_UDP_DISCOVERY_PORT)
        sock.sendto(request, broadcast_addr)
        print("Discovery request sent")
        
        # Wait for response
        response, addr = sock.recvfrom(1024)
        print(f"Response received from {addr}: {len(response)} bytes")
        print(f"Response hex: {response.hex()}")
        
        # Parse response
        if len(response) >= 8:
            version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', response[:8])
            print(f"Response header: version=0x{version:02x}, inv_version=0x{inv_version:02x}, type=0x{payload_type:04x}, length={payload_length}")
            
            if payload_type == DOIP_VEHICLE_IDENTIFICATION_RESPONSE:
                print("✅ Correct response type received")
                if payload_length == 33:
                    print("✅ Correct payload length (33 bytes)")
                else:
                    print(f"❌ Incorrect payload length: {payload_length} bytes")
            else:
                print(f"❌ Unexpected response type: 0x{payload_type:04x}")
        
    except socket.timeout:
        print("❌ No response received (timeout)")
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    # Test the corrected payload structure
    test_vehicle_identification_response()
    
    # Test UDP discovery
    test_udp_discovery()
    
    print("\n=== Test Complete ===")
    print("If you see ✅ marks, the DoIP payload structure fix is working correctly.")
    print("If you see ❌ marks, there may still be issues to resolve.") 