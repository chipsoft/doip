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
import math
from typing import Optional, Tuple

# DOIP Protocol Constants
DOIP_UDP_DISCOVERY_PORT = 13400
DOIP_TCP_DATA_PORT = 13400
DOIP_PROTOCOL_VERSION = 0x02
DOIP_INVERSE_PROTOCOL_VERSION = 0xFD

# DOIP Payload Types (ISO 13400)
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
UDS_POSITIVE_RESPONSE_MASK = 0x40

# Data Identifiers (DIDs) - AUTOSAR Standard
DID_VIN = 0xF190
DID_ECU_SOFTWARE_VERSION = 0xF1A0
DID_ECU_HARDWARE_VERSION = 0xF1A1

# System Information DIDs
DID_ACTIVE_DIAGNOSTIC_SESSION = 0xF186
DID_VEHICLE_MANUFACTURER_SPARE_PART_NUMBER = 0xF187
DID_VEHICLE_MANUFACTURER_ECU_SW_NUMBER = 0xF188
DID_VEHICLE_MANUFACTURER_ECU_SW_VERSION = 0xF189
DID_SYSTEM_SUPPLIER_IDENTIFIER = 0xF18A
DID_ECU_MANUFACTURING_DATE = 0xF18B
DID_ECU_SERIAL_NUMBER = 0xF18C
DID_VEHICLE_MANUFACTURER_KIT_ASSEMBLY_PART_NUMBER = 0xF192

# Network/Communication DIDs
DID_VEHICLE_MANUFACTURER_ECU_NETWORK_NAME = 0xF1A2
DID_VEHICLE_MANUFACTURER_ECU_NETWORK_ADDRESS = 0xF1A3
DID_VEHICLE_IDENTIFICATION_DATA_TRACEABILITY = 0xF1A4
DID_VEHICLE_MANUFACTURER_ECU_PIN_TRACEABILITY = 0xF1A5

# Runtime Monitoring DIDs
DID_ECU_OPERATING_HOURS = 0xF1A6
DID_VEHICLE_SPEED_INFORMATION = 0xF1A7
DID_ENGINE_RPM_INFORMATION = 0xF1A8
DID_BATTERY_VOLTAGE_INFORMATION = 0xF1A9
DID_TEMPERATURE_SENSOR_DATA = 0xF1AA
DID_FUEL_LEVEL_INFORMATION = 0xF1AB

