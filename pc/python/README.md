# DOIP ECU Emulator

This Python script implements a complete DOIP (Diagnostics over Internet Protocol) ECU emulator according to ISO 13400 standard.

## Features

- **UDP Discovery Server**: Responds to vehicle identification requests on port 13400
- **TCP Diagnostic Server**: Handles diagnostic communication on port 13400
- **UDS Service Support**: Implements Read Data By Identifier (0x22) service
- **Configurable Vehicle Data**: Customizable VIN, ECU versions, and addressing
- **Multi-threaded Architecture**: Concurrent handling of multiple client connections
- **Protocol Compliance**: Full ISO 13400 DOIP message framing and routing

## Supported Data Identifiers (DIDs)

- **0xF190**: Vehicle Identification Number (VIN)
- **0xF1A0**: ECU Software Version
- **0xF1A1**: ECU Hardware Version

## Usage

### Basic Usage

```bash
python3 doip_ecu_emulator.py
```

### Custom VIN

```bash
python3 doip_ecu_emulator.py "WBAVN31010AE67890"
```

## Configuration

Default configuration in the script:

```python
VIN = "WBAVN31010AE12345"
ECU_ADDRESS = 0x1001
LOGICAL_ADDRESS = 0x0001
ECU_SW_VERSION = "SW_V1.2.3"
ECU_HW_VERSION = "HW_V2.0.1"
```

## Protocol Flow

1. **Discovery Phase (UDP)**:
   - Client broadcasts vehicle identification request
   - ECU responds with vehicle announcement containing VIN, addresses, and network info

2. **Connection Phase (TCP)**:
   - Client establishes TCP connection to ECU
   - Client sends routing activation request
   - ECU responds with activation confirmation

3. **Diagnostic Phase (TCP)**:
   - Client sends UDS diagnostic requests
   - ECU processes requests and sends responses
   - Supports positive and negative acknowledgments

## Network Requirements

- The emulator binds to all network interfaces (0.0.0.0)
- Ensure ports 13400 (UDP and TCP) are available
- Firewall may need configuration to allow incoming connections

## Testing with Firmware

The DOIP ECU emulator is designed to work with the embedded firmware DOIP client. Start the emulator before powering on the firmware to ensure proper discovery.

## Example Output

```
Starting DOIP ECU Emulator
VIN: WBAVN31010AE12345
ECU Address: 0x1001
Logical Address: 0x0001
Press Ctrl+C to stop

UDP discovery server listening on port 13400
TCP diagnostic server listening on port 13400
Received vehicle identification request from ('192.168.1.100', 54321)
Sending vehicle identification response: VIN=WBAVN31010AE12345, Logical Address=0x0001
TCP client connected from ('192.168.1.100', 54322)
Routing activation request from ('192.168.1.100', 54322), Source Address: 0x0E80
Routing activation successful for source 0x0E80
Diagnostic message: SA=0x0E80, TA=0x0001, Data=22f190
Sending positive response for DID 0xF190: WBAVN31010AE12345
```

## Troubleshooting

- **Connection refused**: Check if ports 13400 are already in use
- **No discovery response**: Verify network connectivity and firewall settings
- **Permission denied**: On some systems, binding to port 13400 may require root privileges

## Implementation Notes

This emulator implements a subset of the full DOIP specification, focusing on the essential functionality needed for automotive diagnostic communication. It's designed for testing and development purposes.