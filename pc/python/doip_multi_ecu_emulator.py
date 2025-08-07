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

# ANSI color codes for enhanced output
class Colors:
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    RESET = '\033[0m'  # Reset to default color

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
UDS_WRITE_DATA_BY_IDENTIFIER = 0x2E
UDS_LARGE_MESSAGE_TEST = 0x3E  # Custom service for large message testing
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

    def handle_large_message_test(self, test_data: bytes) -> Optional[bytes]:
        """Handle large message test service - echo back data with length info"""
        received_length = len(test_data)
        
        # Enhanced logging for large message test
        print(f"ECU {Colors.BLUE}{self.ecu_type}{Colors.RESET}: {Colors.CYAN}{Colors.BOLD}🔧 LARGE MESSAGE TEST:{Colors.RESET}")
        
        # Use enhanced size formatting
        size_display = self.format_message_size(received_length)
        print(f"   📥 Received: {size_display}")
        
        # Create response with received length and echo back the data
        # Format: Length (4 bytes) + Original Data  
        response_data = struct.pack('>I', received_length) + test_data
        
        # Enhanced response logging
        response_size_display = self.format_message_size(len(response_data))
        print(f"   📤 Echoing: {response_size_display}")
        
        # Additional analysis for large messages
        if received_length > 1400:
            print(f"   ⚠️  Note: Message exceeds TCP MSS (1460 bytes) - chunking may be required")
        if received_length > 4096:
            print(f"   🔥 Note: Very large message - intensive processing")
        if received_length > 8192:
            print(f"   🚀 Note: Mega message - maximum processing required")
        
        return response_data

