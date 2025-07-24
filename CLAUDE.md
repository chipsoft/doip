# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an embedded systems project for the SAME54 microcontroller featuring:
- LwIP TCP/IP stack implementation with socket API
- FreeRTOS real-time operating system
- Ethernet connectivity with GMAC controller
- Basic web server functionality
- Hardware abstraction layer (HAL) for peripherals

## Build System

The project has been modernized with a root-level Makefile for easier builds:

### Root Directory Build (Recommended)
```bash
make clean
make all
make size     # Check memory usage
make rebuild  # Clean and build
make help     # Show available targets
```

### Legacy Build (Still Available)
```bash
# GCC toolchain in subdirectory
cd gcc && make all

# ARM Compiler (ARMCC)
cd armcc && make all
```

Build outputs (in `build/` directory):
- `build/AtmelStart.elf` - Main executable
- `build/AtmelStart.bin` - Binary firmware image
- `build/AtmelStart.hex` - Intel HEX format
- `build/AtmelStart.map` - Memory map
- `build/AtmelStart.lss` - Assembly listing

## Architecture

### Core Components

**Hardware Layer (`hal/`, `hpl/`):**
- Hardware abstraction and peripheral drivers
- Low-level hardware configuration for SAME54

**Network Stack (`lwip/`):**
- LwIP 1.4.0 TCP/IP stack
- Socket API implementation in `lwip_socket_api.c`
- Ethernet MAC interface (`lwip/lwip-1.4.0/port/ethif_mac.c`)

**RTOS (`thirdparty/RTOS/freertos/`):**
- FreeRTOS v8.2.3 for task scheduling
- Configuration in `config/FreeRTOSConfig.h`

**Application Layer:**
- `main.c` - Entry point and basic socket demo
- `webserver_tasks.c` - Web server and LED tasks
- `ethernet_phy_main.c` - Ethernet PHY management

### Key Files

- `atmel_start.c:6-11` - System initialization sequence
- `main.c:64-77` - Main application entry with socket demo
- `config/FreeRTOSConfig.h` - RTOS configuration parameters
- `config/lwipopts.h` - Network stack configuration

### Memory Layout

The project uses heap_2.c memory management with 42KB heap (`configTOTAL_HEAP_SIZE`). Stack overflow checking is disabled in the current configuration.

## Development Notes

### Network Configuration
- The system demonstrates basic socket API usage with a web server
- DHCP support is available (check `config/lwipopts.h`)
- Ethernet PHY auto-negotiation is handled in `ethernet_phy/`

### RTOS Tasks
- LED blink task for status indication
- Web server task for HTTP requests
- Network stack runs in dedicated RTOS tasks

### Hardware Platform
- Target: SAME54P20A Cortex-M4F microcontroller
- Ethernet: GMAC with external PHY
- Debug: UART stdio redirection available

The codebase follows Microchip's ASF (Advanced Software Framework) conventions and is generated from Atmel Start configuration tool.

## Build Optimization

- When build project use as many cores as possible