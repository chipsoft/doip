# DOIP Large Message Testing

This directory contains enhanced DOIP emulator scripts and test tools that support large messages up to 256KB.

## Enhanced Features

### Python Emulators
- **Large Message Support**: Handle messages up to 256KB (matching C driver capability)
- **TCP Fragmentation Handling**: Proper reassembly of fragmented TCP messages
- **Echo Testing**: New UDS service (0x3E) for large message round-trip testing
- **Memory Management**: Efficient buffer handling with bounds checking

### Files
- `doip_ecu_emulator.py` - Single ECU emulator with large message support
- `doip_multi_ecu_emulator.py` - Multi-ECU vehicle emulator with large message support
- `doip_large_message_test.py` - Test client for large message validation
- `README_LARGE_MESSAGES.md` - This documentation

## Usage Examples

### 1. Start Single ECU Emulator
```bash
python3 doip_ecu_emulator.py
# or with custom VIN
python3 doip_ecu_emulator.py WBAVN31010AE99999
```

### 2. Start Multi-ECU Vehicle Emulator
```bash
python3 doip_multi_ecu_emulator.py
# or with custom base VIN
python3 doip_multi_ecu_emulator.py WBAVN31010AE8888
```

The multi-ECU emulator creates 4 ECUs:
- ENGINE (0x0001)
- TRANSMISSION (0x0002) 
- ABS (0x0003)
- BCM (0x0004)

### 3. Run Large Message Tests

#### Test Single ECU:
```bash
python3 doip_large_message_test.py 192.168.100.1
```

#### Test Specific ECU in Multi-ECU Setup:
```bash
python3 doip_large_message_test.py 192.168.100.1 0x0001  # ENGINE
python3 doip_large_message_test.py 192.168.100.1 0x0002  # TRANSMISSION
python3 doip_large_message_test.py 192.168.100.1 0x0003  # ABS
python3 doip_large_message_test.py 192.168.100.1 0x0004  # BCM
```

## Test Suite Description

The test suite validates large message handling with:

### Test Sizes
- **1KB**: Basic functionality test
- **1.4KB**: Near-MTU boundary test  
- **1.6KB**: Above-MTU fragmentation test
- **4KB, 8KB, 16KB, 32KB, 64KB, 128KB**: Progressive size tests
- **256KB**: Maximum size test (optional)

### Test Process
1. **Connection**: TCP connection and DOIP routing activation
2. **Message Generation**: Creates test data with repeating pattern and signature
3. **Transmission**: Sends large UDS diagnostic message (service 0x3E)
4. **Echo Verification**: Validates that ECU echoes back exact data
5. **Performance Metrics**: Measures send/receive speeds

### Expected Output
```
=== DOIP Large Message Test Suite ===
Target ECU: 0x0001
Test sizes: ['1KB', '1KB', '1KB', '4KB', '8KB', '16KB', '32KB', '64KB', '128KB']
=============================================

Test 1/9: 1024 bytes (1KB)
------------------------------
Generated test data: 1024 bytes with signature: 00000400
Sending large message test: 1039 bytes total
Message sent in 0.001 seconds (1015.6 KB/s)
Response received in 0.002 seconds (2077.0 KB/s)
✓ Large message test PASSED - Data echoed correctly
✓ Test 1 PASSED

...

=============================================
Test Results: 9 passed, 0 failed
Success rate: 100.0%
```

## Technical Implementation

### Message Flow
```
Client → Large Message → TCP Fragmentation → 
Python Reassembly → UDS Processing → 
Large Response Generation → TCP Fragmentation → Client
```

### Key Enhancements
- **Header-First Parsing**: Read 8-byte DOIP header to determine payload size
- **Streaming Receive**: Handle partial TCP receives with buffering
- **Memory Bounds**: 256KB maximum message size limit
- **Pattern Verification**: Test data uses repeating pattern for integrity checking

## Integration with C Driver

This Python implementation is designed to test the C DOIP driver's large message capabilities:

1. **Compatible Protocol**: Full ISO 13400 compliance
2. **Size Matching**: Same 256KB limit as C implementation  
3. **Fragmentation Testing**: Validates TCP/IP stack fragmentation handling
4. **Performance Baseline**: Provides performance comparison data

## Troubleshooting

### Common Issues
- **Connection Refused**: Ensure emulator is running and listening on correct IP/port
- **Message Too Large**: Check 256KB limit in both client and emulator
- **Fragmentation Errors**: Verify TCP receive buffer handling
- **Memory Errors**: Monitor system memory usage during large transfers

### Debug Mode
Add debug prints by modifying the scripts or use Wireshark to capture DOIP traffic on port 13400.