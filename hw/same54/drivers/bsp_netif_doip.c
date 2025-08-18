/**
 * @file bsp_netif_doip.c
 * @brief Network Interface Selection Implementation for DoIP Communications
 * 
 * @details This file provides the implementation of network interface selection
 * and management functions for DoIP communications. It provides a unified API
 * that works with different network interface implementations based on 
 * compile-time configuration.
 * 
 * @author  SAME54 DoIP Project
 * @date    2024
 * @version 1.0
 * 
 * @copyright MIT License - Free to use, modify, and distribute
 */

#include "bsp_netif_doip.h"
#include "utils_assert.h"
#include "printf.h"
#include <string.h>

// Include hardware-specific headers based on selection
#ifdef USE_KSZ8851SNL_INTERFACE
    #include "bsp_netif_doip_ksz.h"
#endif

drv_netif_doip_status_t bsp_netif_doip_init_default(void)
{
    drv_netif_doip_t *driver = bsp_netif_doip_get_driver();
    drv_netif_doip_config_t config;
    
    printf("NetIF DoIP: Initializing %s network interface with default configuration\r\n", 
           bsp_netif_doip_get_type_string());
    
    // Create default configuration for the selected interface type
    hw_netif_doip_create_default_config(&config, bsp_netif_doip_get_type());
    
    // Initialize interface-specific extensions
#ifdef USE_KSZ8851SNL_INTERFACE
    // Initialize KSZ8851SNL extended configuration
    drv_netif_doip_status_t ext_result = bsp_netif_doip_ksz_init_extended(NULL);
    if (ext_result != DRV_NETIF_DOIP_STATUS_OK) {
        printf("NetIF DoIP: KSZ8851SNL extended initialization failed: %d\r\n", ext_result);
        return ext_result;
    }
#endif
    
    // Initialize the driver
    drv_netif_doip_status_t result = hw_netif_doip_init(driver, &config);
    if (result == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NetIF DoIP: %s interface initialized successfully\r\n", 
               bsp_netif_doip_get_type_string());
        
        // Enable the interface
        result = hw_netif_doip_enable(driver);
        if (result == DRV_NETIF_DOIP_STATUS_OK) {
            printf("NetIF DoIP: %s interface enabled\r\n", 
                   bsp_netif_doip_get_type_string());
        } else {
            printf("NetIF DoIP: %s interface enable failed: %d\r\n", 
                   bsp_netif_doip_get_type_string(), result);
        }
    } else {
        printf("NetIF DoIP: %s interface initialization failed: %d\r\n", 
               bsp_netif_doip_get_type_string(), result);
    }
    
    return result;
}

drv_netif_doip_status_t bsp_netif_doip_init_with_config(const drv_netif_doip_config_t *config)
{
    ASSERT(config != NULL);
    
    drv_netif_doip_t *driver = bsp_netif_doip_get_driver();
    
    printf("NetIF DoIP: Initializing %s network interface with custom configuration\r\n", 
           bsp_netif_doip_get_type_string());
    
    // Validate that the interface type matches the selected implementation
    if (config->interface_type != bsp_netif_doip_get_type()) {
        printf("NetIF DoIP: Configuration interface type (%d) does not match selected type (%d)\r\n",
               config->interface_type, bsp_netif_doip_get_type());
        return DRV_NETIF_DOIP_STATUS_INVALID_PARAM;
    }
    
    // Initialize interface-specific extensions
#ifdef USE_KSZ8851SNL_INTERFACE
    // Initialize KSZ8851SNL extended configuration
    drv_netif_doip_status_t ext_result = bsp_netif_doip_ksz_init_extended(NULL);
    if (ext_result != DRV_NETIF_DOIP_STATUS_OK) {
        printf("NetIF DoIP: KSZ8851SNL extended initialization failed: %d\r\n", ext_result);
        return ext_result;
    }
#endif
    
    // Initialize the driver
    drv_netif_doip_status_t result = hw_netif_doip_init(driver, config);
    if (result == DRV_NETIF_DOIP_STATUS_OK) {
        printf("NetIF DoIP: %s interface initialized successfully\r\n", 
               bsp_netif_doip_get_type_string());
        
        // Enable the interface if requested
        result = hw_netif_doip_enable(driver);
        if (result == DRV_NETIF_DOIP_STATUS_OK) {
            printf("NetIF DoIP: %s interface enabled\r\n", 
                   bsp_netif_doip_get_type_string());
        } else {
            printf("NetIF DoIP: %s interface enable failed: %d\r\n", 
                   bsp_netif_doip_get_type_string(), result);
        }
    } else {
        printf("NetIF DoIP: %s interface initialization failed: %d\r\n", 
               bsp_netif_doip_get_type_string(), result);
    }
    
    return result;
}

bool bsp_netif_doip_hardware_available(void *hw_info)
{
    printf("NetIF DoIP: Checking %s hardware availability\r\n", 
           bsp_netif_doip_get_type_string());
    
#ifdef USE_KSZ8851SNL_INTERFACE
    uint16_t chip_id = 0;
    bool available = bsp_netif_doip_ksz_hardware_available(&chip_id);
    
    if (available) {
        printf("NetIF DoIP: KSZ8851SNL hardware detected (Chip ID: 0x%04X)\r\n", chip_id);
        if (hw_info != NULL) {
            *(uint16_t*)hw_info = chip_id;
        }
    } else {
        printf("NetIF DoIP: KSZ8851SNL hardware not available\r\n");
    }
    
    return available;
#else
    // GMAC hardware is always available on SAME54
    printf("NetIF DoIP: GMAC hardware available (built-in)\r\n");
    return true;
#endif
}

