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

## Driver Development Guide

This project implements a universal driver pattern for hardware abstraction. Follow this guide when creating new drivers.

### Universal Driver Architecture

The project uses a three-layer driver architecture:

1. **Universal Driver Interface** (`drivers/driver_*.h`) - Hardware-agnostic API
2. **Universal Driver Implementation** (`drivers/driver_*.c`) - Common logic and validation
3. **BSP Driver Implementation** (`hw/same54/drivers/bsp_*.c`) - Hardware-specific code

### Creating a New Driver

Follow these steps to add a new peripheral driver (using "SPI" as an example):

#### 1. Define Universal Driver Interface

Create `drivers/driver_spi.h`:

```c
#ifndef _DRIVER_SPI_H_
#define _DRIVER_SPI_H_

#include <stdbool.h>
#include <stdint.h>

// Status enumeration
typedef enum {
    DRV_SPI_STATUS_OK = 0,
    DRV_SPI_STATUS_ERROR = 1,
    DRV_SPI_STATUS_BUSY = 2,
    DRV_SPI_STATUS_TIMEOUT = 3,
} drv_spi_status_t;

// Callback types
typedef enum {
    DRV_SPI_CB_TRANSFER_COMPLETE = 0,
    DRV_SPI_CB_ERROR = 1,
} drv_spi_cb_type_t;

typedef void (*drv_spi_callback_t)(void);

// Driver structure with function pointers
typedef struct {
    bool is_init;
    bool is_enabled;
    const void *hw_context;
    
    // Core operations
    drv_spi_status_t (*init)(const void *hw_context);
    drv_spi_status_t (*deinit)(const void *hw_context);
    drv_spi_status_t (*enable)(const void *hw_context);
    drv_spi_status_t (*disable)(const void *hw_context);
    
    // Data operations
    drv_spi_status_t (*transfer)(const void *hw_context, const uint8_t *tx_data, 
                                uint8_t *rx_data, uint32_t length);
    
    // Configuration
    drv_spi_status_t (*set_baudrate)(const void *hw_context, uint32_t baudrate);
    
    // Callback management
    drv_spi_status_t (*register_callback)(const void *hw_context, 
                                         drv_spi_cb_type_t type, 
                                         drv_spi_callback_t callback);
} drv_spi_t;

#ifdef __cplusplus
extern "C" {
#endif

// Universal API functions
drv_spi_status_t hw_spi_init(drv_spi_t *handle);
drv_spi_status_t hw_spi_deinit(drv_spi_t *handle);
drv_spi_status_t hw_spi_enable(drv_spi_t *handle);
drv_spi_status_t hw_spi_disable(drv_spi_t *handle);
drv_spi_status_t hw_spi_transfer(drv_spi_t *handle, const uint8_t *tx_data, 
                                uint8_t *rx_data, uint32_t length);
drv_spi_status_t hw_spi_set_baudrate(drv_spi_t *handle, uint32_t baudrate);
drv_spi_status_t hw_spi_register_callback(drv_spi_t *handle, 
                                         drv_spi_cb_type_t type, 
                                         drv_spi_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_SPI_H_
```

#### 2. Implement Universal Driver Logic

Create `drivers/driver_spi.c`:

```c
#include "driver_spi.h"
#include "utils_assert.h"

drv_spi_status_t hw_spi_init(drv_spi_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->init != NULL);
    
    if (handle->is_init) {
        return DRV_SPI_STATUS_OK;
    }
    
    drv_spi_status_t result = handle->init(handle->hw_context);
    if (result == DRV_SPI_STATUS_OK) {
        handle->is_init = true;
    }
    
    return result;
}

drv_spi_status_t hw_spi_transfer(drv_spi_t *handle, const uint8_t *tx_data, 
                                uint8_t *rx_data, uint32_t length)
{
    ASSERT(handle != NULL);
    ASSERT(handle->transfer != NULL);
    ASSERT(tx_data != NULL || rx_data != NULL);
    ASSERT(length > 0);
    
    if (!handle->is_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    return handle->transfer(handle->hw_context, tx_data, rx_data, length);
}

// Implement other universal functions...
```

#### 3. Create BSP Driver Implementation

Create `hw/same54/drivers/bsp_spi.c`:

