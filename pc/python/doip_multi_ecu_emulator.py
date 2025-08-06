#!/usr/bin/env python3
"""
Multi-ECU DOIP Vehicle Emulator
Implements ISO 13400 Diagnostics over Internet Protocol
Emulates multiple automotive ECUs in a single vehicle
"""

import socket
import struct
import threading
import time
import sys
import math
from typing import Optional, Tuple, Dict, List

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
DID_ACTIVE_DIAGNOSTIC_SESSION = 0xF186
DID_VEHICLE_MANUFACTURER_SPARE_PART_NUMBER = 0xF187
DID_VEHICLE_MANUFACTURER_ECU_SW_NUMBER = 0xF188
DID_VEHICLE_MANUFACTURER_ECU_SW_VERSION = 0xF189
DID_SYSTEM_SUPPLIER_IDENTIFIER = 0xF18A
DID_ECU_MANUFACTURING_DATE = 0xF18B
DID_ECU_SERIAL_NUMBER = 0xF18C
DID_VEHICLE_MANUFACTURER_KIT_ASSEMBLY_PART_NUMBER = 0xF192
DID_VEHICLE_MANUFACTURER_ECU_NETWORK_NAME = 0xF1A2
DID_VEHICLE_MANUFACTURER_ECU_NETWORK_ADDRESS = 0xF1A3
DID_VEHICLE_IDENTIFICATION_DATA_TRACEABILITY = 0xF1A4
DID_VEHICLE_MANUFACTURER_ECU_PIN_TRACEABILITY = 0xF1A5
DID_ECU_OPERATING_HOURS = 0xF1A6
DID_VEHICLE_SPEED_INFORMATION = 0xF1A7
DID_ENGINE_RPM_INFORMATION = 0xF1A8
DID_BATTERY_VOLTAGE_INFORMATION = 0xF1A9
DID_TEMPERATURE_SENSOR_DATA = 0xF1AA
DID_FUEL_LEVEL_INFORMATION = 0xF1AB
DID_ERROR_MEMORY_STATUS = 0xF1AC
DID_LAST_RESET_REASON = 0xF1AD
DID_BOOT_SOFTWARE_IDENTIFICATION = 0xF1AE
DID_APPLICATION_SOFTWARE_FINGERPRINT = 0xF1AF

