#!/usr/bin/env python3
"""
DOIP ECU Emulator
Implements ISO 13400 Diagnostics over Internet Protocol
Emulates an automotive ECU with VIN and diagnostic services
"""

import socket
import struct
import threading
import time
import sys
from typing import Optional, Tuple

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
DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK = 0x8002
DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK = 0x8003

# UDS Service IDs
UDS_READ_DATA_BY_IDENTIFIER = 0x22
UDS_POSITIVE_RESPONSE_MASK = 0x40

# Data Identifiers (DIDs)
DID_VIN = 0xF190
DID_ECU_SOFTWARE_VERSION = 0xF1A0
DID_ECU_HARDWARE_VERSION = 0xF1A1

class DOIPECUEmulator:
    def __init__(self, vin: str = "WBAVN31010AE12345", ecu_address: int = 0x1001):
        self.vin = vin
        self.ecu_address = ecu_address
        self.logical_address = 0x0001
        self.entity_id = b'\x00\x01\x02\x03\x04\x05'  # 6-byte entity ID
        self.group_id = b'\x00\x01'  # 2-byte group ID
        
        # ECU information
        self.ecu_sw_version = "SW_V1.2.3"
        self.ecu_hw_version = "HW_V2.0.1"
        
        self.udp_socket = None
        self.tcp_socket = None
        self.running = False
        self.active_connections = []

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

    def handle_vehicle_identification_request(self, addr) -> bytes:
        """Handle UDP vehicle identification request"""
        print(f"Received vehicle identification request from {addr}")
        
        # Vehicle announcement payload according to ISO 13400:
        # VIN (17 bytes) + Logical Address (2 bytes) + EID (6 bytes) + GID (2 bytes) + 
        # Further Action Required (1 byte) + VIN/GID Sync Status (1 byte)
        vin_bytes = self.vin.encode('ascii')[:17].ljust(17, b'\x00')
        
        payload = (vin_bytes + 
                  struct.pack('>H', self.logical_address) +
                  self.entity_id +
                  self.group_id +
                  b'\x00' +  # No further action required
                  b'\x00')   # VIN/GID sync status (0 = synchronized)
        
        header = self.create_doip_header(DOIP_VEHICLE_IDENTIFICATION_RESPONSE, len(payload))
        response = header + payload
        
        print(f"Sending vehicle identification response: VIN={self.vin}, Logical Address=0x{self.logical_address:04x}")
        return response

    def handle_routing_activation_request(self, data: bytes, addr) -> bytes:
        """Handle TCP routing activation request"""
        if len(data) < 7:  # Minimum payload length
            return self.create_negative_ack(0x02)  # Invalid payload length
        
        source_address = struct.unpack('>H', data[8:10])[0]
        activation_type = data[10]
        
        print(f"Routing activation request from {addr}, Source Address: 0x{source_address:04x}, Type: 0x{activation_type:02x}")
        
        # Routing activation response payload:
        # Logical Address (2 bytes) + Source Address (2 bytes) + Response Code (1 byte)
        payload = struct.pack('>HHB', self.logical_address, source_address, 0x10)  # Success
        
        header = self.create_doip_header(DOIP_ROUTING_ACTIVATION_RESPONSE, len(payload))
        response = header + payload
        
        print(f"Routing activation successful for source 0x{source_address:04x}")
        return response

    def handle_diagnostic_message(self, data: bytes) -> bytes:
        """Handle UDS diagnostic message"""
        if len(data) < 12:  # Header (8) + SA (2) + TA (2) minimum
            return self.create_negative_ack(0x02)
        
        source_address = struct.unpack('>H', data[8:10])[0]
        target_address = struct.unpack('>H', data[10:12])[0]
        uds_data = data[12:]
        
        print(f"Diagnostic message: SA=0x{source_address:04x}, TA=0x{target_address:04x}, Data={uds_data.hex()}")
        
        if len(uds_data) == 0:
            return self.create_negative_ack(0x03)
        
        service_id = uds_data[0]
        
        if service_id == UDS_READ_DATA_BY_IDENTIFIER and len(uds_data) >= 3:
            did = struct.unpack('>H', uds_data[1:3])[0]
            response_data = self.handle_read_data_by_identifier(did)
            
            if response_data:
                # Create positive response
                uds_response = struct.pack('>BH', service_id + UDS_POSITIVE_RESPONSE_MASK, did) + response_data
                payload = struct.pack('>HH', target_address, source_address) + uds_response
                header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
                
                print(f"Sending positive response for DID 0x{did:04x}: {response_data.decode('ascii', errors='ignore')}")
                return header + payload
        
        # Negative response
        print(f"Unsupported service 0x{service_id:02x}")
        return self.create_negative_ack(0x03)

    def handle_read_data_by_identifier(self, did: int) -> Optional[bytes]:
        """Handle UDS Read Data By Identifier service"""
        if did == DID_VIN:
            return self.vin.encode('ascii')
        elif did == DID_ECU_SOFTWARE_VERSION:
            return self.ecu_sw_version.encode('ascii')
        elif did == DID_ECU_HARDWARE_VERSION:
            return self.ecu_hw_version.encode('ascii')
        else:
            return None

    def create_negative_ack(self, nack_code: int) -> bytes:
        """Create DOIP negative acknowledgment"""
        payload = struct.pack('>B', nack_code)
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK, len(payload))
        return header + payload

    def get_doip_interface_ip(self):
        """Get the IP address for DOIP communication (192.168.100.x network)"""
        try:
            import netifaces
            for iface in netifaces.interfaces():
                try:
                    addrs = netifaces.ifaddresses(iface)
                    if netifaces.AF_INET in addrs:
                        ip_info = addrs[netifaces.AF_INET][0]
                        ip = ip_info.get('addr', '')
                        if ip.startswith('192.168.100.'):
                            print(f"Found DOIP interface {iface}: {ip}")
                            return ip
                except:
                    continue
        except ImportError:
            print("netifaces not available, trying to use 192.168.100.1")
            # Test if 192.168.100.1 is available
            try:
                test_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                test_sock.bind(('192.168.100.1', 0))
                test_sock.close()
                print("Using 192.168.100.1 for DOIP interface")
                return '192.168.100.1'
            except:
                print("192.168.100.1 not available, using all interfaces")
        
        # Fallback to all interfaces
        return ''

    def udp_server(self):
        """UDP discovery server thread"""
        self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        # Get the appropriate interface IP
        bind_ip = self.get_doip_interface_ip()
        
        try:
            self.udp_socket.bind(('0.0.0.0', DOIP_UDP_DISCOVERY_PORT))
            print(f"UDP discovery server listening on all interfaces:{DOIP_UDP_DISCOVERY_PORT}")
            
            while self.running:
                try:
                    data, addr = self.udp_socket.recvfrom(1024)
                    
                    print(f"UDP received {len(data)} bytes from {addr}: {data.hex()}")
                    
                    header_info = self.parse_doip_header(data)
                    print(f"Parsed header: {header_info}")
                    
                    if header_info and header_info[1] == DOIP_VEHICLE_IDENTIFICATION_REQUEST:
                        print("Sending vehicle identification response...")
                        response = self.handle_vehicle_identification_request(addr)
                        self.udp_socket.sendto(response, addr)
                        print("Response sent!")
                    else:
                        print(f"UDP: Invalid header or wrong payload type: {header_info}")
                        
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"UDP server error: {e}")
                        
        finally:
            if self.udp_socket:
                self.udp_socket.close()

    def handle_tcp_client(self, client_socket, addr):
        """Handle individual TCP client connection"""
        print(f"TCP client connected from {addr}")
        
        try:
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                
                print(f"TCP received {len(data)} bytes from {addr}: {data.hex()}")
                
                header_info = self.parse_doip_header(data)
                if not header_info:
                    print(f"TCP: Invalid header from {addr}")
                    continue
                
                _, payload_type, payload_length = header_info
                print(f"TCP: Payload type 0x{payload_type:04x}, length {payload_length}")
                
                if payload_type == DOIP_ROUTING_ACTIVATION_REQUEST:
                    print(f"TCP: Handling routing activation request from {addr}")
                    response = self.handle_routing_activation_request(data, addr)
                    print(f"TCP: Sending routing activation response ({len(response)} bytes)")
                    client_socket.send(response)
                elif payload_type == DOIP_DIAGNOSTIC_MESSAGE:
                    print(f"TCP: Handling diagnostic message from {addr}")
                    response = self.handle_diagnostic_message(data)
                    print(f"TCP: Sending diagnostic response ({len(response)} bytes)")
                    client_socket.send(response)
                else:
                    print(f"Unsupported payload type: 0x{payload_type:04x}")
                    
        except Exception as e:
            print(f"TCP client error: {e}")
        finally:
            client_socket.close()
            if client_socket in self.active_connections:
                self.active_connections.remove(client_socket)
            print(f"TCP client {addr} disconnected")

    def tcp_server(self):
        """TCP diagnostic server thread"""
        self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp_socket.settimeout(1.0)
        
        # Get the appropriate interface IP
        bind_ip = self.get_doip_interface_ip()
        
        try:
            self.tcp_socket.bind(('0.0.0.0', DOIP_TCP_DATA_PORT))
            self.tcp_socket.listen(5)
            print(f"TCP diagnostic server listening on all interfaces:{DOIP_TCP_DATA_PORT}")
            
            while self.running:
                try:
                    client_socket, addr = self.tcp_socket.accept()
                    self.active_connections.append(client_socket)
                    
                    # Handle each client in a separate thread
                    client_thread = threading.Thread(
                        target=self.handle_tcp_client,
                        args=(client_socket, addr),
                        daemon=True
                    )
                    client_thread.start()
                    
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"TCP server error: {e}")
                        
        finally:
            if self.tcp_socket:
                self.tcp_socket.close()

    def start(self):
        """Start the DOIP ECU emulator"""
        print(f"Starting DOIP ECU Emulator")
        print(f"VIN: {self.vin}")
        print(f"ECU Address: 0x{self.ecu_address:04x}")
        print(f"Logical Address: 0x{self.logical_address:04x}")
        print("Press Ctrl+C to stop")
        
        self.running = True
        
        # Start UDP and TCP servers
        udp_thread = threading.Thread(target=self.udp_server, daemon=True)
        tcp_thread = threading.Thread(target=self.tcp_server, daemon=True)
        
        udp_thread.start()
        tcp_thread.start()
        
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\nShutting down DOIP ECU emulator...")
            self.stop()

    def stop(self):
        """Stop the DOIP ECU emulator"""
        self.running = False
        
        # Close all active connections
        for conn in self.active_connections:
            conn.close()
        
        if self.udp_socket:
            self.udp_socket.close()
        if self.tcp_socket:
            self.tcp_socket.close()

if __name__ == "__main__":
    # Allow VIN to be specified as command line argument
    vin = sys.argv[1] if len(sys.argv) > 1 else "WBAVN31010AE12345"
    
    emulator = DOIPECUEmulator(vin=vin)
    emulator.start()