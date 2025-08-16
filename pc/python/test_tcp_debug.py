#!/usr/bin/env python3
"""
Simple TCP Test Script for DOIP Debugging
This script helps test the TCP functionality and see the debugging output
"""

import socket
import struct
import time
import sys

# DOIP Protocol Constants
DOIP_PROTOCOL_VERSION = 0x02
DOIP_INVERSE_PROTOCOL_VERSION = 0xFD
DOIP_ROUTING_ACTIVATION_REQUEST = 0x0005
DOIP_ROUTING_ACTIVATION_RESPONSE = 0x0006
DOIP_DIAGNOSTIC_MESSAGE = 0x8001

# UDS Service IDs
UDS_READ_DATA_BY_IDENTIFIER = 0x22
UDS_POSITIVE_RESPONSE_MASK = 0x40

# Data Identifiers (DIDs)
DID_VIN = 0xF190
DID_ECU_SOFTWARE_VERSION = 0xF1A0

def create_doip_header(payload_type, payload_length):
    """Create DOIP header"""
    return struct.pack('>BBHI', 
                      DOIP_PROTOCOL_VERSION,
                      DOIP_INVERSE_PROTOCOL_VERSION,
                      payload_type,
                      payload_length)

def test_tcp_connection(host='192.168.100.1', port=13400):
    """Test basic TCP connection"""
    print(f"🔍 Testing TCP connection to {host}:{port}")
    
    try:
        # Create socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        
        print("✅ Socket created successfully")
        
        # Connect
        print(f"🔍 Attempting to connect...")
        sock.connect((host, port))
        print("✅ TCP connection established successfully")
        
        # Test routing activation
        print("🔍 Testing routing activation...")
        
        # Routing activation payload: SA(2) + TA(2) + Type(1) + Reserved(2)
        payload = struct.pack('>HHBHH', 0x0E80, 0x1000, 0x00, 0x00, 0x00)
        header = create_doip_header(DOIP_ROUTING_ACTIVATION_REQUEST, len(payload))
        message = header + payload
        
        print(f"📤 Sending routing activation request ({len(message)} bytes)")
        print(f"   Header: {header.hex()}")
        print(f"   Payload: {payload.hex()}")
        
        bytes_sent = sock.send(message)
        print(f"✅ Sent {bytes_sent} bytes")
        
        # Wait for response
        print("🔍 Waiting for routing activation response...")
        response = sock.recv(1024)
        
        if response:
            print(f"📥 Received response ({len(response)} bytes): {response.hex()}")
            
            if len(response) >= 8:
                version, inv_version, resp_type, resp_length = struct.unpack('>BBHI', response[:8])
                print(f"   Protocol version: 0x{version:02X}")
                print(f"   Inverse version: 0x{inv_version:02X}")
                print(f"   Response type: 0x{resp_type:04X}")
                print(f"   Payload length: {resp_length}")
                
                if resp_type == DOIP_ROUTING_ACTIVATION_RESPONSE:
                    print("✅ Routing activation response received")
                    
                    if len(response) >= 17:  # Header(8) + Payload(9)
                        response_code = response[12]  # Response code at offset 12
                        print(f"   Response code: 0x{response_code:02X}")
                        
                        if response_code == 0x10:
                            print("✅ Routing activation successful!")
                            
                            # Test diagnostic message
                            print("🔍 Testing diagnostic message...")
                            
                            # Diagnostic message payload: SA(2) + TA(2) + UDS(3)
                            uds_data = struct.pack('>BH', UDS_READ_DATA_BY_IDENTIFIER, DID_VIN)
                            diag_payload = struct.pack('>HH', 0x0E80, 0x1000) + uds_data
                            diag_header = create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(diag_payload))
                            diag_message = diag_header + diag_payload
                            
                            print(f"📤 Sending diagnostic request ({len(diag_message)} bytes)")
                            print(f"   Header: {diag_header.hex()}")
                            print(f"   Payload: {diag_payload.hex()}")
                            
                            bytes_sent = sock.send(diag_message)
                            print(f"✅ Sent {bytes_sent} bytes")
                            
                            # Wait for diagnostic response
                            print("🔍 Waiting for diagnostic response...")
                            diag_response = sock.recv(1024)
                            
                            if diag_response:
                                print(f"📥 Received diagnostic response ({len(diag_response)} bytes): {diag_response.hex()}")
                                
                                if len(diag_response) >= 8:
                                    d_version, d_inv_version, d_type, d_length = struct.unpack('>BBHI', diag_response[:8])
                                    print(f"   Protocol version: 0x{d_version:02X}")
                                    print(f"   Inverse version: 0x{d_inv_version:02X}")
                                    print(f"   Response type: 0x{d_type:04X}")
                                    print(f"   Payload length: {d_length}")
                                    
                                    if d_type == DOIP_DIAGNOSTIC_MESSAGE:
                                        print("✅ Diagnostic response received")
                                        
                                        if len(diag_response) >= 15:  # Header(8) + SA(2) + TA(2) + UDS(3)
                                            uds_response = diag_response[12:15]
                                            if uds_response[0] == (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK):
                                                print("✅ UDS positive response received")
                                                if len(diag_response) > 15:
                                                    data = diag_response[15:]
                                                    print(f"   Data: {data.hex()}")
                                                    try:
                                                        vin = data.decode('ascii')
                                                        print(f"   VIN: {vin}")
                                                    except:
                                                        print(f"   Data (raw): {data}")
                                            else:
                                                print(f"❌ UDS negative response: 0x{uds_response[0]:02X}")
                                        else:
                                            print(f"❌ Diagnostic response too short: {len(diag_response)} bytes")
                                    else:
                                        print(f"❌ Unexpected response type: 0x{d_type:04X}")
                                else:
                                    print(f"❌ Diagnostic response header too short: {len(diag_response)} bytes")
                            else:
                                print("❌ No diagnostic response received")
                        else:
                            print(f"❌ Routing activation failed with code: 0x{response_code:02X}")
                    else:
                        print(f"❌ Routing activation response too short: {len(response)} bytes")
                else:
                    print(f"❌ Unexpected response type: 0x{resp_type:04X}")
            else:
                print(f"❌ Response header too short: {len(response)} bytes")
        else:
            print("❌ No response received")
        
        # Close connection
        sock.close()
        print("✅ Connection closed")
        
    except socket.timeout:
        print("❌ Connection timeout")
    except ConnectionRefusedError:
        print("❌ Connection refused - check if server is running")
    except socket.gaierror as e:
        print(f"❌ DNS resolution error: {e}")
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()

def main():
    """Main function"""
    print("🚗 DOIP TCP Debug Test Script")
    print("=" * 40)
    
    # Default test parameters
    host = '192.168.100.1'
    port = 13400
    
    # Allow command line override
    if len(sys.argv) > 1:
        host = sys.argv[1]
    if len(sys.argv) > 2:
        port = int(sys.argv[2])
    
    print(f"Target: {host}:{port}")
    print(f"Time: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print()
    
    # Run test
    test_tcp_connection(host, port)
    
    print()
    print("🏁 Test completed")

if __name__ == "__main__":
    main()

