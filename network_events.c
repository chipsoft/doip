/**
 * \file network_events.c
 *
 * \brief Network event logging implementation for lwIP stack
 *
 * Provides comprehensive real-time logging for lwIP network events using
 * SEGGER RTT printf output for debugging and monitoring.
 */

#include "network_events.h"
#include "printf.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include <string.h>
#include <stdbool.h>

/* Static variables for state tracking */
static bool network_events_initialized = false;
static u8_t last_dhcp_state[NETIF_MAX_HWADDR_LEN] = {0};

/**
 * \brief Initialize network event logging system
 */
void network_events_init(void)
{
    if (!network_events_initialized) {
        printf("\r\n=== Network Events Logging Initialized ===\r\n");
        memset(last_dhcp_state, 0, sizeof(last_dhcp_state));
        network_events_initialized = true;
    }
}

/**
 * \brief Log lwIP stack initialization
 */
void log_lwip_init(err_t status)
{
    printf("\r\n[LWIP] Stack initialization: %s\r\n", 
           (status == ERR_OK) ? "SUCCESS" : "FAILED");
    
    if (status == ERR_OK) {
        printf("[LWIP] TCP/IP stack ready\r\n");
    } else {
        printf("[LWIP] Initialization error code: %d\r\n", status);
    }
}

/**
 * \brief Convert DHCP state to string
 */
const char* dhcp_state_to_string(u8_t state)
{
    switch (state) {
        case DHCP_OFF:        return "OFF";
        case DHCP_REQUESTING: return "REQUESTING";
        case DHCP_INIT:       return "INIT";
        case DHCP_REBOOTING:  return "REBOOTING";
        case DHCP_REBINDING:  return "REBINDING";
        case DHCP_RENEWING:   return "RENEWING";
        case DHCP_SELECTING:  return "SELECTING";
        case DHCP_INFORMING:  return "INFORMING";
        case DHCP_CHECKING:   return "CHECKING";
        case DHCP_PERMANENT:  return "PERMANENT";
        case DHCP_BOUND:      return "BOUND";
        case DHCP_BACKING_OFF: return "BACKING_OFF";
        default:              return "UNKNOWN";
    }
}

/**
 * \brief Convert IP address to string
 */
char* ip_addr_to_string(ip_addr_t *addr, char *buf)
{
    if (addr == NULL || buf == NULL) {
        return "0.0.0.0";
    }
    
    u32_t ip = addr->addr;
    sprintf(buf, "%d.%d.%d.%d", 
            (int)(ip & 0xFF),
            (int)((ip >> 8) & 0xFF),
            (int)((ip >> 16) & 0xFF),
            (int)((ip >> 24) & 0xFF));
    
    return buf;
}

/**
 * \brief Log DHCP state changes
 */
void log_dhcp_state_change(struct netif *netif, u8_t old_state, u8_t new_state)
{
    if (old_state != new_state) {
        printf("[DHCP] State transition: %s -> %s (netif: %c%c%d)\r\n",
               dhcp_state_to_string(old_state),
               dhcp_state_to_string(new_state),
               netif->name[0], netif->name[1], netif->num);
    }
}

/**
 * \brief Log DHCP IP address assignment
 */
void log_dhcp_ip_assigned(struct netif *netif, ip_addr_t *ip_addr, 
                         ip_addr_t *netmask, ip_addr_t *gateway)
{
    char ip_str[16], mask_str[16], gw_str[16];
    
    printf("\r\n[DHCP] IP Configuration Assigned:\r\n");
    printf("  Interface: %c%c%d\r\n", netif->name[0], netif->name[1], netif->num);
    printf("  IP Address: %s\r\n", ip_addr_to_string(ip_addr, ip_str));
    printf("  Subnet Mask: %s\r\n", ip_addr_to_string(netmask, mask_str));
    printf("  Gateway: %s\r\n", ip_addr_to_string(gateway, gw_str));
}

/**
 * \brief Log DHCP lease renewal
 */
void log_dhcp_lease_renewal(struct netif *netif, u32_t lease_time)
{
    printf("[DHCP] Lease renewed for %lu seconds (netif: %c%c%d)\r\n",
           lease_time, netif->name[0], netif->name[1], netif->num);
}

/**
 * \brief Log DHCP timeout or failure
 */
void log_dhcp_error(struct netif *netif, const char *error_type)
{
    printf("[DHCP] ERROR: %s (netif: %c%c%d)\r\n",
           error_type, netif->name[0], netif->name[1], netif->num);
}

/**
 * \brief Log physical link status changes
 */
