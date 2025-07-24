/**
 * \file network_events.h
 *
 * \brief Network event logging for lwIP stack
 *
 * Provides comprehensive logging for lwIP network events including:
 * - lwIP stack initialization
 * - DHCP state changes and IP address assignment
 * - Link up/down events
 * - Network interface status changes
 *
 * Uses SEGGER RTT printf for real-time debug output.
 */

#ifndef NETWORK_EVENTS_H
#define NETWORK_EVENTS_H

#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Initialize network event logging system
 *
 * Sets up callbacks and initializes logging infrastructure.
 * Should be called during network stack initialization.
 */
void network_events_init(void);

/**
 * \brief Log lwIP stack initialization
 *
 * \param[in] status Initialization status (ERR_OK on success)
 */
void log_lwip_init(err_t status);

/**
 * \brief Log DHCP state changes
 *
 * \param[in] netif Network interface
 * \param[in] old_state Previous DHCP state
 * \param[in] new_state New DHCP state
 */
void log_dhcp_state_change(struct netif *netif, u8_t old_state, u8_t new_state);

/**
 * \brief Log DHCP IP address assignment
 *
 * \param[in] netif Network interface
 * \param[in] ip_addr Assigned IP address
 * \param[in] netmask Network mask
 * \param[in] gateway Gateway address
 */
void log_dhcp_ip_assigned(struct netif *netif, ip_addr_t *ip_addr, 
                         ip_addr_t *netmask, ip_addr_t *gateway);

/**
 * \brief Log DHCP lease renewal
 *
 * \param[in] netif Network interface
 * \param[in] lease_time Lease time in seconds
 */
void log_dhcp_lease_renewal(struct netif *netif, u32_t lease_time);

/**
 * \brief Log DHCP timeout or failure
 *
 * \param[in] netif Network interface
 * \param[in] error_type Type of DHCP error
 */
void log_dhcp_error(struct netif *netif, const char *error_type);

/**
 * \brief Log physical link status changes
 *
 * \param[in] netif Network interface
 * \param[in] link_up true if link is up, false if down
 */
void log_link_status_change(struct netif *netif, bool link_up);

/**
 * \brief Log network interface status changes
 *
 * \param[in] netif Network interface
 * \param[in] is_up true if interface is up, false if down
 */
void log_netif_status_change(struct netif *netif, bool is_up);

/**
 * \brief Log Ethernet MAC address
 *
 * \param[in] netif Network interface
 */
void log_mac_address(struct netif *netif);

/**
 * \brief Log network configuration summary
 *
 * \param[in] netif Network interface
 */
void log_network_config(struct netif *netif);

/**
 * \brief Network interface status callback for lwIP
 *
 * Callback function registered with netif_set_status_callback()
 *
 * \param[in] netif Network interface that changed status
 */
void netif_status_callback(struct netif *netif);

/**
 * \brief Network interface link callback for lwIP
 *
 * Callback function registered with netif_set_link_callback()
 *
 * \param[in] netif Network interface that changed link status
 */
void netif_link_callback(struct netif *netif);

/**
 * \brief Convert DHCP state to string
 *
 * \param[in] state DHCP state value
 * \return String representation of DHCP state
 */
const char* dhcp_state_to_string(u8_t state);

/**
 * \brief Convert IP address to string
 *
 * \param[in] addr IP address structure
 * \param[out] buf Buffer to store string (minimum 16 chars)
 * \return Pointer to buffer
 */
char* ip_addr_to_string(ip_addr_t *addr, char *buf);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_EVENTS_H */