```c
#include "bsp_spi.h"
#include "driver_spi.h"
#include "hal_spi_m_sync.h"  // ASF4 SPI driver
#include "utils_assert.h"
#include "printf.h"

// Convert ASF4 error codes
static drv_spi_status_t convert_error_code(int32_t asf4_error)
{
    switch (asf4_error) {
        case ERR_NONE:
            return DRV_SPI_STATUS_OK;
        case ERR_BUSY:
            return DRV_SPI_STATUS_BUSY;
        case ERR_TIMEOUT:
            return DRV_SPI_STATUS_TIMEOUT;
        default:
            return DRV_SPI_STATUS_ERROR;
    }
}

// Hardware context structure
typedef struct {
    struct spi_m_sync_descriptor *spi_desc;
    drv_spi_callback_t transfer_callback;
    drv_spi_callback_t error_callback;
} drv_spi_hw_context_t;

// Static hardware context
static drv_spi_hw_context_t drv_spi_hw_context_0 = {
    .spi_desc = &SPI_0,  // External ASF4 descriptor
    .transfer_callback = NULL,
    .error_callback = NULL,
};

// Forward declarations
static drv_spi_status_t drv_spi_init_impl(const void *hw_context);
static drv_spi_status_t drv_spi_transfer_impl(const void *hw_context, 
                                              const uint8_t *tx_data, 
                                              uint8_t *rx_data, 
                                              uint32_t length);

// Global driver instance
drv_spi_t spi_0 = {
    .is_init = false,
    .is_enabled = false,
    .hw_context = &drv_spi_hw_context_0,
    .init = drv_spi_init_impl,
    .deinit = drv_spi_deinit_impl,
    .enable = drv_spi_enable_impl,
    .disable = drv_spi_disable_impl,
    .transfer = drv_spi_transfer_impl,
    .set_baudrate = drv_spi_set_baudrate_impl,
    .register_callback = drv_spi_register_callback_impl,
};

// Implementation functions
static drv_spi_status_t drv_spi_init_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_spi_hw_context_t *context = (const drv_spi_hw_context_t *)hw_context;
    
    printf("[SPI] Initializing SPI controller\r\n");
    
    int32_t result = spi_m_sync_init(context->spi_desc, SPI);
    if (result != ERR_NONE) {
        return convert_error_code(result);
    }
    
    printf("[SPI] SPI initialized successfully\r\n");
    return DRV_SPI_STATUS_OK;
}

static drv_spi_status_t drv_spi_transfer_impl(const void *hw_context, 
                                              const uint8_t *tx_data, 
                                              uint8_t *rx_data, 
                                              uint32_t length)
{
    ASSERT(hw_context != NULL);
    const drv_spi_hw_context_t *context = (const drv_spi_hw_context_t *)hw_context;
    
    struct spi_xfer xfer = {
        .txbuf = (uint8_t *)tx_data,
        .rxbuf = rx_data,
        .size = length
    };
    
    int32_t result = spi_m_sync_transfer(context->spi_desc, &xfer);
    return convert_error_code(result);
}
```

#### 4. Create BSP Header

Create `hw/same54/drivers/bsp_spi.h`:

```c
#ifndef _BSP_SPI_H_
#define _BSP_SPI_H_

#include "driver_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Global driver instances
extern drv_spi_t spi_0;

#ifdef __cplusplus
}
#endif

#endif // _BSP_SPI_H_
```

#### 5. Usage Example

```c
#include "bsp_spi.h"

void spi_example(void)
{
    // Initialize SPI
    drv_spi_status_t status = hw_spi_init(&spi_0);
    if (status != DRV_SPI_STATUS_OK) {
        printf("SPI init failed: %d\r\n", status);
        return;
    }
    
    // Enable SPI
    hw_spi_enable(&spi_0);
    
    // Transfer data
    uint8_t tx_data[] = {0x01, 0x02, 0x03};
    uint8_t rx_data[3];
    
    status = hw_spi_transfer(&spi_0, tx_data, rx_data, sizeof(tx_data));
    if (status == DRV_SPI_STATUS_OK) {
        printf("SPI transfer successful\r\n");
    }
}
```

### Driver Architecture Benefits

1. **Hardware Abstraction**: Application code uses universal API
2. **Portability**: Easy to port to different hardware platforms
3. **Validation**: Common parameter checking and state management
4. **Consistency**: Standardized error handling and callback patterns
5. **Maintainability**: Clear separation between hardware-specific and generic code

### Existing Driver Examples

Study these implemented drivers as references:
- **LED Driver**: `drivers/driver_led.h/.c` and `hw/same54/drivers/bsp_led.c`
- **Ethernet Driver**: `drivers/driver_ethernet.h/.c` and `hw/same54/drivers/bsp_ethernet.c`

### Driver Integration

1. Add new driver headers to relevant source files
2. Include BSP headers in hardware initialization code
3. Initialize drivers in `atmel_start.c` or similar initialization sequence
4. Use universal API functions throughout application code

## Security Notes

- Don't mention Cloudecode in comments or commits