void log_link_status_change(struct netif *netif, bool link_up)
{
    printf("\r\n[LINK] Physical link %s (netif: %c%c%d)\r\n",
           link_up ? "UP" : "DOWN",
           netif->name[0], netif->name[1], netif->num);
           
    if (link_up) {
        printf("[LINK] Ethernet connection established\r\n");
        log_mac_address(netif);
    } else {
        printf("[LINK] Ethernet connection lost\r\n");
    }
}

/**
 * \brief Log network interface status changes
 */
void log_netif_status_change(struct netif *netif, bool is_up)
{
    printf("[NETIF] Interface %s (netif: %c%c%d)\r\n",
           is_up ? "UP" : "DOWN",
           netif->name[0], netif->name[1], netif->num);
           
    if (is_up) {
        log_network_config(netif);
    }
}

/**
 * \brief Log Ethernet MAC address
 */
void log_mac_address(struct netif *netif)
{
    if (netif != NULL && netif->hwaddr_len == 6) {
        printf("[MAC] Address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
               netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2],
               netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]);
    }
}

/**
 * \brief Log network configuration summary
 */
void log_network_config(struct netif *netif)
{
    char ip_str[16], mask_str[16], gw_str[16];
    
    if (netif != NULL) {
        printf("\r\n[CONFIG] Network Configuration Summary:\r\n");
        printf("  Interface: %c%c%d\r\n", netif->name[0], netif->name[1], netif->num);
        printf("  Status: %s\r\n", netif_is_up(netif) ? "UP" : "DOWN");
        printf("  Link: %s\r\n", netif_is_link_up(netif) ? "UP" : "DOWN");
        printf("  IP Address: %s\r\n", ip_addr_to_string(&netif->ip_addr, ip_str));
        printf("  Subnet Mask: %s\r\n", ip_addr_to_string(&netif->netmask, mask_str));
        printf("  Gateway: %s\r\n", ip_addr_to_string(&netif->gw, gw_str));
        printf("  MTU: %d bytes\r\n", netif->mtu);
        log_mac_address(netif);
        
        /* Log DHCP status if enabled */
        if (netif->dhcp != NULL) {
            printf("  DHCP: %s\r\n", dhcp_state_to_string(netif->dhcp->state));
            if (netif->dhcp->state == DHCP_BOUND) {
                printf("  Lease Time: %lu seconds\r\n", netif->dhcp->offered_t0_lease);
                printf("  Renewal Time: %lu seconds\r\n", netif->dhcp->offered_t1_renew);
                printf("  Rebind Time: %lu seconds\r\n", netif->dhcp->offered_t2_rebind);
            }
        } else {
            printf("  DHCP: Disabled (Static IP)\r\n");
        }
        printf("\r\n");
    }
}

/**
 * \brief Network interface status callback for lwIP
 */
void netif_status_callback(struct netif *netif)
{
    if (netif != NULL) {
        bool is_up = netif_is_up(netif);
        log_netif_status_change(netif, is_up);
        
        /* Check for DHCP state changes */
        if (netif->dhcp != NULL) {
            u8_t current_state = netif->dhcp->state;
            u8_t previous_state = last_dhcp_state[netif->num % NETIF_MAX_HWADDR_LEN];
            
            if (current_state != previous_state) {
                log_dhcp_state_change(netif, previous_state, current_state);
                last_dhcp_state[netif->num % NETIF_MAX_HWADDR_LEN] = current_state;
                
                /* Log IP assignment when DHCP reaches BOUND state */
                if (current_state == DHCP_BOUND) {
                    log_dhcp_ip_assigned(netif, &netif->ip_addr, 
                                       &netif->netmask, &netif->gw);
                    
                    if (netif->dhcp->offered_t0_lease > 0) {
                        log_dhcp_lease_renewal(netif, netif->dhcp->offered_t0_lease);
                    }
                }
                
                /* Log DHCP errors */
                if (current_state == DHCP_BACKING_OFF) {
                    log_dhcp_error(netif, "DHCP server not responding");
                } else if (previous_state == DHCP_BOUND && current_state == DHCP_RENEWING) {
                    printf("[DHCP] Starting lease renewal process\r\n");
                } else if (previous_state == DHCP_RENEWING && current_state == DHCP_REBINDING) {
                    log_dhcp_error(netif, "Lease renewal failed, trying rebind");
                }
            }
        }
    }
}

/**
 * \brief Network interface link callback for lwIP
 */
void netif_link_callback(struct netif *netif)
{
    if (netif != NULL) {
        bool link_up = netif_is_link_up(netif);
        log_link_status_change(netif, link_up);
        
        if (!link_up) {
            /* Link down - DHCP will need to restart when link comes back */
            if (netif->dhcp != NULL && netif->dhcp->state != DHCP_OFF) {
                printf("[DHCP] Link down - DHCP state will reset\r\n");
            }
        } else {
            /* Link up - DHCP may start automatically */
            printf("[LINK] Ready for network configuration\r\n");
        }
    }
}