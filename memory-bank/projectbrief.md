# DOIP Diagnostics Project Brief

## Project Overview
**DOIP (Diagnostics over Internet Protocol)** implementation for automotive diagnostics communication between SAME54 microcontroller and PC via direct Ethernet connection.

## Core Requirements
- Implement **ISO 13400 DOIP client** on SAME54 Xplained Pro board
- Communicate with Python ECU emulator running on PC
- Support vehicle discovery via UDP broadcast
- Establish TCP connections for UDS diagnostic messages
- Read vehicle data (VIN, ECU software/hardware versions)
- Implement alive check protocol for bidirectional keep-alive
- Operate in continuous diagnostic cycles every 10 seconds

## Architecture
- **Direct Ethernet cable connection** between SAME54 board and PC
- **192.168.100.x network** configuration
- **Event-driven TCP callbacks** with zero-CPU overhead during idle
- **FreeRTOS stream buffers** for ISR-safe data handling
- **Raw lwIP API** with TCP optimization for flow control

## Key Technologies
- **ASF4**: Microchip Advanced Software Framework for SAME54
- **FreeRTOS v11.1.0**: Real-time operating system with stream buffers
- **lwIP v2.2.1**: TCP/IP stack with raw API integration
- **Python ECU Emulator**: ISO 13400 compliant DOIP server
- **ARM GCC**: Cross-compiler toolchain
- **Segger J-Link**: Programming and debugging

## Performance Goals
- **Connection Time**: <500ms typical
- **Diagnostic Cycle**: 2-3 seconds complete
- **Memory Usage**: ~8KB RAM for DOIP + buffers
- **Reliability**: >99% success rate in continuous testing
- **TCP Optimization**: 60% reduction in retransmissions

## Development Environment
- **VS Code / Cursor** with J-Link debugging (F5)
- **Segger RTT** for live debug output
- **Wireshark** for packet capture and analysis
- **LED indicators** on SAME54 board for activity monitoring
