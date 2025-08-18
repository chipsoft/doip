/**
 * @file bsp_netif_doip.h
 * @brief Network Interface Selection Header for DoIP Communications
 * 
 * @details This header provides the network interface selection mechanism
 * for DoIP communications. It allows compile-time selection between different
 * network interface implementations (GMAC, KSZ8851SNL, etc.) based on
 * configuration defines.
 * 
 * @author  SAME54 DoIP Project
 * @date    2024
 * @version 1.0
 * 
 * @copyright MIT License - Free to use, modify, and distribute
 */

#ifndef _BSP_NETIF_DOIP_H_
#define _BSP_NETIF_DOIP_H_

#include "driver_netif_doip.h"

/**
 * @defgroup netif_selection Network Interface Selection
 * @brief Compile-time network interface selection for DoIP
 * @{
 */

// Include appropriate network interface implementations based on configuration
#ifdef USE_KSZ8851SNL_INTERFACE
    #include "bsp_netif_doip_ksz.h"
    #define DOIP_NETIF_TYPE_STRING "KSZ8851SNL"
    #define DOIP_NETIF_SELECTED_TYPE DRV_NETIF_DOIP_TYPE_KSZ8851SNL
#else
    // Default to GMAC interface (stub for now)
    #define DOIP_NETIF_TYPE_STRING "GMAC"
    #define DOIP_NETIF_SELECTED_TYPE DRV_NETIF_DOIP_TYPE_GMAC
    
    // For now, create a stub GMAC interface that points to KSZ
    // This will be replaced with actual GMAC implementation later
    #include "bsp_netif_doip_ksz.h"
#endif

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup netif_selection_api Network Interface Selection API
 * @brief API functions for network interface selection and management
 * @{
 */

/**
 * @brief Get the selected network interface driver instance
 * @return Pointer to the selected network interface driver
 * @note Returns the appropriate driver based on compile-time configuration
 */
static inline drv_netif_doip_t* bsp_netif_doip_get_driver(void)
{
#ifdef USE_KSZ8851SNL_INTERFACE
    return &netif_doip_ksz_0;
#else
    // For now, return KSZ driver as default
    // This will be replaced with GMAC driver when implemented
    return &netif_doip_ksz_0;
#endif
}

/**
 * @brief Get the selected network interface type
 * @return Network interface type enumeration value
 */
static inline drv_netif_doip_type_t bsp_netif_doip_get_type(void)
{
    return DOIP_NETIF_SELECTED_TYPE;
}

/**
 * @brief Get the selected network interface type as string
 * @return Network interface type as human-readable string
 */
static inline const char* bsp_netif_doip_get_type_string(void)
{
    return DOIP_NETIF_TYPE_STRING;
}

/**
 * @brief Initialize the selected network interface with default configuration
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 * @note Uses default configuration appropriate for the selected interface type
 */
drv_netif_doip_status_t bsp_netif_doip_init_default(void);

/**
 * @brief Initialize the selected network interface with custom configuration
 * @param config Pointer to network interface configuration
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t bsp_netif_doip_init_with_config(const drv_netif_doip_config_t *config);

/**
 * @brief Check if the selected network interface hardware is available
 * @param hw_info Pointer to store hardware information (can be NULL)
 * @return true if hardware is available and responsive, false otherwise
 */
bool bsp_netif_doip_hardware_available(void *hw_info);

/**
 * @brief Get comprehensive status of the selected network interface
 * @param status Pointer to status structure to populate
 * @param extended_info Include extended hardware-specific information
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t bsp_netif_doip_get_comprehensive_status(drv_netif_doip_status_info_t *status, bool extended_info);

/**
 * @brief Print network interface information and status
 * @note Outputs detailed information about the selected interface to console
 */
void bsp_netif_doip_print_info(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // _BSP_NETIF_DOIP_H_