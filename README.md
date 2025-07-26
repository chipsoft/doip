# SAME54 Embedded Ethernet Project

This project implements a TCP/IP networking solution on the SAME54 microcontroller with LwIP stack, FreeRTOS, and a built-in web server.

## 🔧 Build & Flash

### Root Directory Build (Recommended)
```bash
# Clean and build from project root
make clean
make all

# Check firmware size
make size

# Rebuild everything
make rebuild

# Show available targets
make help
```

### Legacy Build (Still Available)
```bash
# GCC toolchain in subdirectory
cd gcc
make clean
make all

# ARM Compiler (ARMCC)
cd armcc
make clean
make all
```

**Build Outputs (in `build/` directory):**
- `build/AtmelStart.elf` - Main executable
- `build/AtmelStart.bin` - Binary firmware for flashing
- `build/AtmelStart.hex` - Intel HEX format
- `build/AtmelStart.map` - Memory map file
- `build/AtmelStart.lss` - Assembly listing

## ⚙️ Network Configuration

### DHCP vs Static IP

**Current Setting:** DHCP is **ENABLED** by default

#### To Switch to Static IP:
1. **Open:** `config/lwip_macif_config.h`
2. **Change line 14:**
   ```c
   #define CONF_TCPIP_STACK_INTERFACE_0_DHCP 0  // Disable DHCP
   ```
3. **Enable static IP (line 81):**
   ```c
   #define CONF_TCPIP_STACK_INTERFACE_0_STATIC_IP 1
   ```

#### To Configure Static IP Address:
**File:** `config/lwip_macif_config.h`

```c
// Lines 92-106: Static IP Configuration
#define CONF_TCPIP_STACK_INTERFACE_0_IP "192.168.1.100"        // Your IP
#define CONF_TCPIP_STACK_INTERFACE_0_NETMASK "255.255.255.0"   // Subnet mask  
#define CONF_TCPIP_STACK_INTERFACE_0_GATEWAY "192.168.1.1"     // Gateway IP
```

### Other Network Settings

**File:** `config/lwip_macif_config.h`
- **Hostname:** Line 65 - `#define CONF_TCPIP_STACK_INTERFACE_0_HOSTNAME "lwip"`
- **MTU Size:** Line 58 - `#define CONF_TCPIP_STACK_INTERFACE_0_MTU 1536`

**File:** `config/lwipopts.h`
- **Enable DHCP:** Line 12 - `#define LWIP_DHCP 1`
- **TCP/UDP/ICMP:** Lines 37-50 enable network protocols
- **Memory Settings:** Line 32 - `#define MEM_SIZE 14336`

## 🌐 Web Server Configuration

### HTML Page Content

The internal web server serves a simple HTML page defined in:

**File:** `lwip_socket_api.c` (lines 47-52)

```c
const static char socket_webpage[] = "<html> \
<head><title>Basic webpage</title></head> \
<body> \
Welcome to your basic webpage using Socket API. \
</body> \
</html>";
```

**To Change HTML Content:**
1. Edit the `socket_webpage[]` string in `lwip_socket_api.c`
2. Use `\` for line continuation
3. Rebuild the project

### Web Server Port

**File:** `lwip_socket_api.h` (line 35)
```c
#define HTTP_PORT 80  // Standard HTTP port
```

### Adding New Web Pages

To serve different content based on URL path:
1. Modify the request parsing in `lwip_socket_api.c` (line 124)
2. Add URL path checking after `strncmp(buffer, "GET", 3)`
3. Create additional HTML content strings

Example:
```c
if (!strncmp(buffer, "GET /status", 11)) {
    // Serve status page
} else if (!strncmp(buffer, "GET /", 5)) {
    // Serve main page
}
```

## 📡 FreeRTOS Configuration

**File:** `config/FreeRTOSConfig.h`

**Key Settings:**
- **Heap Size:** Line 49 - `#define configTOTAL_HEAP_SIZE ((size_t)(42000))`
- **Max Tasks:** Line 31 - `#define configMAX_PRIORITIES ((uint32_t)5)`
- **Tick Rate:** Line 170 - `#define configTICK_RATE_HZ ((TickType_t)1000)`
- **Stack Size:** Line 39 - `#define configMINIMAL_STACK_SIZE ((uint16_t)64)`

**Task Stack Sizes (webserver_tasks.h):**
- LED Task: Line 40 - `#define TASK_LED_STACK_SIZE (512 / sizeof(portSTACK_TYPE))`
- Ethernet Task: Line 43 - `#define TASK_ETHERNETBASIC_STACK_SIZE (1024 / sizeof(portSTACK_TYPE))`

## 🔍 Debug & Monitoring