class VirtualECU:
    """Represents a single ECU in the vehicle"""
    
    def __init__(self, ecu_type: str, logical_address: int, base_vin: str):
        self.ecu_type = ecu_type
        self.logical_address = logical_address
        self.physical_address = 0x1000 + logical_address  # Physical address derived from logical
        
        # Create unique VIN by modifying last digits
        self.vin = base_vin[:-1] + str(logical_address)
        
        # ECU-specific information
        self.entity_id = struct.pack('>HI', logical_address, 0x01020304)  # 6-byte entity ID
        self.group_id = b'\x00\x01'  # 2-byte group ID
        
        # Common ECU information with type-specific variations
        self.ecu_sw_version = f"SW_V{logical_address}.2.3"
        self.ecu_hw_version = f"HW_V{logical_address}.0.1"
        self.active_diagnostic_session = 0x01
        self.spare_part_number = f"DOIP-{ecu_type.upper()}-ECU-{logical_address:03d}"
        self.ecu_sw_number = f"ECU-SW-{ecu_type.upper()}-{logical_address:03d}"
        self.ecu_sw_version_detailed = f"v{logical_address}.2.3-{ecu_type.lower()}-20240729"
        self.system_supplier_id = f"{ecu_type.upper()}_EMU"
        self.ecu_manufacturing_date = "2024-07-29"
        self.ecu_serial_number = f"{ecu_type.upper()}-ECU-SN{logical_address:06d}"
        self.kit_assembly_part_number = f"{ecu_type.upper()}-DOIP-KIT"
        self.ecu_network_name = f"DOIP_{ecu_type.upper()}_NET"
        self.ecu_network_address = f"192.168.100.{100 + logical_address}"
        self.identification_data_traceability = f"{ecu_type.upper()}-DOIP-TRACE-{logical_address:03d}"
        self.ecu_pin_traceability = f"PIN-TRACE-{ecu_type.upper()}-{logical_address:03d}"
        self.boot_software_id = f"{ecu_type.upper()}-BOOTLOADER-V1.0.0"
        self.application_sw_fingerprint = f"SHA256:{logical_address:04X}" + "0" * 60
        
        # Dynamic data simulation
        self.simulation_cycle = 0
        self.start_time = time.time()
        
        # Initialize ECU-specific data based on type
        self._initialize_ecu_specific_data()
    
    def _initialize_ecu_specific_data(self):
        """Initialize ECU-specific data based on ECU type"""
        if self.ecu_type == "ENGINE":
            self.ecu_operating_hours = 2450
            self.vehicle_speed_kmh = 0
            self.engine_rpm = 850
            self.battery_voltage_mv = 12600
            self.temperature_celsius = 230  # Engine temp
            self.fuel_level_percent = 75
            self.error_memory_status = 0x00
            self.last_reset_reason = 0x01
            
        elif self.ecu_type == "TRANSMISSION":
            self.ecu_operating_hours = 2400
            self.vehicle_speed_kmh = 0
            self.engine_rpm = 0  # Not applicable
            self.battery_voltage_mv = 12600
            self.temperature_celsius = 180  # Transmission temp
            self.fuel_level_percent = 0  # Not applicable
            self.error_memory_status = 0x00
            self.last_reset_reason = 0x01
            
        elif self.ecu_type == "ABS":
            self.ecu_operating_hours = 2380
            self.vehicle_speed_kmh = 0
            self.engine_rpm = 0  # Not applicable
            self.battery_voltage_mv = 12600
            self.temperature_celsius = 150  # Brake temp
            self.fuel_level_percent = 0  # Not applicable
            self.error_memory_status = 0x00
            self.last_reset_reason = 0x01
            
        elif self.ecu_type == "BCM":  # Body Control Module
            self.ecu_operating_hours = 2500
            self.vehicle_speed_kmh = 0
            self.engine_rpm = 0  # Not applicable
            self.battery_voltage_mv = 12600
            self.temperature_celsius = 200  # Ambient temp
            self.fuel_level_percent = 0  # Not applicable
            self.error_memory_status = 0x00
            self.last_reset_reason = 0x01
    
    def update_dynamic_data(self):
        """Update dynamic monitoring data for simulation"""
        self.simulation_cycle += 1
        current_time = time.time()
        elapsed_time = current_time - self.start_time
        
        # ECU-specific dynamic data updates
        if self.ecu_type == "ENGINE":
            # Simulate realistic vehicle behavior
            if self.simulation_cycle % 10 < 7:  # 70% of time vehicle is moving
                base_speed = 40 + (self.simulation_cycle % 50)
                self.vehicle_speed_kmh = max(0, base_speed + int(10 * math.sin(elapsed_time / 10)))
            else:
                self.vehicle_speed_kmh = 0  # Vehicle stopped
            
            # Engine RPM varies with speed
            if self.vehicle_speed_kmh > 0:
                self.engine_rpm = 1000 + int(self.vehicle_speed_kmh * 25) + int(100 * math.cos(elapsed_time / 5))
            else:
                self.engine_rpm = 800 + int(50 * math.sin(elapsed_time / 3))  # Idle variation
            
            # Engine temperature
            temp_base = 200 + int(100 * math.sin(elapsed_time / 30))
            self.temperature_celsius = temp_base + (self.simulation_cycle % 150)
            
            # Fuel level decreases over time
            fuel_consumption = int(elapsed_time / 100)
            self.fuel_level_percent = max(5, 100 - fuel_consumption)
            
        elif self.ecu_type == "TRANSMISSION":
            # Transmission sees same vehicle speed as engine
            if self.simulation_cycle % 10 < 7:
                base_speed = 40 + (self.simulation_cycle % 50)
                self.vehicle_speed_kmh = max(0, base_speed + int(10 * math.sin(elapsed_time / 10)))
            else:
                self.vehicle_speed_kmh = 0
            
            # Transmission temperature
            self.temperature_celsius = 180 + int(50 * math.sin(elapsed_time / 25)) + (self.simulation_cycle % 100)
            
        elif self.ecu_type == "ABS":
            # ABS also sees vehicle speed
            if self.simulation_cycle % 10 < 7:
                base_speed = 40 + (self.simulation_cycle % 50)
                self.vehicle_speed_kmh = max(0, base_speed + int(10 * math.sin(elapsed_time / 10)))
            else:
                self.vehicle_speed_kmh = 0
            
            # Brake temperature (higher when braking)
            brake_temp_base = 150 + (50 if self.vehicle_speed_kmh > 50 else 0)
            self.temperature_celsius = brake_temp_base + int(30 * math.sin(elapsed_time / 15))
            
        elif self.ecu_type == "BCM":
            # BCM doesn't track vehicle speed directly
            self.vehicle_speed_kmh = 0
            
            # Ambient temperature
            self.temperature_celsius = 200 + int(80 * math.sin(elapsed_time / 40))
        
        # Battery voltage common to all ECUs
        self.battery_voltage_mv = 12500 + int(300 * math.sin(elapsed_time / 20)) + (self.simulation_cycle % 200)
        
        # Operating hours increase for all ECUs
        self.ecu_operating_hours += int(elapsed_time / 3600)
    
    def handle_read_data_by_identifier(self, did: int) -> Optional[bytes]:
        """Handle UDS Read Data By Identifier service for this ECU"""
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
        
        # Runtime Monitoring DIDs (ECU-specific responses)
        elif did == DID_ECU_OPERATING_HOURS:
            return struct.pack('>I', self.ecu_operating_hours)
        elif did == DID_VEHICLE_SPEED_INFORMATION:
            return struct.pack('>H', self.vehicle_speed_kmh)
        elif did == DID_ENGINE_RPM_INFORMATION:
            if self.ecu_type == "ENGINE":
                return struct.pack('>H', self.engine_rpm)
            else:
                return None  # Not supported by this ECU type
        elif did == DID_BATTERY_VOLTAGE_INFORMATION:
            return struct.pack('>H', self.battery_voltage_mv)
        elif did == DID_TEMPERATURE_SENSOR_DATA:
            return struct.pack('>h', self.temperature_celsius)
        elif did == DID_FUEL_LEVEL_INFORMATION:
            if self.ecu_type == "ENGINE":
                return struct.pack('B', self.fuel_level_percent)
            else:
                return None  # Not supported by this ECU type
        
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