# Diagnostic Status DIDs
DID_ERROR_MEMORY_STATUS = 0xF1AC
DID_LAST_RESET_REASON = 0xF1AD
DID_BOOT_SOFTWARE_IDENTIFICATION = 0xF1AE
DID_APPLICATION_SOFTWARE_FINGERPRINT = 0xF1AF

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
        
        # System Information
        self.active_diagnostic_session = 0x01  # Default session
        self.spare_part_number = "DOIP-ECU-EMULATOR-001"
        self.ecu_sw_number = "ECU-SW-PYTHON-001"
        self.ecu_sw_version_detailed = "v1.2.3-python-20240729"
        self.system_supplier_id = "PYTHON_EMU"
        self.ecu_manufacturing_date = "2024-07-29"
        self.ecu_serial_number = "PYTHON-ECU-SN987654"
        self.kit_assembly_part_number = "PYTHON-DOIP-KIT"
        
        # Network Information
        self.ecu_network_name = "DOIP_PYTHON_NET"
        self.ecu_network_address = "192.168.100.100"
        self.identification_data_traceability = "PYTHON-DOIP-TRACE-001"
        self.ecu_pin_traceability = "PIN-TRACE-PYTHON-001"
        
        # Runtime Monitoring (will be updated dynamically)
        self.ecu_operating_hours = 2450  # Hours since manufacturing
        self.vehicle_speed_kmh = 0       # Will be updated dynamically
        self.engine_rpm = 850            # Idle RPM
        self.battery_voltage_mv = 12600  # 12.6V
        self.temperature_celsius = 230   # 23.0°C
        self.fuel_level_percent = 75     # 75%
        
        # Diagnostic Status
        self.error_memory_status = 0x00  # No errors
        self.last_reset_reason = 0x01    # Power-on reset
        self.boot_software_id = "PYTHON-BOOTLOADER-V1.0.0"
        self.application_sw_fingerprint = "SHA256:1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF"
        
        # Dynamic data simulation
        self.simulation_cycle = 0
        self.start_time = time.time()
        
        self.udp_socket = None
        self.tcp_socket = None
        self.running = False
        self.active_connections = []
        
        # Alive check functionality
        self.alive_check_interval = 5.0  # Send alive check every 5 seconds
        self.last_alive_check = 0
        self.alive_check_thread = None

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
        
        # Vehicle announcement payload according to ISO 13400-2:
        # VIN (17 bytes) + Logical Address (2 bytes) + EID (6 bytes) + GID (6 bytes) + Further Action Required (1 byte) + VIN/GID Sync Status (1 byte)
        vin_bytes = self.vin.encode('ascii')[:17].ljust(17, b'\x00')
        
        # Validate VIN is exactly 17 bytes
        if len(vin_bytes) != 17:
            print(f"ERROR: VIN length is {len(vin_bytes)}, expected 17")
            vin_bytes = vin_bytes[:17].ljust(17, b'\x00')
        
        # Validate EID length
        if len(self.entity_id) != 6:
            print(f"ERROR: EID length is {len(self.entity_id)}, expected 6")
        
        # Add GID (6 bytes) and VIN/GID Sync Status for Wireshark compatibility
        gid_bytes = b'\x00\x01\x00\x00\x00\x00'  # 6-byte GID (first 2 bytes set, rest zeros)
        sync_status = b'\x00'     # Synchronized
        
        payload = (
            vin_bytes +
            struct.pack('>H', self.logical_address) +
            self.entity_id +
            gid_bytes +
            b'\x00' +  # Further Action Required
            sync_status
        )
        
        print(f"Payload breakdown: VIN({len(vin_bytes)}) + LA(2) + EID({len(self.entity_id)}) + GID(6) + FAR(1) + SYNC(1) = {len(payload)} bytes")
        
        header = self.create_doip_header(DOIP_VEHICLE_IDENTIFICATION_RESPONSE, len(payload))
        response = header + payload
        
        print(f"Sending vehicle identification response: VIN={self.vin}, Logical Address=0x{self.logical_address:04x}")
        print(f"Response packet: {len(response)} bytes total (header: {len(header)}, payload: {len(payload)})")
        print(f"Response hex: {response.hex()}")
        return response

    def handle_routing_activation_request(self, data: bytes, addr) -> bytes:
        """Handle TCP routing activation request"""
        if len(data) < 7:  # Minimum payload length
            return self.create_negative_ack(0x02)  # Invalid payload length
        
        source_address = struct.unpack('>H', data[8:10])[0]
        activation_type = data[10]
        
        print(f"Routing activation request from {addr}, Source Address: 0x{source_address:04x}, Type: 0x{activation_type:02x}")
        
        # Routing activation response payload:
        # Tester Logical Address (2 bytes) + Entity Logical Address (2 bytes) + Response Code (1 byte)
        payload = struct.pack('>HHB', source_address, self.logical_address, 0x10)  # Success
        
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

    def update_dynamic_data(self):
        """Update dynamic monitoring data for simulation"""
        self.simulation_cycle += 1
        current_time = time.time()
        elapsed_time = current_time - self.start_time
        
        # Simulate realistic vehicle behavior
        if self.simulation_cycle % 10 < 7:  # 70% of time vehicle is moving
            base_speed = 40 + (self.simulation_cycle % 50)
            self.vehicle_speed_kmh = max(0, base_speed + int(10 * math.sin(elapsed_time / 10)))
        else:
            self.vehicle_speed_kmh = 0  # Vehicle stopped
        
        # Engine RPM varies with speed and has some randomness
        if self.vehicle_speed_kmh > 0:
            self.engine_rpm = 1000 + int(self.vehicle_speed_kmh * 25) + int(100 * math.cos(elapsed_time / 5))
        else:
            self.engine_rpm = 800 + int(50 * math.sin(elapsed_time / 3))  # Idle variation
        
        # Battery voltage varies slightly
        self.battery_voltage_mv = 12500 + int(300 * math.sin(elapsed_time / 20)) + (self.simulation_cycle % 200)
        
        # Temperature increases gradually, then cycles
        temp_base = 200 + int(100 * math.sin(elapsed_time / 30))  # 20-30°C base
        self.temperature_celsius = temp_base + (self.simulation_cycle % 150)
        
        # Fuel level decreases over time (very slowly)
        fuel_consumption = int(elapsed_time / 100)  # 1% per 100 seconds
        self.fuel_level_percent = max(5, 100 - fuel_consumption)
        
        # Operating hours increase realistically
        self.ecu_operating_hours = 2450 + int(elapsed_time / 3600)  # Add actual hours

    def handle_read_data_by_identifier(self, did: int) -> Optional[bytes]:
        """Handle UDS Read Data By Identifier service"""
        # Update dynamic data before reading
        self.update_dynamic_data()
        
        # Basic DIDs
        if did == DID_VIN:
            return self.vin.encode('ascii')
        elif did == DID_ECU_SOFTWARE_VERSION:
            return self.ecu_sw_version.encode('ascii')
        elif did == DID_ECU_HARDWARE_VERSION:
            return self.ecu_hw_version.encode('ascii')
        
        # System Information DIDs
        elif did == DID_ACTIVE_DIAGNOSTIC_SESSION:
            return struct.pack('B', self.active_diagnostic_session)
        elif did == DID_VEHICLE_MANUFACTURER_SPARE_PART_NUMBER:
            return self.spare_part_number.encode('ascii')
        elif did == DID_VEHICLE_MANUFACTURER_ECU_SW_NUMBER:
            return self.ecu_sw_number.encode('ascii')
        elif did == DID_VEHICLE_MANUFACTURER_ECU_SW_VERSION:
            return self.ecu_sw_version_detailed.encode('ascii')
        elif did == DID_SYSTEM_SUPPLIER_IDENTIFIER:
            return self.system_supplier_id.encode('ascii')
        elif did == DID_ECU_MANUFACTURING_DATE:
            return self.ecu_manufacturing_date.encode('ascii')
        elif did == DID_ECU_SERIAL_NUMBER:
            return self.ecu_serial_number.encode('ascii')
        elif did == DID_VEHICLE_MANUFACTURER_KIT_ASSEMBLY_PART_NUMBER:
            return self.kit_assembly_part_number.encode('ascii')
        
        # Network/Communication DIDs
        elif did == DID_VEHICLE_MANUFACTURER_ECU_NETWORK_NAME:
            return self.ecu_network_name.encode('ascii')
        elif did == DID_VEHICLE_MANUFACTURER_ECU_NETWORK_ADDRESS:
            return self.ecu_network_address.encode('ascii')
        elif did == DID_VEHICLE_IDENTIFICATION_DATA_TRACEABILITY:
            return self.identification_data_traceability.encode('ascii')
        elif did == DID_VEHICLE_MANUFACTURER_ECU_PIN_TRACEABILITY:
            return self.ecu_pin_traceability.encode('ascii')
        
        # Runtime Monitoring DIDs
        elif did == DID_ECU_OPERATING_HOURS:
            return struct.pack('>I', self.ecu_operating_hours)
        elif did == DID_VEHICLE_SPEED_INFORMATION:
            return struct.pack('>H', self.vehicle_speed_kmh)
        elif did == DID_ENGINE_RPM_INFORMATION:
            return struct.pack('>H', self.engine_rpm)
        elif did == DID_BATTERY_VOLTAGE_INFORMATION:
            return struct.pack('>H', self.battery_voltage_mv)
        elif did == DID_TEMPERATURE_SENSOR_DATA:
            return struct.pack('>h', self.temperature_celsius)
        elif did == DID_FUEL_LEVEL_INFORMATION:
            return struct.pack('B', self.fuel_level_percent)
        
        # Diagnostic Status DIDs
        elif did == DID_ERROR_MEMORY_STATUS:
            return struct.pack('B', self.error_memory_status)
        elif did == DID_LAST_RESET_REASON:
            return struct.pack('B', self.last_reset_reason)
        elif did == DID_BOOT_SOFTWARE_IDENTIFICATION:
            return self.boot_software_id.encode('ascii')
        elif did == DID_APPLICATION_SOFTWARE_FINGERPRINT:
            return self.application_sw_fingerprint.encode('ascii')
        
        else:
            return None

    def create_negative_ack(self, nack_code: int) -> bytes:
        """Create DOIP negative acknowledgment"""
        payload = struct.pack('>B', nack_code)
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK, len(payload))
        return header + payload

    def handle_alive_check_request(self, data: bytes, addr) -> bytes:
        """Handle alive check request (0x0007)"""
        print(f"Alive check request from {addr}")
        
        # Alive check response payload: Source Address (2 bytes)
        source_address = struct.unpack('>H', data[8:10])[0] if len(data) >= 10 else 0x0000
        
        payload = struct.pack('>H', source_address)
        header = self.create_doip_header(DOIP_ALIVE_CHECK_RESPONSE, len(payload))
        response = header + payload
        
        print(f"Alive check response sent to {addr}")
        return response

    def send_alive_check_request(self, client_socket):
        """Send alive check request to client (0x0007)"""
        try:
            # Alive check request payload: Source Address (2 bytes)
            payload = struct.pack('>H', self.logical_address)
            header = self.create_doip_header(DOIP_ALIVE_CHECK_REQUEST, len(payload))
            request = header + payload
            
            client_socket.send(request)
            print(f"Alive check request sent")
            return True
        except Exception as e:
            print(f"Failed to send alive check request: {e}")
            return False

    def send_diagnostic_ack(self, client_socket, ack_type: int = 0x00):
        """Send diagnostic message acknowledgment (0x8002/0x8003)"""
        try:
            # Diagnostic ACK payload: Source Address (2 bytes) + Target Address (2 bytes) + ACK Type (1 byte)
            payload = struct.pack('>HHB', 
                                self.logical_address,  # Source Address
                                0x0E80,               # Target Address (tester)
                                ack_type)             # ACK type (0x00 = positive, 0x01 = negative)
            
            payload_type = DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK if ack_type == 0x00 else DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK
            header = self.create_doip_header(payload_type, len(payload))
            ack_message = header + payload
            
            client_socket.send(ack_message)
            print(f"Diagnostic ACK sent: type 0x{ack_type:02x}")
            return True
        except Exception as e:
            print(f"Failed to send diagnostic ACK: {e}")
            return False

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
                    elif header_info and header_info[1] == DOIP_ALIVE_CHECK_REQUEST:
                        print("Sending alive check response...")
                        response = self.handle_alive_check_request(data, addr)
                        self.udp_socket.sendto(response, addr)
                        print("Alive check response sent!")
                    else:
                        print(f"UDP: Invalid header or unsupported payload type: {header_info}")
                        
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
                elif payload_type == DOIP_ALIVE_CHECK_REQUEST:
                    print(f"TCP: Handling alive check request from {addr}")
                    response = self.handle_alive_check_request(data, addr)
                    print(f"TCP: Sending alive check response ({len(response)} bytes)")
                    client_socket.send(response)
                elif payload_type == DOIP_ALIVE_CHECK_RESPONSE:
                    print(f"TCP: Received alive check response from {addr}")
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

    def alive_check_thread(self):
        """Thread to send periodic alive check requests to connected clients"""
        while self.running:
            try:
                current_time = time.time()
                if current_time - self.last_alive_check >= self.alive_check_interval:
                    # Send alive check to all connected clients
                    for client_socket in self.active_connections[:]:  # Copy list to avoid modification during iteration
                        try:
                            self.send_alive_check_request(client_socket)
                        except Exception as e:
                            print(f"Failed to send alive check to client: {e}")
                            # Remove disconnected client
                            if client_socket in self.active_connections:
                                self.active_connections.remove(client_socket)
                    
                    self.last_alive_check = current_time
                
                time.sleep(1)  # Check every second
                
            except Exception as e:
                if self.running:
                    print(f"Alive check thread error: {e}")
                time.sleep(1)

    def start(self):
        """Start the DOIP ECU emulator"""
        print(f"Starting DOIP ECU Emulator")
        print(f"VIN: {self.vin}")
        print(f"ECU Address: 0x{self.ecu_address:04x}")
        print(f"Logical Address: 0x{self.logical_address:04x}")
        print(f"Supported DOIP Payload Types:")
        print(f"  - Vehicle ID Request/Response (0x0001/0x0004)")
        print(f"  - Routing Activation Request/Response (0x0005/0x0006)")
        print(f"  - Alive Check Request/Response (0x0007/0x0008)")
        print(f"  - Diagnostic Messages (0x8001)")
        print(f"  - Diagnostic ACKs (0x8002/0x8003)")
        print(f"")
        print(f"Supported Data Identifiers (DIDs):")
        print(f"  System Information:")
        print(f"    - VIN (0xF190)")
        print(f"    - ECU SW Version (0xF1A0), HW Version (0xF1A1)")
        print(f"    - Active Diagnostic Session (0xF186)")
        print(f"    - ECU Serial Number (0xF18C)")
        print(f"    - Spare Part Number (0xF187)")
        print(f"  Runtime Monitoring:")
        print(f"    - Vehicle Speed (0xF1A7), Engine RPM (0xF1A8)")
        print(f"    - Battery Voltage (0xF1A9), Temperature (0xF1AA)")
        print(f"    - Fuel Level (0xF1AB), Operating Hours (0xF1A6)")
        print(f"  Diagnostic Status:")
        print(f"    - Error Memory Status (0xF1AC)")
        print(f"    - Last Reset Reason (0xF1AD)")
        print(f"    - Boot Software ID (0xF1AE)")
        print(f"  + {len([x for x in dir() if x.startswith('DID_')]) - 13} additional DIDs")
        print("Press Ctrl+C to stop")
        
        self.running = True
        
        # Start UDP, TCP, and alive check threads
        udp_thread = threading.Thread(target=self.udp_server, daemon=True)
        tcp_thread = threading.Thread(target=self.tcp_server, daemon=True)
        alive_thread = threading.Thread(target=self.alive_check_thread, daemon=True)
        
        udp_thread.start()
        tcp_thread.start()
        alive_thread.start()
        
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