drv_netif_doip_status_t bsp_netif_doip_get_comprehensive_status(drv_netif_doip_status_info_t *status, bool extended_info)
{
    ASSERT(status != NULL);
    
    drv_netif_doip_t *driver = bsp_netif_doip_get_driver();
    
    // Get basic status
    drv_netif_doip_status_t result = hw_netif_doip_get_status(driver, status);
    if (result != DRV_NETIF_DOIP_STATUS_OK) {
        return result;
    }
    
    // Get extended information if requested
    if (extended_info) {
#ifdef USE_KSZ8851SNL_INTERFACE
        ksz_netif_status_extension_t ksz_extended;
        drv_netif_doip_status_t ext_result = bsp_netif_doip_ksz_get_extended_status(&ksz_extended);
        
        if (ext_result == DRV_NETIF_DOIP_STATUS_OK) {
            printf("NetIF DoIP: Extended KSZ8851SNL status available\r\n");
            printf("  Chip ID: 0x%04X, Revision: 0x%04X\r\n", 
                   ksz_extended.chip_id, ksz_extended.revision_id);
            printf("  SPI Communication: %s, Chip Detected: %s\r\n",
                   ksz_extended.spi_communication_ok ? "OK" : "FAIL",
                   ksz_extended.chip_detected ? "YES" : "NO");
        }
#endif
    }
    
    return result;
}

void bsp_netif_doip_print_info(void)
{
    drv_netif_doip_t *driver = bsp_netif_doip_get_driver();
    drv_netif_doip_status_info_t status;
    
    printf("\r\n=== DoIP Network Interface Information ===\r\n");
    printf("Selected Interface: %s\r\n", bsp_netif_doip_get_type_string());
    printf("Interface Type ID: %d\r\n", bsp_netif_doip_get_type());
    
    // Check hardware availability
    void *hw_info = NULL;
    bool hw_available = bsp_netif_doip_hardware_available(hw_info);
    printf("Hardware Available: %s\r\n", hw_available ? "YES" : "NO");
    
    // Get driver status if available
    if (driver != NULL && driver->is_init) {
        drv_netif_doip_status_t result = bsp_netif_doip_get_comprehensive_status(&status, true);
        
        if (result == DRV_NETIF_DOIP_STATUS_OK) {
            printf("\r\n--- Driver Status ---\r\n");
            printf("State: %d (%s)\r\n", status.state, 
                   (status.state == DRV_NETIF_DOIP_STATE_CONFIGURED) ? "Configured" :
                   (status.state == DRV_NETIF_DOIP_STATE_LINK_UP) ? "Link Up" :
                   (status.state == DRV_NETIF_DOIP_STATE_LINK_DOWN) ? "Link Down" :
                   (status.state == DRV_NETIF_DOIP_STATE_INITIALIZED) ? "Initialized" :
                   (status.state == DRV_NETIF_DOIP_STATE_UNINITIALIZED) ? "Uninitialized" : "Unknown");
            
            printf("Initialized: %s, Enabled: %s\r\n", 
                   status.is_initialized ? "YES" : "NO",
                   status.is_enabled ? "YES" : "NO");
            
            printf("Link Up: %s", status.link_up ? "YES" : "NO");
            if (status.link_up) {
                printf(" (%d Mbps, %s duplex)", status.link_speed, 
                       status.full_duplex ? "Full" : "Half");
            }
            printf("\r\n");
            
            printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                   status.mac_address[0], status.mac_address[1], status.mac_address[2],
                   status.mac_address[3], status.mac_address[4], status.mac_address[5]);
            
            if (status.ip_address != 0) {
                printf("IP Address: %d.%d.%d.%d\r\n",
                       (status.ip_address >> 0) & 0xFF,
                       (status.ip_address >> 8) & 0xFF,
                       (status.ip_address >> 16) & 0xFF,
                       (status.ip_address >> 24) & 0xFF);
                
                printf("Netmask: %d.%d.%d.%d\r\n",
                       (status.netmask >> 0) & 0xFF,
                       (status.netmask >> 8) & 0xFF,
                       (status.netmask >> 16) & 0xFF,
                       (status.netmask >> 24) & 0xFF);
                
                if (status.gateway != 0) {
                    printf("Gateway: %d.%d.%d.%d\r\n",
                           (status.gateway >> 0) & 0xFF,
                           (status.gateway >> 8) & 0xFF,
                           (status.gateway >> 16) & 0xFF,
                           (status.gateway >> 24) & 0xFF);
                }
                
                printf("DHCP: %s\r\n", status.dhcp_enabled ? "Enabled" : "Disabled");
            } else {
                printf("IP Address: Not configured\r\n");
            }
            
            printf("\r\n--- Statistics ---\r\n");
            printf("Packets: TX=%lu, RX=%lu\r\n", status.packets_sent, status.packets_received);
            printf("Bytes: TX=%lu, RX=%lu\r\n", status.bytes_sent, status.bytes_received);
            printf("Errors: TX=%lu, RX=%lu\r\n", status.errors_tx, status.errors_rx);
            printf("Link State Changes: %lu\r\n", status.link_state_changes);
        } else {
            printf("Driver Status: Not available (error %d)\r\n", result);
        }
    } else {
        printf("Driver Status: Not initialized\r\n");
    }
    
    printf("=== End Network Interface Information ===\r\n\r\n");
}