### Debug Output
**File:** `config/lwipopts.h` (lines 746-753)
```c
#define DHCP_DEBUG LWIP_DBG_OFF     // Enable with LWIP_DBG_ON
#define TCP_DEBUG LWIP_DBG_OFF      // TCP debug messages
#define UDP_DEBUG LWIP_DBG_OFF      // UDP debug messages
```

### LED Status Indicator
- **File:** `webserver_tasks.c`
- **Blink Rate:** Line 57 `#define BLINK_NORMAL 500` (ms)
- LED indicates system status and network activity

### Serial Output
The system uses UART for debug output via `stdio_redirect/` functionality.

## 📁 Important Files Summary

| File | Purpose | Key Settings |
|------|---------|--------------|
| `config/lwip_macif_config.h` | Network interface config | DHCP, Static IP, Hostname |
| `config/lwipopts.h` | LwIP stack config | Protocols, memory, debug |
| `config/FreeRTOSConfig.h` | RTOS configuration | Tasks, memory, timing |
| `lwip_socket_api.c` | Web server & HTML content | HTTP responses, socket handling |
| `lwip_socket_api.h` | Socket API definitions | HTTP port definition |
| `webserver_tasks.c` | Task management | RTOS tasks for networking |
| `main.c` | Application entry point | System initialization |

## 🚀 Getting Started

1. **Configure Network:** Edit `config/lwip_macif_config.h` for your network
2. **Customize Web Page:** Modify `socket_webpage[]` in `lwip_socket_api.c`  
3. **Build:** Run `make all` from project root
4. **Flash:** Program `build/AtmelStart.bin` to your SAME54 device
5. **Connect:** Access web server at device IP address (check DHCP or use static IP)

## 🚗 DOIP Automotive Diagnostics

This project includes a complete DOIP (Diagnostics over Internet Protocol) implementation supporting ISO 13400 automotive diagnostics.

### DOIP Features
- **Raw lwIP API Integration** - Event-driven, zero-polling network architecture
- **FreeRTOS Stream Buffers** - Efficient inter-task communication  
- **TCP Retransmission Optimization** - Conditional ACK strategy for reliable flow control
- **Complete DOIP Protocol Support** - Vehicle discovery, routing activation, diagnostic messaging
- **Python ECU Emulator** - Full-featured test environment in `pc/python/`

### DOIP Client Architecture
**File:** `doip_client.c`
- **Hybrid Implementation:** Supports both raw lwIP and socket APIs
- **Event-Driven Reception:** Uses TCP callbacks with stream buffers
- **Optimized Flow Control:** ACKs only successfully buffered data
- **Non-Blocking Operations:** Prevents network processing delays

### TCP Optimization Features
- **Conditional ACK Strategy** - Only acknowledge data successfully buffered to stream
- **Enhanced TCP Parameters** - Optimized retransmission timers and window sizes  
- **Non-Blocking Send Operations** - Immediate `tcp_output()` without waiting for ACK
- **Larger Stream Buffers** - Configurable buffer sizes to prevent data loss

### DOIP ECU Emulator
**File:** `pc/python/doip_ecu_emulator.py`
- **ISO 13400 Compliant** - Proper DOIP header and payload structures
- **Multi-Protocol Support** - UDP discovery and TCP diagnostics
- **VIN & Diagnostic Services** - Emulates real ECU responses
- **Alive Check Implementation** - Bidirectional keep-alive messaging

### Performance Optimizations
- **Stream Buffer Size:** `DOIP_STREAM_BUFFER_SIZE` configurable for memory vs latency
- **TCP Window Management:** Enhanced flow control reduces retransmissions
- **Priority-Based Scheduling:** Network tasks appropriately prioritized
- **Memory-Efficient Design:** Zero-copy operations where possible

## 📈 Performance Tuning

**Memory Optimization:**
- Adjust `MEM_SIZE` in `lwipopts.h` for available RAM
- Tune `configTOTAL_HEAP_SIZE` in FreeRTOSConfig.h
- Configure `DOIP_STREAM_BUFFER_SIZE` for optimal buffering

**Network Performance:**
- Increase `PBUF_POOL_SIZE` in lwipopts.h for more buffers
- Adjust `TCP_WND` for TCP window size
- Modify `GMAC_RX_BUFFERS`/`GMAC_TX_BUFFERS` in webserver_tasks.h

**TCP Retransmission Reduction:**
- **Conditional ACK Strategy:** Data acknowledged only after successful buffering
- **Optimized TCP Parameters:** `TCP_MAXRTX`, `TCP_SYNMAXRTX`, `TCP_RTO_TIME`
- **Non-Blocking Operations:** Prevents network stack delays