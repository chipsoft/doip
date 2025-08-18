/**
 * @file bsp_netif_doip_ksz.h
 * @brief KSZ8851SNL Network Interface BSP Header for DoIP Communications
 * 
 * @details This header provides the KSZ8851SNL-specific network interface
 * implementation for DoIP communications. It integrates with the universal
 * network interface driver to provide hardware abstraction for the
 * KSZ8851SNL SPI-to-Ethernet controller.
 * 
 * @author  SAME54 DoIP Project
 * @date    2024
 * @version 1.0
 * 
 * @copyright MIT License - Free to use, modify, and distribute
 */

#ifndef _BSP_NETIF_DOIP_KSZ_H_
#define _BSP_NETIF_DOIP_KSZ_H_

#include "driver_netif_doip.h"
#include "driver_ksz8851snl.h"

/**
 * @defgroup ksz_netif_constants KSZ8851SNL Network Interface Constants
 * @brief Configuration constants specific to KSZ8851SNL network interface
 * @{
 */
#define KSZ_NETIF_DEFAULT_MAC_ADDR  {0x00, 0x00, 0x00, 0x00, 0x20, 0x76}  /**< Default MAC address */
#define KSZ_NETIF_MAX_LINK_RETRIES  5                                      /**< Maximum link establishment retries */
#define KSZ_NETIF_LINK_CHECK_INTERVAL_MS  500                             /**< Link status check interval */
#define KSZ_NETIF_INIT_DELAY_MS     100                                    /**< Initialization delay */
#define KSZ_NETIF_RESET_DELAY_MS    50                                     /**< Reset delay */
/** @} */

/**
 * @defgroup ksz_netif_lwip LwIP Integration Constants
 * @brief Constants for lwIP network interface integration
 * @{
 */
#define KSZ_NETIF_NAME0             'k'    /**< Network interface name first character */
#define KSZ_NETIF_NAME1             's'    /**< Network interface name second character */
#define KSZ_NETIF_MTU               1500   /**< Maximum Transfer Unit */
/** @} */

/**
 * @brief KSZ8851SNL network interface configuration extension
 * @details Additional configuration parameters specific to KSZ8851SNL
 */
typedef struct {
    /** @name SPI Configuration
     * SPI interface configuration for KSZ8851SNL communication
     * @{
     */
    uint32_t spi_clock_speed;              /**< SPI clock speed in Hz */
    uint8_t spi_mode;                      /**< SPI mode (0-3) */
    bool spi_cs_active_low;                /**< Chip select active low */
    /** @} */
    
    /** @name Interrupt Configuration  
     * Interrupt handling configuration
     * @{
     */
    bool interrupt_enabled;                /**< Enable interrupt processing */
    uint8_t interrupt_priority;            /**< Interrupt priority level */
    /** @} */
    
    /** @name Power Management
     * Power management and low-power mode configuration
     * @{
     */
    bool power_down_enabled;               /**< Enable power-down mode when idle */
    uint32_t power_down_delay_ms;          /**< Delay before entering power-down */
    /** @} */
} ksz_netif_config_extension_t;

/**
 * @brief KSZ8851SNL network interface status extension
 * @details Additional status information specific to KSZ8851SNL
 */
typedef struct {
    /** @name Hardware Status
     * Hardware-specific status information
     * @{
     */
    uint16_t chip_id;                      /**< KSZ8851SNL chip ID */
    uint16_t revision_id;                  /**< Chip revision ID */
    bool spi_communication_ok;             /**< SPI communication status */
    bool chip_detected;                    /**< Chip detection status */
    /** @} */
    
    /** @name Link Status Details
     * Detailed link status information
     * @{
     */
    bool auto_neg_complete;                /**< Auto-negotiation completion */
    bool link_partner_capable;             /**< Link partner capability */
    uint16_t link_partner_advertised;      /**< Link partner advertised abilities */
    /** @} */
    
    /** @name Statistics
     * KSZ8851SNL specific statistics
     * @{
     */
    uint32_t spi_transactions;             /**< Total SPI transactions */
    uint32_t interrupt_count;              /**< Total interrupts processed */
    uint32_t link_state_changes;           /**< Link state change count */
    uint32_t tx_underruns;                 /**< Transmission underruns */
    uint32_t rx_overruns;                  /**< Reception overruns */
    /** @} */
} ksz_netif_status_extension_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ksz_netif_api KSZ8851SNL Network Interface API
 * @brief KSZ8851SNL specific network interface functions
 * @{
 */

/**
 * @brief Global KSZ8851SNL network interface driver instance
 * @details This is the main driver instance for KSZ8851SNL network interface
 */
extern drv_netif_doip_t netif_doip_ksz_0;

/**
 * @brief Initialize KSZ8851SNL network interface for DoIP
 * @param extended_config Extended configuration specific to KSZ8851SNL (can be NULL for defaults)
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 * @note This function sets up both the KSZ8851SNL hardware and lwIP interface
 */
drv_netif_doip_status_t bsp_netif_doip_ksz_init_extended(const ksz_netif_config_extension_t *extended_config);

/**
 * @brief Get KSZ8851SNL extended status information
 * @param extended_status Pointer to extended status structure to populate
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t bsp_netif_doip_ksz_get_extended_status(ksz_netif_status_extension_t *extended_status);

/**
 * @brief Force KSZ8851SNL hardware reset
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 * @note This performs a complete hardware reset and reinitialization
 */
drv_netif_doip_status_t bsp_netif_doip_ksz_hardware_reset(void);

/**
 * @brief Get underlying KSZ8851SNL driver instance
 * @return Pointer to KSZ8851SNL driver instance, NULL on error
 * @note For advanced users requiring direct access to KSZ8851SNL driver
 */
drv_ksz8851snl_t* bsp_netif_doip_ksz_get_driver(void);

/**
 * @brief Set KSZ8851SNL power mode
 * @param power_down True to enter power-down mode, false to wake up
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 */
drv_netif_doip_status_t bsp_netif_doip_ksz_set_power_mode(bool power_down);

/**
 * @brief Perform KSZ8851SNL diagnostics
 * @return DRV_NETIF_DOIP_STATUS_OK if all diagnostics pass, error code otherwise
 * @note Runs comprehensive hardware diagnostics including register tests
 */
drv_netif_doip_status_t bsp_netif_doip_ksz_run_diagnostics(void);

/**
 * @brief Create default extended configuration for KSZ8851SNL
 * @param extended_config Pointer to extended configuration structure to initialize
 * @note Fills structure with recommended default values for automotive applications
 */
void bsp_netif_doip_ksz_create_default_extended_config(ksz_netif_config_extension_t *extended_config);

/**
 * @brief Check if KSZ8851SNL hardware is available and responsive
 * @param chip_id Pointer to store detected chip ID (can be NULL)
 * @return true if hardware is available, false otherwise
 * @note Performs basic communication test without full initialization
 */
bool bsp_netif_doip_ksz_hardware_available(uint16_t *chip_id);

/**
 * @brief Register KSZ8851SNL specific event callback
 * @param callback Callback function for KSZ8851SNL events
 * @return DRV_NETIF_DOIP_STATUS_OK on success, error code otherwise
 * @note This is in addition to standard network interface callbacks
 */
drv_netif_doip_status_t bsp_netif_doip_ksz_register_hw_callback(drv_ksz8851snl_callback_t callback);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // _BSP_NETIF_DOIP_KSZ_H_