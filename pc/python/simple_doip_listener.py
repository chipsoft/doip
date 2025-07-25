#!/usr/bin/env python3
"""
Simple DOIP Discovery Listener
Listen for UDP packets from embedded device to verify communication
"""

import socket
import struct
import time
from datetime import datetime

def parse_doip_packet(data):
    """Parse DOIP packet and return info"""
    if len(data) < 8:
        return None
    
    try:
        version, inv_version, payload_type, payload_length = struct.unpack('>BBHI', data[:8])
        
        if version == 0x02 and inv_version == 0xFD:
            payload_types = {
                0x0001: "VEHICLE_IDENTIFICATION_REQUEST",
                0x0004: "VEHICLE_IDENTIFICATION_RESPONSE", 
                0x0005: "ROUTING_ACTIVATION_REQUEST",
                0x0006: "ROUTING_ACTIVATION_RESPONSE",
                0x8001: "DIAGNOSTIC_MESSAGE"
            }
            
            return {
                'version': f'0x{version:02x}',
                'payload_type': f'0x{payload_type:04x}',
                'payload_type_name': payload_types.get(payload_type, 'UNKNOWN'),
                'length': payload_length,
                'is_doip': True
            }
    except:
        pass
    
    return {'is_doip': False, 'data': data.hex()}

def listen_for_doip():
    """Listen for DOIP packets on port 13400"""
    print("🎯 Simple DOIP Discovery Listener")
    print("=" * 50)
    print(f"Listening on all interfaces, port 13400")
    print(f"Waiting for DOIP discovery from embedded device...")
    print(f"Press Ctrl+C to stop\n")
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        # Bind to all interfaces on port 13400
        sock.bind(('', 13400))
        print("✅ Successfully bound to port 13400")
        print("🔍 Monitoring for packets...\n")
        
        packet_count = 0
        
        while True:
            try:
                data, addr = sock.recvfrom(1024)
                packet_count += 1
                timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                
                print(f"[{timestamp}] 📦 Packet #{packet_count} from {addr[0]}:{addr[1]}")
                print(f"   Size: {len(data)} bytes")
                print(f"   Raw: {data.hex()}")
                
                # Parse as DOIP
                doip_info = parse_doip_packet(data)
                if doip_info['is_doip']:
                    print(f"   ✅ DOIP: {doip_info['payload_type_name']} ({doip_info['payload_type']})")
                    print(f"   📋 Length: {doip_info['length']} bytes")
                    
                    # If this is a discovery request from the embedded device
                    if (doip_info['payload_type'] == '0x0001' and 
                        addr[0] == '192.168.100.2'):
                        print(f"   🎉 DISCOVERY REQUEST FROM EMBEDDED DEVICE!")
                        print(f"   📡 This proves the network connection is working!")
                        
                        # Send simple response
                        response = struct.pack('>BBHI', 0x02, 0xFD, 0x0004, 28)
                        response += b'WBAVN31010AE12345' + b'\x00' * 11  # VIN + padding
                        sock.sendto(response, addr)
                        print(f"   📤 Sent response back to device")
                        
                else:
                    print(f"   ℹ️  Non-DOIP packet")
                
                print()
                
            except socket.timeout:
                continue
                
    except KeyboardInterrupt:
        print(f"\n🛑 Stopped after receiving {packet_count} packets")
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    listen_for_doip()