class DOIPVehicleEmulator:
    """Multi-ECU DOIP Vehicle Emulator"""
    
    def __init__(self, base_vin: str = "WBAVN31010AE1234"):
        self.base_vin = base_vin
        self.ecus: Dict[int, VirtualECU] = {}
        
        # Large message support constants
        self.max_message_size = 256 * 1024  # 256KB maximum message size
        
        # Create multiple ECUs with different types and logical addresses
        self.ecus[0x0001] = VirtualECU("ENGINE", 0x0001, base_vin)
        self.ecus[0x0002] = VirtualECU("TRANSMISSION", 0x0002, base_vin)
        self.ecus[0x0003] = VirtualECU("ABS", 0x0003, base_vin)
        self.ecus[0x0004] = VirtualECU("BCM", 0x0004, base_vin)
        
        self.udp_socket = None
        self.tcp_socket = None
        self.running = False
        self.active_connections = []
        
        # Connection reliability improvements
        self.connection_lock = threading.Lock()
        self.max_concurrent_connections = 10
        self.connection_rate_limit = 0.1  # Minimum 100ms between connections
        self.last_connection_time = 0
        self.connection_retry_delay = 0.5  # 500ms delay between retries
        self.max_connection_retries = 3
        
        # Alive check functionality
        self.alive_check_interval = 5.0
        self.last_alive_check = 0
        
        # Connection statistics
        self.connection_stats = {
            'total_connections': 0,
            'successful_connections': 0,
            'failed_connections': 0,
            'active_connections': 0,
            'connection_errors': []
        }
        
        # Message processing statistics
        self.message_stats = {
            'total_messages': 0,
            'small_messages': 0,      # < 1KB
            'medium_messages': 0,     # 1KB - 1.4KB
            'large_messages': 0,      # 1.4KB - 4KB
            'very_large_messages': 0, # 4KB - 8KB
            'mega_messages': 0,       # > 8KB
            'total_bytes_processed': 0,
            'largest_message': 0
        }
    
    def check_port_availability(self, port: int, protocol: str = "TCP") -> bool:
        """Check if a port is available for binding"""
        try:
            if protocol.upper() == "TCP":
                test_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            else:
                test_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            
            test_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            test_socket.bind(('0.0.0.0', port))
            test_socket.close()
            return True
        except OSError:
            return False
    
    def check_required_ports(self) -> bool:
        """Check if all required ports are available"""
        print(f"{Colors.CYAN}🔍 Checking port availability...{Colors.RESET}")
        
        udp_available = self.check_port_availability(DOIP_UDP_DISCOVERY_PORT, "UDP")
        tcp_available = self.check_port_availability(DOIP_TCP_DATA_PORT, "TCP")
        
        if udp_available:
            print(f"{Colors.GREEN}✅ UDP Port {DOIP_UDP_DISCOVERY_PORT} is available{Colors.RESET}")
        else:
            print(f"{Colors.RED}❌ UDP Port {DOIP_UDP_DISCOVERY_PORT} is already in use{Colors.RESET}")
        
        if tcp_available:
            print(f"{Colors.GREEN}✅ TCP Port {DOIP_TCP_DATA_PORT} is available{Colors.RESET}")
        else:
            print(f"{Colors.RED}❌ TCP Port {DOIP_TCP_DATA_PORT} is already in use{Colors.RESET}")
        
        if not udp_available or not tcp_available:
            print(f"\n{Colors.YELLOW}💡 Troubleshooting tips:{Colors.RESET}")
            print(f"   • Check for existing emulator processes: ps aux | grep doip")
            print(f"   • Check port usage: lsof -i :{DOIP_UDP_DISCOVERY_PORT} && lsof -i :{DOIP_TCP_DATA_PORT}")
            print(f"   • Kill existing processes: pkill -f doip_multi_ecu_emulator")
            print(f"   • Or use different ports by modifying the script")
            return False
        
        return True
    
    def can_accept_connection(self) -> bool:
        """Check if we can accept a new connection based on rate limiting and capacity"""
        current_time = time.time()
        
        # Rate limiting check
        if current_time - self.last_connection_time < self.connection_rate_limit:
            return False
        
        # Capacity check
        with self.connection_lock:
            if len(self.active_connections) >= self.max_concurrent_connections:
                return False
        
        return True
    
    def register_connection(self, client_socket, addr):
        """Register a new connection with statistics"""
        with self.connection_lock:
            self.active_connections.append(client_socket)
            self.connection_stats['total_connections'] += 1
            self.connection_stats['successful_connections'] += 1
            self.connection_stats['active_connections'] = len(self.active_connections)
            self.last_connection_time = time.time()
        
        print(f"{Colors.GREEN}✅ Connection accepted from {addr} (Active: {len(self.active_connections)}){Colors.RESET}")
    
    def unregister_connection(self, client_socket, addr, error=None):
        """Unregister a connection and update statistics"""
        with self.connection_lock:
            if client_socket in self.active_connections:
                self.active_connections.remove(client_socket)
                self.connection_stats['active_connections'] = len(self.active_connections)
            
            if error:
                self.connection_stats['failed_connections'] += 1
                self.connection_stats['connection_errors'].append(f"{addr}: {error}")
        
        print(f"{Colors.YELLOW}🔌 Connection closed from {addr} (Active: {len(self.active_connections)}){Colors.RESET}")
    
    def update_message_stats(self, message_size: int):
        """Update message processing statistics"""
        self.message_stats['total_messages'] += 1
        self.message_stats['total_bytes_processed'] += message_size
        
        if message_size > self.message_stats['largest_message']:
            self.message_stats['largest_message'] = message_size
        
        category = self.get_message_size_category(message_size)
        if category == "SMALL":
            self.message_stats['small_messages'] += 1
        elif category == "MEDIUM":
            self.message_stats['medium_messages'] += 1
        elif category == "LARGE":
            self.message_stats['large_messages'] += 1
        elif category == "VERY_LARGE":
            self.message_stats['very_large_messages'] += 1
        elif category == "MEGA":
            self.message_stats['mega_messages'] += 1
    
    def print_message_stats(self):
        """Print current message processing statistics"""
        stats = self.message_stats.copy()
        
        print(f"\n{Colors.CYAN}📊 Message Processing Statistics:{Colors.RESET}")
        print(f"  Total Messages: {stats['total_messages']}")
        print(f"  Total Bytes Processed: {stats['total_bytes_processed']:,} bytes ({stats['total_bytes_processed']/1024:.1f} KB)")
        print(f"  Largest Message: {stats['largest_message']} bytes ({stats['largest_message']/1024:.1f} KB)")
        print(f"  Message Size Distribution:")
        print(f"    📄 Small (<1KB): {stats['small_messages']}")
        print(f"    📋 Medium (1-1.4KB): {stats['medium_messages']}")
        print(f"    📦 Large (1.4-4KB): {stats['large_messages']}")
        print(f"    📦🔥 Very Large (4-8KB): {stats['very_large_messages']}")
        print(f"    📦💥🚀 Mega (>8KB): {stats['mega_messages']}")
    
    def print_connection_stats(self):
        """Print current connection statistics"""
        with self.connection_lock:
            stats = self.connection_stats.copy()
        
        print(f"\n{Colors.CYAN}📊 Connection Statistics:{Colors.RESET}")
        print(f"  Total Connections: {stats['total_connections']}")
        print(f"  Successful: {stats['successful_connections']}")
        print(f"  Failed: {stats['failed_connections']}")
        print(f"  Active: {stats['active_connections']}")
        
        if stats['connection_errors']:
            print(f"  Recent Errors: {len(stats['connection_errors'][-5:])}")
            for error in stats['connection_errors'][-3:]:
                print(f"    - {error}")
    
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
    
    def format_message_size(self, size_bytes: int) -> str:
        """Format message size with color coding based on size"""
        if size_bytes < 1024:  # Less than 1KB
            return f"{size_bytes} bytes"
        elif size_bytes < 1400:  # Less than MTU
            return f"{Colors.YELLOW}{size_bytes} bytes ({size_bytes/1024:.1f} KB){Colors.RESET}"
        elif size_bytes < 4096:  # Large message (1.4KB - 4KB)
            return f"{Colors.MAGENTA}{Colors.BOLD}{size_bytes} bytes ({size_bytes/1024:.1f} KB){Colors.RESET}"
        elif size_bytes < 8192:  # Very large message (4KB - 8KB)
            return f"{Colors.RED}{Colors.BOLD}*** LARGE MESSAGE: {size_bytes} bytes ({size_bytes/1024:.1f} KB) ***{Colors.RESET}"
        else:  # Extremely large message (>8KB)
            return f"{Colors.RED}{Colors.BOLD}{Colors.UNDERLINE}*** MEGA MESSAGE: {size_bytes} bytes ({size_bytes/1024:.1f} KB) ***{Colors.RESET}"
    
    def get_message_size_category(self, size_bytes: int) -> str:
        """Get message size category for enhanced logging"""
        if size_bytes < 1024:
            return "SMALL"
        elif size_bytes < 1400:
            return "MEDIUM"
        elif size_bytes < 4096:
            return "LARGE"
        elif size_bytes < 8192:
            return "VERY_LARGE"
        else:
            return "MEGA"
    
    def log_message_received(self, size_bytes: int, source_addr: str, message_type: str = "DOIP"):
        """Enhanced logging for received messages with size categorization"""
        size_display = self.format_message_size(size_bytes)
        category = self.get_message_size_category(size_bytes)
        
        # Different emoji and color based on size
        if category == "SMALL":
            emoji = "📄"
            color = Colors.WHITE
        elif category == "MEDIUM":
            emoji = "📋"
            color = Colors.YELLOW
        elif category == "LARGE":
            emoji = "📦"
            color = Colors.MAGENTA
        elif category == "VERY_LARGE":
            emoji = "📦🔥"
            color = Colors.RED
        else:  # MEGA
            emoji = "📦💥🚀"
            color = Colors.RED + Colors.BOLD + Colors.UNDERLINE
        
        print(f"{color}{emoji} {message_type} MESSAGE RECEIVED: {size_display} from {source_addr}{Colors.RESET}")
        
        # Update message statistics
        self.update_message_stats(size_bytes)
        
        # Additional info for large messages
        if category in ["LARGE", "VERY_LARGE", "MEGA"]:
            print(f"{color}   📊 Size Analysis: {size_bytes} bytes = {size_bytes/1024:.2f} KB = {size_bytes/1024/1024:.3f} MB{Colors.RESET}")
            if size_bytes > 1400:
                print(f"{color}   ⚠️  Note: Message exceeds typical TCP MSS (1460 bytes){Colors.RESET}")
            if size_bytes > 4096:
                print(f"{color}   🔥  Note: Very large message - chunking likely required{Colors.RESET}")
            if size_bytes > 8192:
                print(f"{color}   🚀  Note: Mega message - intensive processing required{Colors.RESET}")
    
    def receive_complete_doip_message(self, client_socket) -> Optional[bytes]:
        """Receive a complete DOIP message, handling TCP fragmentation"""
        try:
            # First, receive the 8-byte DOIP header
            header_data = bytearray()
            while len(header_data) < 8:
                chunk = client_socket.recv(8 - len(header_data))
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
                chunk = client_socket.recv(payload_length - len(payload_data))
                if not chunk:
                    return None
                payload_data.extend(chunk)
            
            complete_message = header_data + payload_data
            total_size = len(complete_message)
            
            # Use enhanced logging for message size
            self.log_message_received(total_size, "client", "DOIP")
            print(f"   📋 Message Details: Header (8 bytes) + Payload ({payload_length} bytes) = {total_size} bytes total")
            
            return bytes(complete_message)
            
        except Exception as e:
            print(f"Error receiving DOIP message: {e}")
            return None
    
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
        
        # Enhanced logging for diagnostic messages
        uds_data_size = len(uds_data)
        print(f"{Colors.CYAN}🔧 Diagnostic message: SA=0x{source_address:04x}, TA=0x{target_address:04x}{Colors.RESET}")
        
        # Log large diagnostic messages with enhanced details
        if uds_data_size > 1024:
            self.log_message_received(uds_data_size, f"SA=0x{source_address:04x}", "DIAGNOSTIC")
            print(f"   🔍 UDS Data: {uds_data[:32].hex()}... (showing first 32 bytes)")
        else:
            print(f"   🔍 UDS Data: {uds_data.hex()}")
        
        # Find the target ECU
        target_ecu = self.ecus.get(target_address)
        if not target_ecu:
            print(f"{Colors.RED}❌ No ECU found for target address 0x{target_address:04x}{Colors.RESET}")
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
        elif service_id == UDS_LARGE_MESSAGE_TEST:
            # Handle large message test service
            response_data = target_ecu.handle_large_message_test(uds_data[1:])
            
            if response_data:
                # Create positive response
                uds_response = struct.pack('B', service_id + UDS_POSITIVE_RESPONSE_MASK) + response_data
                payload = struct.pack('>HH', target_address, source_address) + uds_response
                header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
                
                print(f"ECU {target_ecu.ecu_type} sending large message test response: {len(response_data)} bytes")
                return header + payload
        
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
            print(f"{Colors.GREEN}📡 UDP discovery server listening on all interfaces:{DOIP_UDP_DISCOVERY_PORT}{Colors.RESET}")
        except OSError as e:
            if e.errno == 48:  # Address already in use
                print(f"{Colors.RED}❌ UDP Port {DOIP_UDP_DISCOVERY_PORT} is already in use!{Colors.RESET}")
                print(f"{Colors.YELLOW}💡 Try: lsof -i :{DOIP_UDP_DISCOVERY_PORT} to see what's using the port{Colors.RESET}")
                print(f"{Colors.YELLOW}💡 Or kill the existing process and try again{Colors.RESET}")
            else:
                print(f"{Colors.RED}❌ UDP server error: {e}{Colors.RESET}")
            return
            
        # Main UDP server loop - now properly placed outside exception handling
        try:
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
        """Handle individual TCP client connection with improved reliability"""
        print(f"{Colors.BLUE}🔗 TCP client connected from {addr}{Colors.RESET}")
        
        # Register the connection
        self.register_connection(client_socket, addr)
        
        try:
            while self.running:
                # Use new method to receive complete DOIP messages
                data = self.receive_complete_doip_message(client_socket)
                if not data:
                    print(f"{Colors.YELLOW}📭 No data received from {addr}, closing connection{Colors.RESET}")
                    break
                
                # Use enhanced logging for received message size
                self.log_message_received(len(data), str(addr), "TCP")
                
                header_info = self.parse_doip_header(data)
                if not header_info:
                    print(f"{Colors.RED}❌ TCP: Invalid header from {addr}{Colors.RESET}")
                    continue
                
                _, payload_type, payload_length = header_info
                payload_size_display = self.format_message_size(payload_length)
                print(f"{Colors.CYAN}📦 TCP: Payload type 0x{payload_type:04x}, payload {payload_size_display}{Colors.RESET}")
                
                try:
                    if payload_type == DOIP_ROUTING_ACTIVATION_REQUEST:
                        print(f"{Colors.GREEN}🔄 TCP: Handling routing activation request from {addr}{Colors.RESET}")
                        response = self.handle_routing_activation_request(data, addr)
                        client_socket.send(response)
                        print(f"{Colors.GREEN}✅ Routing activation successful for {addr}{Colors.RESET}")
                    elif payload_type == DOIP_DIAGNOSTIC_MESSAGE:
                        print(f"{Colors.GREEN}🔧 TCP: Handling diagnostic message from {addr}{Colors.RESET}")
                        response = self.handle_diagnostic_message(data)
                        client_socket.send(response)
                        print(f"{Colors.GREEN}✅ Diagnostic response sent to {addr}{Colors.RESET}")
                    elif payload_type == DOIP_ALIVE_CHECK_REQUEST:
                        print(f"{Colors.GREEN}💓 TCP: Handling alive check request from {addr}{Colors.RESET}")
                        response = self.handle_alive_check_request(data, addr)
                        client_socket.send(response)
                        print(f"{Colors.GREEN}✅ Alive check response sent to {addr}{Colors.RESET}")
                    elif payload_type == DOIP_ALIVE_CHECK_RESPONSE:
                        print(f"{Colors.GREEN}💓 TCP: Received alive check response from {addr}{Colors.RESET}")
                    else:
                        print(f"{Colors.YELLOW}⚠️ Unsupported payload type: 0x{payload_type:04x}{Colors.RESET}")
                        
                except Exception as e:
                    print(f"{Colors.RED}❌ Error processing message from {addr}: {e}{Colors.RESET}")
                    # Try to send error response
                    try:
                        error_response = self.create_negative_ack(0x02)  # Invalid payload length
                        client_socket.send(error_response)
                    except:
                        pass
                    
        except Exception as e:
            print(f"{Colors.RED}❌ TCP client error from {addr}: {e}{Colors.RESET}")
            self.unregister_connection(client_socket, addr, str(e))
        finally:
            client_socket.close()
            self.unregister_connection(client_socket, addr)
            print(f"{Colors.YELLOW}🔌 TCP client {addr} disconnected{Colors.RESET}")
    
    def tcp_server(self):
        """TCP diagnostic server thread with improved connection management"""
        self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp_socket.settimeout(1.0)
        
        try:
            self.tcp_socket.bind(('0.0.0.0', DOIP_TCP_DATA_PORT))
            self.tcp_socket.listen(10)  # Increased backlog for better connection handling
            print(f"{Colors.GREEN}🚀 TCP diagnostic server listening on all interfaces:{DOIP_TCP_DATA_PORT}{Colors.RESET}")
        except OSError as e:
            if e.errno == 48:  # Address already in use
                print(f"{Colors.RED}❌ TCP Port {DOIP_TCP_DATA_PORT} is already in use!{Colors.RESET}")
                print(f"{Colors.YELLOW}💡 Try: lsof -i :{DOIP_TCP_DATA_PORT} to see what's using the port{Colors.RESET}")
                print(f"{Colors.YELLOW}💡 Or kill the existing process and try again{Colors.RESET}")
            else:
                print(f"{Colors.RED}❌ TCP server error: {e}{Colors.RESET}")
            return
            
        # Main TCP server loop - now properly placed outside exception handling
        try:
            while self.running:
                try:
                    client_socket, addr = self.tcp_socket.accept()
                    
                    # Check if we can accept this connection
                    if not self.can_accept_connection():
                        print(f"{Colors.RED}⏸️ Connection rate limited or capacity full, rejecting {addr}{Colors.RESET}")
                        client_socket.close()
                        time.sleep(self.connection_retry_delay)
                        continue
                    
                    # Set socket options for better reliability
                    client_socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
                    
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
                        print(f"{Colors.RED}❌ TCP server error: {e}{Colors.RESET}")
                        
        finally:
            if self.tcp_socket:
                self.tcp_socket.close()
    
    def start(self):
        """Start the multi-ECU DOIP vehicle emulator with improved reliability"""
        print(f"{Colors.BOLD}{Colors.CYAN}🚗 Starting Multi-ECU DOIP Vehicle Emulator{Colors.RESET}")
        print(f"{Colors.CYAN}Base VIN: {self.base_vin}{Colors.RESET}")
        print(f"{Colors.CYAN}Configured ECUs:{Colors.RESET}")
        for logical_addr, ecu in self.ecus.items():
            print(f"  {Colors.GREEN}✅ {ecu.ecu_type}:{Colors.RESET} Logical Address 0x{logical_addr:04x}, VIN {ecu.vin}")
        
        print(f"")
        print(f"{Colors.CYAN}Supported DOIP Features:{Colors.RESET}")
        print(f"  {Colors.GREEN}✅{Colors.RESET} Multi-ECU vehicle identification")
        print(f"  {Colors.GREEN}✅{Colors.RESET} ECU-specific diagnostic routing")
        print(f"  {Colors.GREEN}✅{Colors.RESET} Concurrent client connections (max: {self.max_concurrent_connections})")
        print(f"  {Colors.GREEN}✅{Colors.RESET} Connection rate limiting ({self.connection_rate_limit*1000:.0f}ms min interval)")
        print(f"  {Colors.GREEN}✅{Colors.RESET} Dynamic data simulation per ECU type")
        print(f"  {Colors.GREEN}✅{Colors.RESET} Large message support (up to {self.max_message_size/1024:.0f}KB)")
        print(f"{Colors.YELLOW}Press Ctrl+C to stop{Colors.RESET}")
        
        # Check port availability before starting
        if not self.check_required_ports():
            print(f"\n{Colors.RED}❌ Cannot start emulator - required ports are not available{Colors.RESET}")
            return
        
        print(f"\n{Colors.GREEN}✅ All ports available - starting emulator...{Colors.RESET}")
        
        self.running = True
        
        # Start UDP and TCP servers
        udp_thread = threading.Thread(target=self.udp_server, daemon=True)
        tcp_thread = threading.Thread(target=self.tcp_server, daemon=True)
        
        udp_thread.start()
        tcp_thread.start()
        
        # Start statistics monitoring thread
        stats_thread = threading.Thread(target=self._monitor_stats, daemon=True)
        stats_thread.start()
        
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}🛑 Shutting down Multi-ECU DOIP vehicle emulator...{Colors.RESET}")
            self.stop()
    
    def _monitor_stats(self):
        """Monitor and display connection and message statistics periodically"""
        while self.running:
            time.sleep(30)  # Print stats every 30 seconds
            if self.running:
                self.print_connection_stats()
                self.print_message_stats()
    
    def stop(self):
        """Stop the multi-ECU vehicle emulator with final statistics"""
        print(f"{Colors.YELLOW}🛑 Stopping Multi-ECU DOIP vehicle emulator...{Colors.RESET}")
        self.running = False
        
        # Close all active connections
        with self.connection_lock:
            for conn in self.active_connections:
                try:
                    conn.close()
                except:
                    pass
            self.active_connections.clear()
        
        if self.udp_socket:
            self.udp_socket.close()
        if self.tcp_socket:
            self.tcp_socket.close()
        
        # Print final statistics
        print(f"\n{Colors.CYAN}📊 Final Statistics:{Colors.RESET}")
        self.print_connection_stats()
        self.print_message_stats()
        print(f"{Colors.GREEN}✅ Multi-ECU DOIP vehicle emulator stopped successfully{Colors.RESET}")

if __name__ == "__main__":
    # Allow base VIN to be specified as command line argument
    base_vin = sys.argv[1] if len(sys.argv) > 1 else "WBAVN31010AE1234"
    
    emulator = DOIPVehicleEmulator(base_vin=base_vin)
    emulator.start()