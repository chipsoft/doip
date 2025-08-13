# Adding New Ethernet Interface to DOIP Project

This guide provides a complete template and step-by-step instructions for adding any new Ethernet interface to the existing DOIP project while maintaining the universal driver architecture and build system flexibility.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Implementation Templates](#implementation-templates)
3. [Step-by-Step Integration Guide](#step-by-step-integration-guide)
4. [Build System Integration](#build-system-integration)
5. [Configuration Examples](#configuration-examples)
6. [Testing and Validation](#testing-and-validation)

## Architecture Overview

The project uses a three-layer architecture for hardware abstraction:

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                        │
│                   (DOIP, User Tasks)                       │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                    LWIP Integration                         │
│    (ethif_INTERFACE_init, linkoutput, input, task)        │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                 Universal Driver Layer                      │
│        (drivers/driver_INTERFACE.h/.c)                    │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                  BSP Driver Layer                          │
│         (hw/same54/drivers/bsp_INTERFACE.c)               │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                    Hardware Layer                          │
│              (Controller-specific code)                    │
└─────────────────────────────────────────────────────────────┘
```

### Current Implementations

- **GMAC**: Built-in Ethernet MAC controller with external PHY
- **KSZ8851SNL**: SPI-based Ethernet controller with built-in PHY

## Implementation Templates

### 1. Universal Driver Interface Template

Create `drivers/driver_NEWINTF.h`:

```c
#ifndef _DRIVER_NEWINTF_H_
#define _DRIVER_NEWINTF_H_

#include <stdbool.h>
#include <stdint.h>

// Status enumeration
typedef enum {
    DRV_NEWINTF_STATUS_OK = 0,
    DRV_NEWINTF_STATUS_ERROR = 1,
    DRV_NEWINTF_STATUS_BUSY = 2,
    DRV_NEWINTF_STATUS_TIMEOUT = 3,
    DRV_NEWINTF_STATUS_INVALID_PARAM = 4,
    DRV_NEWINTF_STATUS_NO_LINK = 5,
} drv_newintf_status_t;

// Callback types
typedef enum {
    DRV_NEWINTF_CB_RX_COMPLETE = 0,
    DRV_NEWINTF_CB_TX_COMPLETE = 1,
    DRV_NEWINTF_CB_LINK_CHANGE = 2,
    DRV_NEWINTF_CB_ERROR = 3,
} drv_newintf_cb_type_t;

typedef void (*drv_newintf_callback_t)(void);

// Configuration structure
typedef struct {
    uint8_t mac_addr[6];
    bool auto_negotiation;
    uint16_t link_speed;     // 10, 100, or 1000 Mbps
    bool full_duplex;
    // Add interface-specific config parameters here
} drv_newintf_config_t;

// Status information structure
typedef struct {
    bool link_up;
    uint16_t link_speed;
    bool full_duplex;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
} drv_newintf_status_info_t;

// Driver structure with function pointers
typedef struct {
    bool is_init;
    bool is_enabled;
    const void *hw_context;
    
    // Core operations
    drv_newintf_status_t (*init)(const void *hw_context, const drv_newintf_config_t *config);
    drv_newintf_status_t (*deinit)(const void *hw_context);
    drv_newintf_status_t (*enable)(const void *hw_context);
    drv_newintf_status_t (*disable)(const void *hw_context);
    
    // Data operations
    drv_newintf_status_t (*send_packet)(const void *hw_context, const uint8_t *data, uint16_t length);
    drv_newintf_status_t (*receive_packet)(const void *hw_context, uint8_t *data, uint16_t *length);
    drv_newintf_status_t (*check_rx_available)(const void *hw_context, bool *rx_available);
    
    // Status and configuration
    drv_newintf_status_t (*get_status)(const void *hw_context, drv_newintf_status_info_t *status_info);
    
    // Callback management
    drv_newintf_status_t (*register_callback)(const void *hw_context, 
                                             drv_newintf_cb_type_t type, 
                                             drv_newintf_callback_t callback);
} drv_newintf_t;

#ifdef __cplusplus
extern "C" {
#endif

// Universal API functions
drv_newintf_status_t hw_newintf_init(drv_newintf_t *handle, const drv_newintf_config_t *config);
drv_newintf_status_t hw_newintf_deinit(drv_newintf_t *handle);
drv_newintf_status_t hw_newintf_enable(drv_newintf_t *handle);
drv_newintf_status_t hw_newintf_disable(drv_newintf_t *handle);

drv_newintf_status_t hw_newintf_send_packet(drv_newintf_t *handle, const uint8_t *data, uint16_t length);
drv_newintf_status_t hw_newintf_receive_packet(drv_newintf_t *handle, uint8_t *data, uint16_t *length);
drv_newintf_status_t hw_newintf_check_rx_available(drv_newintf_t *handle, bool *rx_available);

drv_newintf_status_t hw_newintf_get_status(drv_newintf_t *handle, drv_newintf_status_info_t *status_info);

drv_newintf_status_t hw_newintf_register_callback(drv_newintf_t *handle, 
                                                 drv_newintf_cb_type_t type, 
                                                 drv_newintf_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_NEWINTF_H_
```

### 2. Universal Driver Implementation Template

Create `drivers/driver_newintf.c`:

```c
#include "driver_newintf.h"
#include "utils_assert.h"

drv_newintf_status_t hw_newintf_init(drv_newintf_t *handle, const drv_newintf_config_t *config)
{
    ASSERT(handle != NULL);
    ASSERT(handle->init != NULL);
    ASSERT(config != NULL);
    
    if (handle->is_init) {
        return DRV_NEWINTF_STATUS_OK;
    }
    
    drv_newintf_status_t result = handle->init(handle->hw_context, config);
    if (result == DRV_NEWINTF_STATUS_OK) {
        handle->is_init = true;
    }
    
    return result;
}

drv_newintf_status_t hw_newintf_deinit(drv_newintf_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    
    if (!handle->is_init) {
        return DRV_NEWINTF_STATUS_OK;
    }
    
    drv_newintf_status_t result = handle->deinit(handle->hw_context);
    if (result == DRV_NEWINTF_STATUS_OK) {
        handle->is_init = false;
        handle->is_enabled = false;
    }
    
    return result;
}

drv_newintf_status_t hw_newintf_enable(drv_newintf_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->enable != NULL);
    
    if (!handle->is_init) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    if (handle->is_enabled) {
        return DRV_NEWINTF_STATUS_OK;
    }
    
    drv_newintf_status_t result = handle->enable(handle->hw_context);
    if (result == DRV_NEWINTF_STATUS_OK) {
        handle->is_enabled = true;
    }
    
    return result;
}

drv_newintf_status_t hw_newintf_disable(drv_newintf_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->disable != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_NEWINTF_STATUS_OK;
    }
    
    drv_newintf_status_t result = handle->disable(handle->hw_context);
    if (result == DRV_NEWINTF_STATUS_OK) {
        handle->is_enabled = false;
    }
    
    return result;
}

drv_newintf_status_t hw_newintf_send_packet(drv_newintf_t *handle, const uint8_t *data, uint16_t length)
{
    ASSERT(handle != NULL);
    ASSERT(handle->send_packet != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    return handle->send_packet(handle->hw_context, data, length);
}

drv_newintf_status_t hw_newintf_receive_packet(drv_newintf_t *handle, uint8_t *data, uint16_t *length)
{
    ASSERT(handle != NULL);
    ASSERT(handle->receive_packet != NULL);
    ASSERT(data != NULL);
    ASSERT(length != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    return handle->receive_packet(handle->hw_context, data, length);
}

drv_newintf_status_t hw_newintf_check_rx_available(drv_newintf_t *handle, bool *rx_available)
{
    ASSERT(handle != NULL);
    ASSERT(handle->check_rx_available != NULL);
    ASSERT(rx_available != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        *rx_available = false;
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    return handle->check_rx_available(handle->hw_context, rx_available);
}

drv_newintf_status_t hw_newintf_get_status(drv_newintf_t *handle, drv_newintf_status_info_t *status_info)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_status != NULL);
    ASSERT(status_info != NULL);
    
    if (!handle->is_init) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    return handle->get_status(handle->hw_context, status_info);
}

drv_newintf_status_t hw_newintf_register_callback(drv_newintf_t *handle, 
                                                 drv_newintf_cb_type_t type, 
                                                 drv_newintf_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(handle->register_callback != NULL);
    
    if (!handle->is_init) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    return handle->register_callback(handle->hw_context, type, callback);
}
```

### 3. BSP Driver Header Template

Create `hw/same54/drivers/bsp_newintf.h`:

```c
#ifndef _BSP_NEWINTF_H_
#define _BSP_NEWINTF_H_

#include "driver_newintf.h"

#ifdef __cplusplus
extern "C" {
#endif

// Global driver instances
extern drv_newintf_t newintf_0;

#ifdef __cplusplus
}
#endif

#endif // _BSP_NEWINTF_H_
```

### 4. BSP Driver Implementation Template

Create `hw/same54/drivers/bsp_newintf.c`:

```c
#include "bsp_newintf.h"
#include "driver_newintf.h"
#include "utils_assert.h"
#include "printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <hal_gpio.h>
#include <hal_delay.h>
// Add interface-specific includes here

// Hardware context structure
typedef struct {
    drv_newintf_callback_t rx_callback;
    drv_newintf_callback_t tx_callback;
    drv_newintf_callback_t link_callback;
    drv_newintf_callback_t error_callback;
    bool is_initialized;
    bool is_enabled;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
    // Add interface-specific context here
} drv_newintf_hw_context_t;

static drv_newintf_hw_context_t drv_newintf_hw_context_0 = {
    .rx_callback = NULL,
    .tx_callback = NULL,
    .link_callback = NULL,
    .error_callback = NULL,
    .is_initialized = false,
    .is_enabled = false,
    .rx_packets = 0,
    .tx_packets = 0,
    .rx_errors = 0,
    .tx_errors = 0,
};

// Interrupt handling variables (if needed)
static SemaphoreHandle_t newintf_interrupt_semaphore = NULL;

// Forward declarations
static drv_newintf_status_t drv_newintf_init_impl(const void *hw_context, const drv_newintf_config_t *config);
static drv_newintf_status_t drv_newintf_deinit_impl(const void *hw_context);
static drv_newintf_status_t drv_newintf_enable_impl(const void *hw_context);
static drv_newintf_status_t drv_newintf_disable_impl(const void *hw_context);
static drv_newintf_status_t drv_newintf_send_packet_impl(const void *hw_context, const uint8_t *data, uint16_t length);
static drv_newintf_status_t drv_newintf_receive_packet_impl(const void *hw_context, uint8_t *data, uint16_t *length);
static drv_newintf_status_t drv_newintf_check_rx_available_impl(const void *hw_context, bool *rx_available);
static drv_newintf_status_t drv_newintf_get_status_impl(const void *hw_context, drv_newintf_status_info_t *status_info);
static drv_newintf_status_t drv_newintf_register_callback_impl(const void *hw_context, 
                                                              drv_newintf_cb_type_t type, 
                                                              drv_newintf_callback_t callback);

// Global driver instance
drv_newintf_t newintf_0 = {
    .is_init = false,
    .is_enabled = false,
    .hw_context = &drv_newintf_hw_context_0,
    .init = drv_newintf_init_impl,
    .deinit = drv_newintf_deinit_impl,
    .enable = drv_newintf_enable_impl,
    .disable = drv_newintf_disable_impl,
    .send_packet = drv_newintf_send_packet_impl,
    .receive_packet = drv_newintf_receive_packet_impl,
    .check_rx_available = drv_newintf_check_rx_available_impl,
    .get_status = drv_newintf_get_status_impl,
    .register_callback = drv_newintf_register_callback_impl,
};

// Implementation functions
static drv_newintf_status_t drv_newintf_init_impl(const void *hw_context, const drv_newintf_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    printf("[NEWINTF] Initializing interface\r\n");
    
    // TODO: Add interface-specific initialization here
    // - Configure pins/peripherals
    // - Initialize communication interface (SPI, I2C, etc.)
    // - Setup interrupts if needed
    // - Configure MAC address
    // - Initialize PHY if external
    
    context->is_initialized = true;
    printf("[NEWINTF] Interface initialized successfully\r\n");
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t drv_newintf_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    printf("[NEWINTF] Deinitializing interface\r\n");
    
    // TODO: Add interface-specific deinitialization here
    // - Disable interrupts
    // - Power down interface
    // - Free resources
    
    context->is_initialized = false;
    context->is_enabled = false;
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t drv_newintf_enable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    printf("[NEWINTF] Enabling interface\r\n");
    
    // TODO: Add interface-specific enable here
    // - Enable receiver/transmitter
    // - Start PHY auto-negotiation
    // - Enable interrupts
    
    context->is_enabled = true;
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t drv_newintf_disable_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    printf("[NEWINTF] Disabling interface\r\n");
    
    // TODO: Add interface-specific disable here
    // - Disable receiver/transmitter
    // - Disable interrupts
    // - Put interface in low power mode
    
    context->is_enabled = false;
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t drv_newintf_send_packet_impl(const void *hw_context, const uint8_t *data, uint16_t length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    if (!context->is_initialized || !context->is_enabled) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    // TODO: Add interface-specific packet transmission here
    // - Check transmit buffer availability
    // - Copy data to transmit buffer
    // - Trigger transmission
    // - Handle transmission completion
    
    context->tx_packets++;
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t drv_newintf_receive_packet_impl(const void *hw_context, uint8_t *data, uint16_t *length)
{
    ASSERT(hw_context != NULL);
    ASSERT(data != NULL);
    ASSERT(length != NULL);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    if (!context->is_initialized || !context->is_enabled) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    // TODO: Add interface-specific packet reception here
    // - Check receive buffer status
    // - Read packet length
    // - Copy data from receive buffer
    // - Update receive statistics
    
    context->rx_packets++;
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t drv_newintf_check_rx_available_impl(const void *hw_context, bool *rx_available)
{
    ASSERT(hw_context != NULL);
    ASSERT(rx_available != NULL);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    if (!context->is_initialized || !context->is_enabled) {
        *rx_available = false;
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    // TODO: Add interface-specific receive check here
    // - Check receive buffer status
    // - Check packet count
    // - Return availability status
    
    *rx_available = false; // Default to no packets available
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t drv_newintf_get_status_impl(const void *hw_context, drv_newintf_status_info_t *status_info)
{
    ASSERT(hw_context != NULL);
    ASSERT(status_info != NULL);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    // TODO: Add interface-specific status reading here
    // - Read link status
    // - Read link speed and duplex
    // - Read error counters
    
    status_info->link_up = true; // Default values
    status_info->link_speed = 100;
    status_info->full_duplex = true;
    status_info->rx_packets = context->rx_packets;
    status_info->tx_packets = context->tx_packets;
    status_info->rx_errors = context->rx_errors;
    status_info->tx_errors = context->tx_errors;
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t drv_newintf_register_callback_impl(const void *hw_context, 
                                                              drv_newintf_cb_type_t type, 
                                                              drv_newintf_callback_t callback)
{
    ASSERT(hw_context != NULL);
    
    drv_newintf_hw_context_t *context = (drv_newintf_hw_context_t *)hw_context;
    
    if (!context->is_initialized) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    switch (type) {
        case DRV_NEWINTF_CB_RX_COMPLETE:
            context->rx_callback = callback;
            break;
        case DRV_NEWINTF_CB_TX_COMPLETE:
            context->tx_callback = callback;
            break;
        case DRV_NEWINTF_CB_LINK_CHANGE:
            context->link_callback = callback;
            break;
        case DRV_NEWINTF_CB_ERROR:
            context->error_callback = callback;
            break;
        default:
            return DRV_NEWINTF_STATUS_INVALID_PARAM;
    }
    
    return DRV_NEWINTF_STATUS_OK;
}

// Interrupt handler (if needed)
static void newintf_irq_handler(void)
{
    // TODO: Add interrupt handling here
    // - Read interrupt status
    // - Handle RX/TX/Link/Error interrupts
    // - Signal semaphore for packet processing task
    // - Call appropriate callbacks
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    if (newintf_interrupt_semaphore != NULL) {
        xSemaphoreGiveFromISR(newintf_interrupt_semaphore, &xHigherPriorityTaskWoken);
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### 5. LWIP Integration Template

Create `app_libs/lwip/port/ethif_newintf.c`:

```c
/*
 * NEWINTF LwIP network interface implementation
 * Provides full integration with LWIP stack and FreeRTOS
 */

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "netif/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include "ethernetif.h"
#include "printf.h"

// NEWINTF driver includes
#include "bsp_newintf.h"
#include "driver_newintf.h"

#include <string.h>

// Network interface constants
#define NEWINTF_MTU                    1500
#define NEWINTF_MAX_PACKET_SIZE        1518
#define NEWINTF_TASK_STACK_SIZE        512
#define NEWINTF_TASK_PRIORITY          (configMAX_PRIORITIES - 1)
#define NEWINTF_HOSTNAME               "newintf"

// Network interface context structure
typedef struct {
    drv_newintf_t *newintf_driver;
    struct netif *netif;
    sys_sem_t rx_sem;
    sys_thread_t task_id;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
} ethif_newintf_context_t;

// Global context instance
static ethif_newintf_context_t newintf_netif_context;

// Forward declarations
static void newintf_task(void *pvParameters);
static err_t ethif_newintf_linkoutput(struct netif *netif, struct pbuf *p);
static void newintf_rx_callback(void);
static uint16_t pbuf_to_buffer(struct pbuf *p, uint8_t *buffer, uint16_t buffer_size);
static struct pbuf *buffer_to_pbuf(const uint8_t *buffer, uint16_t length);

/**
 * Initialize the NEWINTF network interface
 * This function should be passed as a parameter to netif_add().
 */
err_t ethif_newintf_init(struct netif *netif)
{
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    
    printf("[NEWINTF] Initializing network interface\\r\\n");
    
    // Get driver instance from netif state
    ethif_newintf_context_t *context = &newintf_netif_context;
    context->newintf_driver = (drv_newintf_t *)netif->state;
    context->netif = netif;
    context->rx_packets = 0;
    context->tx_packets = 0;
    context->rx_errors = 0;
    context->tx_errors = 0;
    
    // Set netif context
    netif->state = context;
    
    // Set MAC hardware address length
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    
    // Set MAC hardware address (default, can be overridden)
    netif->hwaddr[0] = 0x02;
    netif->hwaddr[1] = 0x04;
    netif->hwaddr[2] = 0xA3;
    netif->hwaddr[3] = 0x12;
    netif->hwaddr[4] = 0x34;
    netif->hwaddr[5] = 0x57; // Different from KSZ to avoid conflicts
    
    // Maximum transfer unit
    netif->mtu = NEWINTF_MTU;
    
    // Device capabilities
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    
#if LWIP_NETIF_HOSTNAME
    // Initialize interface hostname
    netif->hostname = NEWINTF_HOSTNAME;
#endif
    
    // Set interface name
    netif->name[0] = 'n'; // newintf
    netif->name[1] = 'w';
    
    // Set netif output functions
    netif->output = etharp_output;
    netif->linkoutput = ethif_newintf_linkoutput;
    
    // Initialize NEWINTF driver
    drv_newintf_config_t config = {
        .mac_addr = {netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2], 
                     netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]},
        .auto_negotiation = true,
        .link_speed = 100,
        .full_duplex = true
    };
    
    drv_newintf_status_t status = hw_newintf_init(context->newintf_driver, &config);
    if (status != DRV_NEWINTF_STATUS_OK) {
        printf("[NEWINTF] Failed to initialize driver: %d\\r\\n", status);
        return ERR_IF;
    }
    
    // Register RX callback
    status = hw_newintf_register_callback(context->newintf_driver, 
                                         DRV_NEWINTF_CB_RX_COMPLETE, 
                                         newintf_rx_callback);
    if (status != DRV_NEWINTF_STATUS_OK) {
        printf("[NEWINTF] Failed to register RX callback: %d\\r\\n", status);
        return ERR_IF;
    }
    
    // Enable the driver
    status = hw_newintf_enable(context->newintf_driver);
    if (status != DRV_NEWINTF_STATUS_OK) {
        printf("[NEWINTF] Failed to enable driver: %d\\r\\n", status);
        return ERR_IF;
    }
    
    // Create receive semaphore
    if (sys_sem_new(&context->rx_sem, 0) != ERR_OK) {
        printf("[NEWINTF] Failed to create RX semaphore\\r\\n");
        return ERR_MEM;
    }
    
    // Create packet processing task
    context->task_id = sys_thread_new("NEWINTF", newintf_task, context, 
                                     NEWINTF_TASK_STACK_SIZE, 
                                     NEWINTF_TASK_PRIORITY);
    if (context->task_id.thread_handle == NULL) {
        printf("[NEWINTF] Failed to create packet processing task\\r\\n");
        sys_sem_free(&context->rx_sem);
        return ERR_MEM;
    }
    
    printf("[NEWINTF] Network interface initialized successfully\\r\\n");
    return ERR_OK;
}

/**
 * Low level output function for NEWINTF
 * This function is called by the TCP/IP stack when an IP packet is ready to be sent.
 */
static err_t ethif_newintf_linkoutput(struct netif *netif, struct pbuf *p)
{
    ethif_newintf_context_t *context = (ethif_newintf_context_t *)netif->state;
    static uint8_t tx_buffer[NEWINTF_MAX_PACKET_SIZE];
    
    LWIP_ASSERT("context != NULL", context != NULL);
    LWIP_ASSERT("context->newintf_driver != NULL", context->newintf_driver != NULL);
    
    if (p->tot_len > NEWINTF_MAX_PACKET_SIZE) {
        printf("[NEWINTF] Packet too large: %d bytes\\r\\n", p->tot_len);
        context->tx_errors++;
        return ERR_BUF;
    }
    
    // Convert pbuf chain to contiguous buffer
    uint16_t length = pbuf_to_buffer(p, tx_buffer, sizeof(tx_buffer));
    
    // Send packet via NEWINTF driver
    drv_newintf_status_t status = hw_newintf_send_packet(context->newintf_driver, 
                                                        tx_buffer, length);
    
    if (status == DRV_NEWINTF_STATUS_OK) {
        context->tx_packets++;
        LINK_STATS_INC(link.xmit);
        return ERR_OK;
    } else {
        context->tx_errors++;
        LINK_STATS_INC(link.err);
        printf("[NEWINTF] Transmit failed: %d\\r\\n", status);
        return ERR_IF;
    }
}

/**
 * Process received packets and pass them to the LWIP stack
 */
void ethif_newintf_input(struct netif *netif)
{
    ethif_newintf_context_t *context = (ethif_newintf_context_t *)netif->state;
    static uint8_t rx_buffer[NEWINTF_MAX_PACKET_SIZE];
    uint16_t length;
    
    LWIP_ASSERT("context != NULL", context != NULL);
    LWIP_ASSERT("context->newintf_driver != NULL", context->newintf_driver != NULL);
    
    // Check if packets are available
    bool rx_available = false;
    drv_newintf_status_t status = hw_newintf_check_rx_available(context->newintf_driver, &rx_available);
    
    if (status != DRV_NEWINTF_STATUS_OK || !rx_available) {
        return;
    }
    
    // Process all available packets
    while (rx_available) {
        length = sizeof(rx_buffer);
        status = hw_newintf_receive_packet(context->newintf_driver, rx_buffer, &length);
        
        if (status == DRV_NEWINTF_STATUS_OK && length > 0) {
            // Create pbuf from received data
            struct pbuf *p = buffer_to_pbuf(rx_buffer, length);
            
            if (p != NULL) {
                // Pass packet to LWIP stack
                if (netif->input(p, netif) != ERR_OK) {
                    printf("[NEWINTF] Input error, dropping packet\\r\\n");
                    pbuf_free(p);
                    context->rx_errors++;
                    LINK_STATS_INC(link.err);
                } else {
                    context->rx_packets++;
                    LINK_STATS_INC(link.recv);
                }
            } else {
                printf("[NEWINTF] Failed to allocate pbuf for packet\\r\\n");
                context->rx_errors++;
                LINK_STATS_INC(link.memerr);
            }
        } else {
            printf("[NEWINTF] Receive failed: %d\\r\\n", status);
            context->rx_errors++;
            break;
        }
        
        // Check for more packets
        hw_newintf_check_rx_available(context->newintf_driver, &rx_available);
    }
}

/**
 * NEWINTF packet processing task
 * Waits for interrupt notifications and processes received packets
 */
static void newintf_task(void *pvParameters)
{
    ethif_newintf_context_t *context = (ethif_newintf_context_t *)pvParameters;
    
    printf("[NEWINTF] Packet processing task started\\r\\n");
    
    while (1) {
        // Wait for RX interrupt notification
        if (sys_arch_sem_wait(&context->rx_sem, portMAX_DELAY) == SYS_ARCH_TIMEOUT) {
            continue;
        }
        
        // Process all available packets
        ethif_newintf_input(context->netif);
    }
}

/**
 * RX callback function called from interrupt context
 * Signals the packet processing task that packets are available
 */
static void newintf_rx_callback(void)
{
    // Signal packet processing task from ISR
    sys_sem_signal(&newintf_netif_context.rx_sem);
}

/**
 * Convert pbuf chain to contiguous buffer
 */
static uint16_t pbuf_to_buffer(struct pbuf *p, uint8_t *buffer, uint16_t buffer_size)
{
    uint16_t copied = 0;
    struct pbuf *q;
    
    for (q = p; q != NULL && copied < buffer_size; q = q->next) {
        uint16_t copy_len = (q->len <= (buffer_size - copied)) ? q->len : (buffer_size - copied);
        memcpy(buffer + copied, q->payload, copy_len);
        copied += copy_len;
    }
    
    return copied;
}

/**
 * Convert buffer to pbuf
 */
static struct pbuf *buffer_to_pbuf(const uint8_t *buffer, uint16_t length)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
    
    if (p != NULL) {
        pbuf_take(p, buffer, length);
    }
    
    return p;
}
```

### 6. Header File Updates

Add to `app_libs/lwip/port/include/ethernetif.h`:

```c
// NEWINTF network interface functions
err_t ethif_newintf_init(struct netif *netif);
void ethif_newintf_input(struct netif *netif);
```

## Step-by-Step Integration Guide

### Step 1: Create Driver Files

1. **Create Universal Driver Files:**
   ```bash
   # Create driver interface
   touch drivers/driver_newintf.h
   touch drivers/driver_newintf.c
   
   # Create BSP driver
   touch hw/same54/drivers/bsp_newintf.h
   touch hw/same54/drivers/bsp_newintf.c
   
   # Create LWIP integration
   touch app_libs/lwip/port/ethif_newintf.c
   ```

2. **Implement Templates:**
   - Copy and modify the templates above
   - Replace `NEWINTF` with your actual interface name
   - Replace `newintf` with your actual interface name (lowercase)
   - Add interface-specific includes and definitions

### Step 2: Implement Hardware-Specific Code

1. **BSP Driver Implementation:**
   - Add interface-specific initialization (GPIO, SPI, I2C, etc.)
   - Implement packet transmission and reception
   - Add interrupt handling if needed
   - Implement status reading and link monitoring

2. **Register Definitions:**
   - Add interface-specific register definitions
   - Create configuration structures
   - Define interface-specific constants

### Step 3: Update Build System

Add to `Makefile` in the network interface selection section:

```makefile
# Network Interface Selection
ifeq ($(NETWORK_INTERFACE), KSZ8851SNL)
# KSZ8851SNL configuration (existing)
else ifeq ($(NETWORK_INTERFACE), NEWINTF)
DRIVER_CFILES += \
$(DRIVERS_DIR)/driver_newintf.c \
$(BSP_DRIVERS_DIR)/bsp_newintf.c \
$(LWIP_DIR)/port/ethif_newintf.c
DEFINES += -DUSE_NEWINTF_INTERFACE=1
# Add interface-specific ASF4 files if needed
# ASF4_CFILES += $(ASF4_DIR)/hal/src/hal_spi_m_sync.c
DIR_INCLUDES += -I"$(LWIP_DIR)/port/include"
$(info Building with NEWINTF interface)
else ifeq ($(NETWORK_INTERFACE), GMAC)
# GMAC configuration (existing)
else
$(error "Unknown NETWORK_INTERFACE: $(NETWORK_INTERFACE). Valid options: KSZ8851SNL, NEWINTF, GMAC")
endif
```

Add build target:

```makefile
newintf:
	@echo "Building with NEWINTF interface..."
	$(MAKE) clean
	$(MAKE) NETWORK_INTERFACE=NEWINTF all
```

### Step 4: Update Configuration Files

1. **Update `eth_ipstack_main.c`:**

```c
// Add conditional includes
#ifdef USE_NEWINTF_INTERFACE
#include "bsp_newintf.h"
#include "ethernetif.h"
#endif

// Update netif_add call
#ifdef USE_KSZ8851SNL_INTERFACE
	// KSZ8851SNL configuration (existing)
#elif defined(USE_NEWINTF_INTERFACE)
	// NEWINTF interface
	netif_add(&TCPIP_STACK_INTERFACE_0_desc,
	          &ip, &nm, &gw,
	          (void *)&newintf_0,              // NEWINTF driver instance
	          ethif_newintf_init,              // NEWINTF init function
	          tcpip_input);                    // Threading input
#elif defined(USE_GMAC_INTERFACE)
	// GMAC configuration (existing)
#else
	#error "No network interface selected"
#endif
```

2. **Update `eth_ipstack_main.h`:**

```c
// Conditional includes based on network interface selection
#ifdef USE_GMAC_INTERFACE
#include <ethif_mac.h>
#endif
#ifdef USE_NEWINTF_INTERFACE
// Add interface-specific includes if needed
#endif
```

### Step 5: Integration Testing

1. **Build Test:**
   ```bash
   make newintf
   ```

2. **Functional Test:**
   - Verify driver initialization
   - Test packet transmission and reception
   - Check DOIP communication
   - Monitor network statistics

### Step 6: Advanced Features (Optional)

1. **Power Management:**
   - Add sleep/wake functionality
   - Implement low-power modes

2. **Error Recovery:**
   - Add error detection and recovery
   - Implement watchdog functionality

3. **Advanced Networking:**
   - Add VLAN support
   - Implement QoS features
   - Add network security features

## Configuration Examples

### Example: SPI-based Ethernet Controller

For SPI-based controllers like W5500, ENC28J60, etc.:

```c
// In bsp_newintf.c
#include "bsp_spi.h"

// Add SPI configuration
static drv_newintf_status_t spi_init(void)
{
    drv_spi_status_t status = hw_spi_init(&spi_0);
    if (status != DRV_SPI_STATUS_OK) {
        return DRV_NEWINTF_STATUS_ERROR;
    }
    
    // Configure CS pin
    gpio_set_pin_direction(PIN_PA05, GPIO_DIRECTION_OUT);
    gpio_set_pin_level(PIN_PA05, true); // CS high
    
    return DRV_NEWINTF_STATUS_OK;
}

// SPI transaction helper
static drv_newintf_status_t spi_transaction(const uint8_t *tx_data, uint8_t *rx_data, uint16_t length)
{
    gpio_set_pin_level(PIN_PA05, false); // CS low
    
    drv_spi_status_t status = hw_spi_transfer(&spi_0, tx_data, rx_data, length);
    
    gpio_set_pin_level(PIN_PA05, true); // CS high
    
    return (status == DRV_SPI_STATUS_OK) ? DRV_NEWINTF_STATUS_OK : DRV_NEWINTF_STATUS_ERROR;
}
```

### Example: USB-Ethernet Adapter

For USB-based controllers:

```c
// In bsp_newintf.c
#include "usb_host.h"

// USB endpoint configuration
#define USB_ETH_EP_IN   0x81
#define USB_ETH_EP_OUT  0x02

static drv_newintf_status_t usb_init(void)
{
    // Initialize USB host
    usb_host_init();
    
    // Enumerate and configure USB Ethernet device
    // Configure endpoints
    // Set up bulk transfers
    
    return DRV_NEWINTF_STATUS_OK;
}
```

### Example: WiFi Module Integration

For WiFi modules like ESP32, ESP8266:

```c
// In bsp_newintf.c
#include "uart.h"

// AT command interface
static drv_newintf_status_t wifi_send_command(const char *cmd, char *response, uint32_t timeout)
{
    // Send AT command via UART
    // Wait for response
    // Parse response
    
    return DRV_NEWINTF_STATUS_OK;
}

static drv_newintf_status_t wifi_connect(const char *ssid, const char *password)
{
    char cmd[128];
    char response[256];
    
    // Connect to WiFi network
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    
    return wifi_send_command(cmd, response, 10000);
}
```

## Testing and Validation

### Unit Testing

1. **Driver API Testing:**
   ```c
   void test_newintf_driver(void)
   {
       drv_newintf_config_t config = {
           .mac_addr = {0x02, 0x04, 0xA3, 0x12, 0x34, 0x57},
           .auto_negotiation = true,
           .link_speed = 100,
           .full_duplex = true
       };
       
       // Test initialization
       drv_newintf_status_t status = hw_newintf_init(&newintf_0, &config);
       assert(status == DRV_NEWINTF_STATUS_OK);
       
       // Test enable
       status = hw_newintf_enable(&newintf_0);
       assert(status == DRV_NEWINTF_STATUS_OK);
       
       // Test status
       drv_newintf_status_info_t status_info;
       status = hw_newintf_get_status(&newintf_0, &status_info);
       assert(status == DRV_NEWINTF_STATUS_OK);
       assert(status_info.link_up == true);
       
       printf("NEWINTF driver test passed\n");
   }
   ```

2. **Packet Testing:**
   ```c
   void test_newintf_packets(void)
   {
       uint8_t test_packet[] = {
           0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Broadcast MAC
           0x02, 0x04, 0xA3, 0x12, 0x34, 0x57, // Source MAC
           0x08, 0x06,                         // ARP type
           // ARP packet data...
       };
       
       // Test packet transmission
       drv_newintf_status_t status = hw_newintf_send_packet(&newintf_0, test_packet, sizeof(test_packet));
       assert(status == DRV_NEWINTF_STATUS_OK);
       
       printf("Packet transmission test passed\n");
   }
   ```

### Integration Testing

1. **LWIP Integration:**
   - Test DHCP client functionality
   - Verify ARP table population
   - Test TCP/UDP socket communication

2. **DOIP Testing:**
   - Test DOIP discovery
   - Verify diagnostic communication
   - Test multi-ECU scenarios

### Performance Testing

1. **Throughput Testing:**
   - Measure maximum packet rate
   - Test sustained data transfer
   - Monitor memory usage

2. **Latency Testing:**
   - Measure packet processing latency
   - Test interrupt response time
   - Monitor task scheduling

## Troubleshooting Guide

### Common Issues

1. **Build Errors:**
   - Check Makefile conditional compilation
   - Verify all required files are included
   - Check for missing dependencies

2. **Runtime Issues:**
   - Verify hardware initialization
   - Check interrupt configuration
   - Monitor task creation and scheduling

3. **Network Issues:**
   - Verify MAC address configuration
   - Check link status and PHY configuration
   - Monitor packet statistics

### Debug Tools

1. **Debug Prints:**
   ```c
   #define NEWINTF_DEBUG 1
   
   #if NEWINTF_DEBUG
   #define NEWINTF_PRINTF(fmt, ...) printf("[NEWINTF] " fmt, ##__VA_ARGS__)
   #else
   #define NEWINTF_PRINTF(fmt, ...)
   #endif
   ```

2. **Statistics Monitoring:**
   ```c
   void newintf_print_stats(void)
   {
       drv_newintf_status_info_t status;
       hw_newintf_get_status(&newintf_0, &status);
       
       printf("Link: %s, Speed: %d Mbps, Duplex: %s\n", 
              status.link_up ? "UP" : "DOWN",
              status.link_speed,
              status.full_duplex ? "FULL" : "HALF");
       printf("RX: %lu packets, %lu errors\n", status.rx_packets, status.rx_errors);
       printf("TX: %lu packets, %lu errors\n", status.tx_packets, status.tx_errors);
   }
   ```

## Summary

This guide provides a complete framework for integrating any Ethernet interface with the DOIP project. The key principles are:

1. **Follow Universal Driver Pattern** - Maintain hardware abstraction
2. **Use Conditional Compilation** - Support multiple interfaces in one codebase
3. **Integrate with LWIP** - Provide proper network stack integration
4. **Support FreeRTOS** - Use tasks and semaphores appropriately
5. **Maintain Build System** - Add proper Makefile support

By following these templates and guidelines, you can easily add support for any Ethernet controller while maintaining compatibility with the existing DOIP infrastructure.