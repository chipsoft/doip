#!/usr/bin/env python3
"""
Realistic DOIP ECU Emulator
Implements ISO 13400 Diagnostics over Internet Protocol with realistic ECU behavior
Based on real automotive ECU patterns and diagnostic sequences
"""

import socket
import struct
import threading
import time
import sys
import random
from typing import Optional, Tuple, Dict
from datetime import datetime

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

# UDS Service IDs (ISO 14229)
UDS_DIAGNOSTIC_SESSION_CONTROL = 0x10
UDS_ECU_RESET = 0x11
UDS_SECURITY_ACCESS = 0x27
UDS_COMMUNICATION_CONTROL = 0x28
UDS_READ_DATA_BY_IDENTIFIER = 0x22
UDS_READ_MEMORY_BY_ADDRESS = 0x23
UDS_READ_SCALING_DATA_BY_IDENTIFIER = 0x24
UDS_READ_DATA_BY_IDENTIFIER_PERIODIC = 0x2A
UDS_DYNAMICALLY_DEFINE_DATA_IDENTIFIER = 0x2C
UDS_WRITE_DATA_BY_IDENTIFIER = 0x2E
UDS_ROUTINE_CONTROL = 0x31
UDS_REQUEST_DOWNLOAD = 0x34
UDS_REQUEST_UPLOAD = 0x35
UDS_TRANSFER_DATA = 0x36
UDS_REQUEST_TRANSFER_EXIT = 0x37
UDS_REQUEST_FILE_TRANSFER = 0x38
UDS_WRITE_MEMORY_BY_ADDRESS = 0x3D
UDS_TESTER_PRESENT = 0x3E
UDS_SECURITY_ACCESS_DATA_TRANSMISSION = 0x87
UDS_LINK_CONTROL = 0x87

# UDS Response Masks
UDS_POSITIVE_RESPONSE_MASK = 0x40
UDS_NEGATIVE_RESPONSE = 0x7F

# UDS Negative Response Codes
UDS_NRC_SERVICE_NOT_SUPPORTED = 0x11
UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED = 0x12
UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT = 0x13
UDS_NRC_CONDITIONS_NOT_CORRECT = 0x22
UDS_NRC_REQUEST_SEQUENCE_ERROR = 0x24
UDS_NRC_REQUEST_OUT_OF_RANGE = 0x31
UDS_NRC_SECURITY_ACCESS_DENIED = 0x33
UDS_NRC_INVALID_KEY = 0x35
UDS_NRC_EXCEEDED_NUMBER_OF_ATTEMPTS = 0x36
UDS_NRC_REQUIRED_TIME_DELAY_NOT_EXPIRED = 0x37
UDS_NRC_SECURITY_ACCESS_REQUIRED = 0x33
UDS_NRC_GENERAL_PROGRAMMING_FAILURE = 0x72
UDS_NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73
UDS_NRC_REQUEST_CORRECTLY_RECEIVED_RESPONSE_PENDING = 0x7F

# Data Identifiers (DIDs) - Real automotive DIDs
DID_VIN = 0xF190
DID_VEHICLE_MANUFACTURER_ECU_SOFTWARE_NUMBER = 0xF194
DID_SYSTEM_SUPPLIER_IDENTIFIER = 0xF195
DID_ECU_SOFTWARE_VERSION_NUMBER = 0xF196
DID_ECU_SOFTWARE_CALIBRATION_VERSION_NUMBER = 0xF197
DID_ECU_HARDWARE_VERSION_NUMBER = 0xF198
DID_ECU_HARDWARE_SERIAL_NUMBER = 0xF199
DID_SUPPORTED_DID_LIST = 0xFF00
DID_ECU_SERIAL_NUMBER = 0xF18C
DID_ECU_PART_NUMBER = 0xF1A0
DID_ECU_HARDWARE_VERSION = 0xF1A1
DID_ECU_SOFTWARE_VERSION = 0xF1A2
DID_ECU_CALIBRATION_VERSION = 0xF1A3
DID_ECU_MANUFACTURER_NAME = 0xF1A4
DID_ECU_MANUFACTURER_CODE = 0xF1A5
DID_ECU_MANUFACTURING_DATE = 0xF1A6
DID_ECU_MANUFACTURING_YEAR = 0xF1A7
DID_ECU_MANUFACTURING_MONTH = 0xF1A8
DID_ECU_MANUFACTURING_DAY = 0xF1A9
DID_ECU_MANUFACTURING_HOUR = 0xF1AA
DID_ECU_MANUFACTURING_MINUTE = 0xF1AB
DID_ECU_MANUFACTURING_SECOND = 0xF1AC
DID_ECU_MANUFACTURING_MILLISECOND = 0xF1AD
DID_ECU_MANUFACTURING_MICROSECOND = 0xF1AE
DID_ECU_MANUFACTURING_NANOSECOND = 0xF1AF

