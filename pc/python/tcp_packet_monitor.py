#!/usr/bin/env python3
"""
TCP Packet Monitor for DOIP Testing
This script helps monitor TCP packets in Wireshark and verify the TCP test is working
"""

import socket
import struct
import time
import sys

# DOIP Protocol Constants
DOIP_PROTOCOL_VERSION = 0x02
DOIP_INVERSE_PROTOCOL_VERSION = 0xFD
DOIP_DIAGNOSTIC_MESSAGE = 0x8001

def create_doip_header(payload_type, payload_length):
    """Create DOIP header"""
    return struct.pack('>BBHI', 
                      DOIP_PROTOCOL_VERSION,
                      DOIP_INVERSE_PROTOCOL_VERSION,
                      payload_type,
                      payload_length)

def main():
    print("🔍 TCP Packet Monitor for DOIP Testing")
    print("=" * 50)
    print()
    print("This script will help you monitor TCP packets in Wireshark.")
    print("The embedded device should now be sending TCP packets every 2 seconds.")
    print()
    print("To monitor the packets:")
    print("1. Open Wireshark")
    print("2. Start capturing on your network interface")
    print("3. Apply filter: 'tcp and ip.addr == 192.168.100.2'")
    print("4. Look for TCP SYN packets to 192.168.100.100:13400")
    print()
    print("Expected behavior:")
    print("- TCP SYN packets every 2 seconds")
    print("- Source: 192.168.100.2 (your device)")
    print("- Destination: 192.168.100.100:13400")
    print("- RST packets in response (since no server exists)")
    print()
    
    # Create a simple TCP server to test connectivity
    print("🔍 Creating test TCP server on 192.168.100.100:13400...")
    print("This will help verify if TCP packets are actually reaching the network.")
    print()
    
    try:
        # Create TCP server socket
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.settimeout(1.0)  # 1 second timeout
        
        # Bind to test IP (this will fail if the IP is not configured)
        try:
            server_socket.bind(('192.168.100.100', 13400))
            print("✅ Test server bound to 192.168.100.100:13400")
            print("🔍 Listening for incoming connections...")
            
            server_socket.listen(1)
            
            # Monitor for connections
            start_time = time.time()
            while time.time() - start_time < 60:  # Monitor for 60 seconds
                try:
                    client_socket, client_addr = server_socket.accept()
                    print(f"✅ Connection received from {client_addr[0]}:{client_addr[1]}")
                    
                    # Send a simple response
                    response = create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, 4)
                    response += b'\x0E\x80\x00\x01\x62\xF1\x90\x00'  # Simple response
                    client_socket.send(response)
                    
                    print(f"✅ Response sent: {len(response)} bytes")
                    client_socket.close()
                    
                except socket.timeout:
                    continue
                except Exception as e:
                    print(f"❌ Server error: {e}")
                    break
                    
        except OSError as e:
            print(f"❌ Cannot bind to 192.168.100.100:13400: {e}")
            print("🔍 This is expected if the IP is not configured on this machine.")
            print("🔍 The embedded device will still send TCP packets that you can see in Wireshark.")
        
        server_socket.close()
        
    except Exception as e:
        print(f"❌ Failed to create test server: {e}")
    
    print()
    print("🔍 TCP Packet Monitoring Instructions:")
    print("1. In Wireshark, look for these packet types:")
    print("   - TCP SYN packets (connection attempts)")
    print("   - TCP RST packets (connection refused)")
    print("   - Any TCP packets with source 192.168.100.2")
    print()
    print("2. Expected packet flow:")
    print("   - Device (192.168.100.2) → TCP SYN → 192.168.100.100:13400")
    print("   - Network → TCP RST → Device (connection refused)")
    print()
    print("3. If you see TCP packets in Wireshark:")
    print("   ✅ TCP stack is working correctly")
    print("   ✅ KSZ8851SNL is transmitting packets")
    print("   ✅ Network configuration is correct")
    print()
    print("4. If you don't see TCP packets:")
    print("   ❌ TCP stack may not be working")
    print("   ❌ KSZ8851SNL may not be transmitting")
    print("   ❌ Network configuration may be incorrect")
    print()
    print("Monitor Wireshark for the next few minutes to see the TCP packets!")

if __name__ == "__main__":
    main()