class DOIPVehicleEmulator:
    """Multi-ECU DOIP Vehicle Emulator"""
    
    def __init__(self, base_vin: str = "WBAVN31010AE1234"):
        self.base_vin = base_vin
        self.ecus: Dict[int, VirtualECU] = {}
        
        # Create multiple ECUs with different types and logical addresses
        self.ecus[0x0001] = VirtualECU("ENGINE", 0x0001, base_vin)
        self.ecus[0x0002] = VirtualECU("TRANSMISSION", 0x0002, base_vin)
        self.ecus[0x0003] = VirtualECU("ABS", 0x0003, base_vin)
        self.ecus[0x0004] = VirtualECU("BCM", 0x0004, base_vin)
        
        self.udp_socket = None
        self.tcp_socket = None
        self.running = False
        self.active_connections = []
        
        # Alive check functionality
        self.alive_check_interval = 5.0
        self.last_alive_check = 0
    
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
    
    def handle_vehicle_identification_request(self, addr) -> List[bytes]:
        """Handle UDP vehicle identification request - return responses for all ECUs"""
        print(f"Received vehicle identification request from {addr}")
        responses = []
        
        # Send response for each ECU
        for logical_addr, ecu in self.ecus.items():
            print(f"Sending identification response for ECU {ecu.ecu_type} (0x{logical_addr:04x})")
            
            # Vehicle announcement payload according to ISO 13400-2
            vin_bytes = ecu.vin.encode('ascii')[:17].ljust(17, b'\x00')
            
            # Create GID bytes (6 bytes total, first 2 bytes are group ID)
            gid_bytes = ecu.group_id + b'\x00\x00\x00\x00'  # 6-byte GID
            sync_status = b'\x00'  # Synchronized
            
            payload = (
                vin_bytes +
                struct.pack('>H', logical_addr) +
                ecu.entity_id +
                gid_bytes +
                b'\x00' +  # Further Action Required
                sync_status
            )
            
            header = self.create_doip_header(DOIP_VEHICLE_IDENTIFICATION_RESPONSE, len(payload))
            response = header + payload
            responses.append(response)
            
            print(f"ECU {ecu.ecu_type}: VIN={ecu.vin}, Logical Address=0x{logical_addr:04x}")
        
        return responses
    
    def handle_routing_activation_request(self, data: bytes, addr) -> bytes:
        """Handle TCP routing activation request"""
        if len(data) < 7:
            return self.create_negative_ack(0x02)
        
        source_address = struct.unpack('>H', data[8:10])[0]
        activation_type = data[10]
        
        print(f"Routing activation request from {addr}, Source Address: 0x{source_address:04x}, Type: 0x{activation_type:02x}")
        
        # For multi-ECU, we accept routing activation generically
        # The actual ECU routing happens in diagnostic messages
        payload = struct.pack('>HHB', source_address, 0x0000, 0x10)  # Success with generic address
        
        header = self.create_doip_header(DOIP_ROUTING_ACTIVATION_RESPONSE, len(payload))
        response = header + payload
        
        print(f"Routing activation successful for source 0x{source_address:04x}")
        return response
    
    def handle_diagnostic_message(self, data: bytes) -> bytes:
        """Handle UDS diagnostic message - route to appropriate ECU"""
        if len(data) < 12:
            return self.create_negative_ack(0x02)
        
        source_address = struct.unpack('>H', data[8:10])[0]
        target_address = struct.unpack('>H', data[10:12])[0]
        uds_data = data[12:]
        
        print(f"Diagnostic message: SA=0x{source_address:04x}, TA=0x{target_address:04x}, Data={uds_data.hex()}")
        
        # Find the target ECU
        target_ecu = self.ecus.get(target_address)
        if not target_ecu:
            print(f"No ECU found for target address 0x{target_address:04x}")
            return self.create_negative_ack(0x03)
        
        if len(uds_data) == 0:
            return self.create_negative_ack(0x03)
        
        service_id = uds_data[0]
        
        if service_id == UDS_READ_DATA_BY_IDENTIFIER and len(uds_data) >= 3:
            did = struct.unpack('>H', uds_data[1:3])[0]
            response_data = target_ecu.handle_read_data_by_identifier(did)
            
            if response_data:
                # Create positive response with ECU identification
                uds_response = struct.pack('>BH', service_id + UDS_POSITIVE_RESPONSE_MASK, did) + response_data
                payload = struct.pack('>HH', target_address, source_address) + uds_response
                header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
                
                print(f"ECU {target_ecu.ecu_type} sending positive response for DID 0x{did:04x}")
                return header + payload
            else:
                print(f"ECU {target_ecu.ecu_type} does not support DID 0x{did:04x}")
                return self.create_negative_ack(0x31)  # Request out of range
        
        print(f"Unsupported service 0x{service_id:02x}")
        return self.create_negative_ack(0x11)  # Service not supported
    
    def create_negative_ack(self, nack_code: int) -> bytes:
        """Create DOIP negative acknowledgment"""
        payload = struct.pack('>B', nack_code)
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK, len(payload))
        return header + payload
    
    def handle_alive_check_request(self, data: bytes, addr) -> bytes:
        """Handle alive check request"""
        print(f"Alive check request from {addr}")
        
        source_address = struct.unpack('>H', data[8:10])[0] if len(data) >= 10 else 0x0000
        
        payload = struct.pack('>H', source_address)
        header = self.create_doip_header(DOIP_ALIVE_CHECK_RESPONSE, len(payload))
        response = header + payload
        
        print(f"Alive check response sent to {addr}")
        return response
    
    def udp_server(self):
        """UDP discovery server thread"""
        self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            self.udp_socket.bind(('0.0.0.0', DOIP_UDP_DISCOVERY_PORT))
            print(f"UDP discovery server listening on all interfaces:{DOIP_UDP_DISCOVERY_PORT}")
            
            while self.running:
                try:
                    data, addr = self.udp_socket.recvfrom(1024)
                    
                    print(f"UDP received {len(data)} bytes from {addr}: {data.hex()}")
                    
                    header_info = self.parse_doip_header(data)
                    
                    if header_info and header_info[1] == DOIP_VEHICLE_IDENTIFICATION_REQUEST:
                        print("Sending vehicle identification responses for all ECUs...")
                        responses = self.handle_vehicle_identification_request(addr)
                        for response in responses:
                            self.udp_socket.sendto(response, addr)
                            time.sleep(0.01)  # Small delay between ECU responses
                        print(f"Sent {len(responses)} ECU identification responses!")
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
                    client_socket.send(response)
                elif payload_type == DOIP_DIAGNOSTIC_MESSAGE:
                    print(f"TCP: Handling diagnostic message from {addr}")
                    response = self.handle_diagnostic_message(data)
                    client_socket.send(response)
                elif payload_type == DOIP_ALIVE_CHECK_REQUEST:
                    print(f"TCP: Handling alive check request from {addr}")
                    response = self.handle_alive_check_request(data, addr)
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
        
        try:
            self.tcp_socket.bind(('0.0.0.0', DOIP_TCP_DATA_PORT))
            self.tcp_socket.listen(5)
            print(f"TCP diagnostic server listening on all interfaces:{DOIP_TCP_DATA_PORT}")
            
            while self.running:
                try:
                    client_socket, addr = self.tcp_socket.accept()
                    self.active_connections.append(client_socket)
                    
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
        """Start the multi-ECU DOIP vehicle emulator"""
        print("Starting Multi-ECU DOIP Vehicle Emulator")
        print(f"Base VIN: {self.base_vin}")
        print(f"Configured ECUs:")
        for logical_addr, ecu in self.ecus.items():
            print(f"  - {ecu.ecu_type}: Logical Address 0x{logical_addr:04x}, VIN {ecu.vin}")
        
        print(f"")
        print(f"Supported DOIP Features:")
        print(f"  - Multi-ECU vehicle identification")
        print(f"  - ECU-specific diagnostic routing")
        print(f"  - Concurrent client connections")
        print(f"  - Dynamic data simulation per ECU type")
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
            print("\nShutting down Multi-ECU DOIP vehicle emulator...")
            self.stop()
    
    def stop(self):
        """Stop the multi-ECU vehicle emulator"""
        self.running = False
        
        for conn in self.active_connections:
            conn.close()
        
        if self.udp_socket:
            self.udp_socket.close()
        if self.tcp_socket:
            self.tcp_socket.close()

if __name__ == "__main__":
    # Allow base VIN to be specified as command line argument
    base_vin = sys.argv[1] if len(sys.argv) > 1 else "WBAVN31010AE1234"
    
    emulator = DOIPVehicleEmulator(base_vin=base_vin)
    emulator.start()