# Security Access Levels
SECURITY_ACCESS_LEVEL_1 = 0x01  # Basic diagnostic access
SECURITY_ACCESS_LEVEL_2 = 0x02  # Extended diagnostic access
SECURITY_ACCESS_LEVEL_3 = 0x03  # Programming access
SECURITY_ACCESS_LEVEL_4 = 0x04  # Extended programming access

class RealisticECUEmulator:
    def __init__(self, vin: str = "WBAVN31010AE12345", ecu_address: int = 0x1001):
        self.vin = vin
        self.ecu_address = ecu_address
        self.logical_address = 0x0001
        self.entity_id = b'\x00\x01\x02\x03\x04\x05'  # 6-byte entity ID
        self.group_id = b'\x00\x01'  # 2-byte group ID
        
        # Realistic ECU information
        self.ecu_sw_version = "SW_V2.1.4_Build_20231215"
        self.ecu_hw_version = "HW_V3.2.1_Rev_B"
        self.ecu_calibration_version = "CAL_V1.8.3_20231120"
        self.ecu_serial_number = "ECU202312150001"
        self.ecu_part_number = "ECU-2023-001-A"
        self.ecu_manufacturer = "BMW_AG"
        self.ecu_manufacturer_code = "BMW"
        
        # Manufacturing date (realistic)
        self.manufacturing_date = {
            'year': 2023,
            'month': 12,
            'day': 15,
            'hour': 14,
            'minute': 30,
            'second': 45
        }
        
        # ECU state
        self.security_level = 0  # 0 = locked, 1-4 = unlocked levels
        self.session_type = 1    # 1 = default, 2 = programming, 3 = extended
        self.communication_control = 0  # 0 = normal, 1 = disable, 2 = enable
        self.tester_present_counter = 0
        self.last_tester_present = 0
        
        # Security access keys (realistic)
        self.security_keys = {
            0x01: 0x1234,  # Level 1 key
            0x02: 0x5678,  # Level 2 key
            0x03: 0x9ABC,  # Level 3 key
            0x04: 0xDEF0   # Level 4 key
        }
        
        # Supported DIDs
        self.supported_dids = [
            DID_VIN, DID_VEHICLE_MANUFACTURER_ECU_SOFTWARE_NUMBER,
            DID_SYSTEM_SUPPLIER_IDENTIFIER, DID_ECU_SOFTWARE_VERSION_NUMBER,
            DID_ECU_SOFTWARE_CALIBRATION_VERSION_NUMBER, DID_ECU_HARDWARE_VERSION_NUMBER,
            DID_ECU_HARDWARE_SERIAL_NUMBER, DID_ECU_SERIAL_NUMBER,
            DID_ECU_PART_NUMBER, DID_ECU_HARDWARE_VERSION, DID_ECU_SOFTWARE_VERSION,
            DID_ECU_CALIBRATION_VERSION, DID_ECU_MANUFACTURER_NAME,
            DID_ECU_MANUFACTURER_CODE, DID_ECU_MANUFACTURING_DATE,
            DID_ECU_MANUFACTURING_YEAR, DID_ECU_MANUFACTURING_MONTH,
            DID_ECU_MANUFACTURING_DAY, DID_ECU_MANUFACTURING_HOUR,
            DID_ECU_MANUFACTURING_MINUTE, DID_ECU_MANUFACTURING_SECOND
        ]
        
        # Network
        self.udp_socket = None
        self.tcp_socket = None
        self.running = False
        self.active_connections = []
        
        # Timing simulation (realistic ECU response times)
        self.min_response_time = 0.005  # 5ms minimum
        self.max_response_time = 0.050  # 50ms maximum
        
        print(f"🚗 Realistic ECU Emulator Initialized")
        print(f"   VIN: {self.vin}")
        print(f"   ECU Address: 0x{self.ecu_address:04x}")
        print(f"   Software Version: {self.ecu_sw_version}")
        print(f"   Hardware Version: {self.ecu_hw_version}")
        print(f"   Manufacturing Date: {self.manufacturing_date['year']}-{self.manufacturing_date['month']:02d}-{self.manufacturing_date['day']:02d}")

    def simulate_ecu_processing_time(self):
        """Simulate realistic ECU processing time"""
        processing_time = random.uniform(self.min_response_time, self.max_response_time)
        time.sleep(processing_time)

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
            print(f"❌ Invalid DOIP version: {version:02x}, {inv_version:02x}")
            return None
            
        return version, payload_type, payload_length

    def handle_vehicle_identification_request(self, addr) -> bytes:
        """Handle UDP vehicle identification request with realistic timing"""
        print(f"🔍 Vehicle identification request from {addr}")
        
        # Simulate ECU processing time
        self.simulate_ecu_processing_time()
        
        # Vehicle announcement payload according to ISO 13400
        vin_bytes = self.vin.encode('ascii')[:17].ljust(17, b'\x00')
        
        payload = (vin_bytes + 
                  struct.pack('>H', self.logical_address) +
                  self.entity_id +
                  self.group_id +
                  b'\x00' +  # No further action required
                  b'\x00')   # VIN/GID sync status (0 = synchronized)
        
        header = self.create_doip_header(DOIP_VEHICLE_IDENTIFICATION_RESPONSE, len(payload))
        response = header + payload
        
        print(f"✅ Vehicle identification response sent: VIN={self.vin}, Logical Address=0x{self.logical_address:04x}")
        return response

    def handle_routing_activation_request(self, data: bytes, addr) -> bytes:
        """Handle TCP routing activation request with realistic validation"""
        print(f"🔗 Routing activation request from {addr}")
        
        # Simulate ECU processing time
        self.simulate_ecu_processing_time()
        
        if len(data) < 12:  # Header (8) + Source Address (2) + Activation Type (1) + Reserved (1)
            print(f"❌ Invalid routing activation request length")
            return self.create_negative_ack(0x02)
        
        source_address = struct.unpack('>H', data[8:10])[0]
        activation_type = data[10]
        
        print(f"   Source Address: 0x{source_address:04x}")
        print(f"   Activation Type: 0x{activation_type:02x}")
        
        # Validate source address (realistic ECU behavior)
        if source_address == 0x0000 or source_address == 0xFFFF:
            print(f"❌ Invalid source address: 0x{source_address:04x}")
            return self.create_negative_ack(0x02)
        
        # Create routing activation response
        # Payload: Client Source Address (2) + Logical Address (2) + Response Code (1) + Reserved (1)
        payload = struct.pack('>HHBB', 
                            source_address,
                            self.logical_address,
                            0x10,  # Routing activation successful
                            0x00)  # Reserved
        
        header = self.create_doip_header(DOIP_ROUTING_ACTIVATION_RESPONSE, len(payload))
        response = header + payload
        
        print(f"✅ Routing activation successful for source 0x{source_address:04x}")
        return response

    def handle_diagnostic_message(self, data: bytes) -> bytes:
        """Handle UDS diagnostic message with realistic ECU behavior"""
        if len(data) < 12:  # Header (8) + SA (2) + TA (2) minimum
            return self.create_negative_ack(0x02)
        
        source_address = struct.unpack('>H', data[8:10])[0]
        target_address = struct.unpack('>H', data[10:12])[0]
        uds_data = data[12:]
        
        print(f"🔧 Diagnostic message: SA=0x{source_address:04x}, TA=0x{target_address:04x}, Data={uds_data.hex()}")
        
        if len(uds_data) == 0:
            return self.create_negative_ack(0x03)
        
        service_id = uds_data[0]
        
        # Simulate ECU processing time
        self.simulate_ecu_processing_time()
        
        # Handle different UDS services
        if service_id == UDS_DIAGNOSTIC_SESSION_CONTROL:
            return self.handle_diagnostic_session_control(uds_data, source_address, target_address)
        elif service_id == UDS_SECURITY_ACCESS:
            return self.handle_security_access(uds_data, source_address, target_address)
        elif service_id == UDS_READ_DATA_BY_IDENTIFIER:
            return self.handle_read_data_by_identifier(uds_data, source_address, target_address)
        elif service_id == UDS_TESTER_PRESENT:
            return self.handle_tester_present(uds_data, source_address, target_address)
        elif service_id == UDS_ECU_RESET:
            return self.handle_ecu_reset(uds_data, source_address, target_address)
        elif service_id == UDS_COMMUNICATION_CONTROL:
            return self.handle_communication_control(uds_data, source_address, target_address)
        else:
            print(f"❌ Unsupported service 0x{service_id:02x}")
            return self.create_uds_negative_response(service_id, UDS_NRC_SERVICE_NOT_SUPPORTED, source_address, target_address)

    def handle_diagnostic_session_control(self, uds_data: bytes, source_address: int, target_address: int) -> bytes:
        """Handle diagnostic session control"""
        if len(uds_data) < 2:
            return self.create_uds_negative_response(UDS_DIAGNOSTIC_SESSION_CONTROL, 
                                                   UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT,
                                                   source_address, target_address)
        
        session_type = uds_data[1]
        print(f"   Session control request: type 0x{session_type:02x}")
        
        # Validate session type
        if session_type not in [0x01, 0x02, 0x03]:  # Default, Programming, Extended
            return self.create_uds_negative_response(UDS_DIAGNOSTIC_SESSION_CONTROL,
                                                   UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED,
                                                   source_address, target_address)
        
        # Check security access for programming session
        if session_type == 0x02 and self.security_level < 2:
            return self.create_uds_negative_response(UDS_DIAGNOSTIC_SESSION_CONTROL,
                                                   UDS_NRC_SECURITY_ACCESS_REQUIRED,
                                                   source_address, target_address)
        
        self.session_type = session_type
        
        # Positive response: Service ID + Session Type + P2Server_max + P2*Server_max
        response_data = struct.pack('>BBHH', 
                                  UDS_DIAGNOSTIC_SESSION_CONTROL + UDS_POSITIVE_RESPONSE_MASK,
                                  session_type,
                                  0x0100,  # P2Server_max (1 second)
                                  0x0100)  # P2*Server_max (1 second)
        
        payload = struct.pack('>HH', target_address, source_address) + response_data
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
        
        print(f"✅ Session control successful: type 0x{session_type:02x}")
        return header + payload

    def handle_security_access(self, uds_data: bytes, source_address: int, target_address: int) -> bytes:
        """Handle security access with realistic key exchange"""
        if len(uds_data) < 2:
            return self.create_uds_negative_response(UDS_SECURITY_ACCESS,
                                                   UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT,
                                                   source_address, target_address)
        
        sub_function = uds_data[1]
        print(f"   Security access request: sub-function 0x{sub_function:02x}")
        
        # Check if it's a request (odd number) or response (even number)
        if sub_function % 2 == 1:  # Request for seed
            level = (sub_function + 1) // 2
            if level not in self.security_keys:
                return self.create_uds_negative_response(UDS_SECURITY_ACCESS,
                                                       UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED,
                                                       source_address, target_address)
            
            # Generate seed (realistic ECU behavior)
            seed = random.randint(0x1000, 0xFFFF)
            
            # Positive response: Service ID + Sub-function + Seed
            response_data = struct.pack('>BH', 
                                      UDS_SECURITY_ACCESS + UDS_POSITIVE_RESPONSE_MASK,
                                      sub_function) + struct.pack('>H', seed)
            
            payload = struct.pack('>HH', target_address, source_address) + response_data
            header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
            
            print(f"✅ Security seed sent: level {level}, seed 0x{seed:04x}")
            return header + payload
            
        else:  # Response with key
            level = sub_function // 2
            if len(uds_data) < 4:
                return self.create_uds_negative_response(UDS_SECURITY_ACCESS,
                                                       UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT,
                                                       source_address, target_address)
            
            key = struct.unpack('>H', uds_data[2:4])[0]
            expected_key = self.security_keys.get(level, 0)
            
            if key == expected_key:
                self.security_level = level
                print(f"✅ Security access granted: level {level}")
                
                # Positive response: Service ID + Sub-function
                response_data = struct.pack('>BH', 
                                          UDS_SECURITY_ACCESS + UDS_POSITIVE_RESPONSE_MASK,
                                          sub_function)
                
                payload = struct.pack('>HH', target_address, source_address) + response_data
                header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
                return header + payload
            else:
                print(f"❌ Security access denied: invalid key 0x{key:04x}")
                return self.create_uds_negative_response(UDS_SECURITY_ACCESS,
                                                       UDS_NRC_INVALID_KEY,
                                                       source_address, target_address)

    def handle_read_data_by_identifier(self, uds_data: bytes, source_address: int, target_address: int) -> bytes:
        """Handle read data by identifier with comprehensive DID support"""
        if len(uds_data) < 3:
            return self.create_uds_negative_response(UDS_READ_DATA_BY_IDENTIFIER,
                                                   UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT,
                                                   source_address, target_address)
        
        did = struct.unpack('>H', uds_data[1:3])[0]
        print(f"   Read Data By Identifier: DID 0x{did:04x}")
        
        # Check if DID is supported
        if did not in self.supported_dids:
            return self.create_uds_negative_response(UDS_READ_DATA_BY_IDENTIFIER,
                                                   UDS_NRC_REQUEST_OUT_OF_RANGE,
                                                   source_address, target_address)
        
        # Get data for the requested DID
        response_data = self.get_did_data(did)
        if response_data is None:
            return self.create_uds_negative_response(UDS_READ_DATA_BY_IDENTIFIER,
                                                   UDS_NRC_CONDITIONS_NOT_CORRECT,
                                                   source_address, target_address)
        
        # Positive response: Service ID + DID + Data
        uds_response = struct.pack('>BH', 
                                 UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK,
                                 did) + response_data
        
        payload = struct.pack('>HH', target_address, source_address) + uds_response
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
        
        print(f"✅ Read Data response: DID 0x{did:04x}, data length {len(response_data)}")
        return header + payload

    def get_did_data(self, did: int) -> Optional[bytes]:
        """Get data for a specific DID"""
        if did == DID_VIN:
            return self.vin.encode('ascii')
        elif did == DID_ECU_SOFTWARE_VERSION_NUMBER:
            return self.ecu_sw_version.encode('ascii')
        elif did == DID_ECU_HARDWARE_VERSION_NUMBER:
            return self.ecu_hw_version.encode('ascii')
        elif did == DID_ECU_SOFTWARE_CALIBRATION_VERSION_NUMBER:
            return self.ecu_calibration_version.encode('ascii')
        elif did == DID_ECU_HARDWARE_SERIAL_NUMBER:
            return self.ecu_serial_number.encode('ascii')
        elif did == DID_ECU_PART_NUMBER:
            return self.ecu_part_number.encode('ascii')
        elif did == DID_ECU_MANUFACTURER_NAME:
            return self.ecu_manufacturer.encode('ascii')
        elif did == DID_ECU_MANUFACTURER_CODE:
            return self.ecu_manufacturer_code.encode('ascii')
        elif did == DID_ECU_MANUFACTURING_DATE:
            return struct.pack('>HBB', 
                             self.manufacturing_date['year'],
                             self.manufacturing_date['month'],
                             self.manufacturing_date['day'])
        elif did == DID_ECU_MANUFACTURING_YEAR:
            return struct.pack('>H', self.manufacturing_date['year'])
        elif did == DID_ECU_MANUFACTURING_MONTH:
            return struct.pack('>B', self.manufacturing_date['month'])
        elif did == DID_ECU_MANUFACTURING_DAY:
            return struct.pack('>B', self.manufacturing_date['day'])
        elif did == DID_ECU_MANUFACTURING_HOUR:
            return struct.pack('>B', self.manufacturing_date['hour'])
        elif did == DID_ECU_MANUFACTURING_MINUTE:
            return struct.pack('>B', self.manufacturing_date['minute'])
        elif did == DID_ECU_MANUFACTURING_SECOND:
            return struct.pack('>B', self.manufacturing_date['second'])
        elif did == DID_SUPPORTED_DID_LIST:
            # Return list of supported DIDs
            did_list = b''
            for supported_did in self.supported_dids:
                did_list += struct.pack('>H', supported_did)
            return did_list
        else:
            return None

    def handle_tester_present(self, uds_data: bytes, source_address: int, target_address: int) -> bytes:
        """Handle tester present with realistic timing"""
        current_time = time.time()
        
        # Check if tester present is required (every 2 seconds)
        if current_time - self.last_tester_present > 2.0:
            print(f"⚠️  Tester present timeout - session may be lost")
        
        self.last_tester_present = current_time
        self.tester_present_counter += 1
        
        # Positive response: Service ID + Sub-function
        response_data = struct.pack('>BB', 
                                  UDS_TESTER_PRESENT + UDS_POSITIVE_RESPONSE_MASK,
                                  0x00)  # Sub-function (suppress positive response)
        
        payload = struct.pack('>HH', target_address, source_address) + response_data
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
        
        if self.tester_present_counter % 10 == 0:  # Log every 10th request
            print(f"✅ Tester present: counter {self.tester_present_counter}")
        
        return header + payload

    def handle_ecu_reset(self, uds_data: bytes, source_address: int, target_address: int) -> bytes:
        """Handle ECU reset with security validation"""
        if len(uds_data) < 2:
            return self.create_uds_negative_response(UDS_ECU_RESET,
                                                   UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT,
                                                   source_address, target_address)
        
        reset_type = uds_data[1]
        print(f"   ECU reset request: type 0x{reset_type:02x}")
        
        # Check security access for hard reset
        if reset_type in [0x01, 0x02] and self.security_level < 2:  # Hard reset types
            return self.create_uds_negative_response(UDS_ECU_RESET,
                                                   UDS_NRC_SECURITY_ACCESS_REQUIRED,
                                                   source_address, target_address)
        
        # Positive response: Service ID + Reset Type
        response_data = struct.pack('>BB', 
                                  UDS_ECU_RESET + UDS_POSITIVE_RESPONSE_MASK,
                                  reset_type)
        
        payload = struct.pack('>HH', target_address, source_address) + response_data
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
        
        print(f"✅ ECU reset successful: type 0x{reset_type:02x}")
        
        # Simulate reset behavior
        if reset_type in [0x01, 0x02]:  # Hard reset
            self.security_level = 0
            self.session_type = 1
            print(f"🔄 ECU state reset to default")
        
        return header + payload

    def handle_communication_control(self, uds_data: bytes, source_address: int, target_address: int) -> bytes:
        """Handle communication control"""
        if len(uds_data) < 3:
            return self.create_uds_negative_response(UDS_COMMUNICATION_CONTROL,
                                                   UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT,
                                                   source_address, target_address)
        
        control_type = uds_data[1]
        comm_type = uds_data[2]
        
        print(f"   Communication control: type 0x{control_type:02x}, comm 0x{comm_type:02x}")
        
        # Check security access
        if self.security_level < 2:
            return self.create_uds_negative_response(UDS_COMMUNICATION_CONTROL,
                                                   UDS_NRC_SECURITY_ACCESS_REQUIRED,
                                                   source_address, target_address)
        
        self.communication_control = control_type
        
        # Positive response: Service ID + Control Type + Comm Type
        response_data = struct.pack('>BBB', 
                                  UDS_COMMUNICATION_CONTROL + UDS_POSITIVE_RESPONSE_MASK,
                                  control_type,
                                  comm_type)
        
        payload = struct.pack('>HH', target_address, source_address) + response_data
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
        
        print(f"✅ Communication control successful: type 0x{control_type:02x}")
        return header + payload

    def create_uds_negative_response(self, service_id: int, nrc: int, source_address: int, target_address: int) -> bytes:
        """Create UDS negative response"""
        # Negative response: Service ID (0x7F) + Requested Service ID + NRC
        response_data = struct.pack('>BBB', 
                                  UDS_NEGATIVE_RESPONSE,
                                  service_id,
                                  nrc)
        
        payload = struct.pack('>HH', target_address, source_address) + response_data
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE, len(payload))
        
        nrc_names = {
            UDS_NRC_SERVICE_NOT_SUPPORTED: "Service Not Supported",
            UDS_NRC_SUB_FUNCTION_NOT_SUPPORTED: "Sub-function Not Supported",
            UDS_NRC_INCORRECT_MESSAGE_LENGTH_OR_INVALID_FORMAT: "Incorrect Message Length",
            UDS_NRC_CONDITIONS_NOT_CORRECT: "Conditions Not Correct",
            UDS_NRC_SECURITY_ACCESS_REQUIRED: "Security Access Required",
            UDS_NRC_INVALID_KEY: "Invalid Key",
            UDS_NRC_REQUEST_OUT_OF_RANGE: "Request Out Of Range"
        }
        
        nrc_name = nrc_names.get(nrc, f"Unknown NRC 0x{nrc:02x}")
        print(f"❌ Negative response: Service 0x{service_id:02x}, NRC: {nrc_name}")
        
        return header + payload

    def create_negative_ack(self, nack_code: int) -> bytes:
        """Create DOIP negative acknowledgment"""
        payload = struct.pack('>B', nack_code)
        header = self.create_doip_header(DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK, len(payload))
        return header + payload

    def get_doip_interface_ip(self):
        """Get the appropriate network interface IP for DOIP"""
        try:
            import netifaces
            for interface in netifaces.interfaces():
                addrs = netifaces.ifaddresses(interface)
                if netifaces.AF_INET in addrs:
                    for addr_info in addrs[netifaces.AF_INET]:
                        ip = addr_info['addr']
                        if ip.startswith('192.168.100.'):
                            return ip
        except ImportError:
            pass
        
        # Fallback to default
        return '192.168.100.1'

    def udp_server(self):
        """UDP discovery server thread"""
        self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            self.udp_socket.bind(('0.0.0.0', DOIP_UDP_DISCOVERY_PORT))
            print(f"📡 UDP discovery server listening on all interfaces:{DOIP_UDP_DISCOVERY_PORT}")
            
            while self.running:
                try:
                    data, addr = self.udp_socket.recvfrom(1024)
                    
                    print(f"📨 UDP received {len(data)} bytes from {addr}: {data.hex()}")
                    
                    header_info = self.parse_doip_header(data)
                    if header_info:
                        _, payload_type, payload_length = header_info
                        print(f"   Parsed header: type 0x{payload_type:04x}, length {payload_length}")
                        
                        if payload_type == DOIP_VEHICLE_IDENTIFICATION_REQUEST:
                            print("🚗 Sending vehicle identification response...")
                            response = self.handle_vehicle_identification_request(addr)
                            self.udp_socket.sendto(response, addr)
                            print("✅ Response sent!")
                        else:
                            print(f"❌ Unexpected payload type: 0x{payload_type:04x}")
                    else:
                        print(f"❌ Invalid DOIP header from {addr}")
                        
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"❌ UDP server error: {e}")
                        
        finally:
            if self.udp_socket:
                self.udp_socket.close()

    def handle_tcp_client(self, client_socket, addr):
        """Handle individual TCP client connection with realistic behavior"""
        print(f"🔗 TCP client connected from {addr}")
        
        try:
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                
                print(f"📨 TCP received {len(data)} bytes from {addr}: {data.hex()}")
                
                header_info = self.parse_doip_header(data)
                if not header_info:
                    print(f"❌ TCP: Invalid header from {addr}")
                    continue
                
                _, payload_type, payload_length = header_info
                print(f"   TCP: Payload type 0x{payload_type:04x}, length {payload_length}")
                
                if payload_type == DOIP_ROUTING_ACTIVATION_REQUEST:
                    print(f"🔗 TCP: Handling routing activation request from {addr}")
                    response = self.handle_routing_activation_request(data, addr)
                    print(f"📤 TCP: Sending routing activation response ({len(response)} bytes)")
                    client_socket.send(response)
                elif payload_type == DOIP_DIAGNOSTIC_MESSAGE:
                    print(f"🔧 TCP: Handling diagnostic message from {addr}")
                    response = self.handle_diagnostic_message(data)
                    print(f"📤 TCP: Sending diagnostic response ({len(response)} bytes)")
                    client_socket.send(response)
                else:
                    print(f"❌ Unsupported payload type: 0x{payload_type:04x}")
                    
        except Exception as e:
            print(f"❌ TCP client error: {e}")
        finally:
            client_socket.close()
            if client_socket in self.active_connections:
                self.active_connections.remove(client_socket)
            print(f"🔌 TCP client {addr} disconnected")

    def tcp_server(self):
        """TCP diagnostic server thread"""
        self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.tcp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            self.tcp_socket.bind(('0.0.0.0', DOIP_TCP_DATA_PORT))
            self.tcp_socket.listen(5)
            print(f"🔧 TCP diagnostic server listening on all interfaces:{DOIP_TCP_DATA_PORT}")
            
            while self.running:
                try:
                    client_socket, addr = self.tcp_socket.accept()
                    self.active_connections.append(client_socket)
                    
                    # Start client handler in separate thread
                    client_thread = threading.Thread(target=self.handle_tcp_client, 
                                                   args=(client_socket, addr))
                    client_thread.daemon = True
                    client_thread.start()
                    
                except Exception as e:
                    if self.running:
                        print(f"❌ TCP server error: {e}")
                        
        finally:
            if self.tcp_socket:
                self.tcp_socket.close()

    def start(self):
        """Start the ECU emulator"""
        self.running = True
        
        # Start UDP server thread
        udp_thread = threading.Thread(target=self.udp_server)
        udp_thread.daemon = True
        udp_thread.start()
        
        # Start TCP server thread
        tcp_thread = threading.Thread(target=self.tcp_server)
        tcp_thread.daemon = True
        tcp_thread.start()
        
        print(f"🚗 Realistic ECU Emulator started")
        print(f"   Press Ctrl+C to stop")
        print(f"   Security Level: {self.security_level}")
        print(f"   Session Type: {self.session_type}")
        print(f"   Communication Control: {self.communication_control}")
        print()

    def stop(self):
        """Stop the ECU emulator"""
        self.running = False
        
        # Close all active connections
        for conn in self.active_connections:
            try:
                conn.close()
            except:
                pass
        self.active_connections.clear()
        
        # Close sockets
        if self.udp_socket:
            self.udp_socket.close()
        if self.tcp_socket:
            self.tcp_socket.close()
        
        print(f"🛑 Realistic ECU Emulator stopped")

def main():
    """Main function"""
    vin = "WBAVN31010AE12345"
    if len(sys.argv) > 1:
        vin = sys.argv[1]
    
    emulator = RealisticECUEmulator(vin)
    
    try:
        emulator.start()
        
        # Keep main thread alive
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print(f"\n🛑 Shutting down...")
        emulator.stop()

if __name__ == "__main__":
    main() 