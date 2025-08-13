/*
 * Simple stub header for ethernetif.h
 * This provides the basic interface definitions without complex dependencies
 */

#ifndef ETHERNETIF_H
#define ETHERNETIF_H

#include "lwip/err.h"
#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

// Basic error codes
#define ERR_OK    0
#define ERR_ERR  -1

// Function declarations
err_t ethernetif_init(struct netif *netif);
err_t ethernetif_input(struct netif *netif);

// KSZ8851SNL network interface functions
err_t ethif_ksz8851snl_init(struct netif *netif);
void ethif_ksz8851snl_input(struct netif *netif);

// GMAC network interface functions
err_t ethif_gmac_init(struct netif *netif);
err_t mac_low_level_output(struct netif *netif, struct pbuf *p);
void mac_low_level_init(struct netif *netif);
void ethernetif_mac_input(struct netif *netif);

#ifdef __cplusplus
}
#endif

#endif /* ETHERNETIF_H */ 