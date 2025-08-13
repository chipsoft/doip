/**
 * \file ethif_ksz8851snl.h
 *
 * \brief LwIP network interface for KSZ8851SNL Ethernet PHY
 */

#ifndef ETHIF_KSZ8851SNL_H
#define ETHIF_KSZ8851SNL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/netif.h"
#include "lwip/err.h"

/**
 * \brief Initialize the KSZ8851SNL network interface
 *
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t ethif_ksz8851snl_init(struct netif *netif);

/**
 * \brief Process incoming packets from KSZ8851SNL
 *
 * This function should be called periodically to check for new packets
 * and process them.
 *
 * @param netif the network interface to check
 */
void ethif_ksz8851snl_input(struct netif *netif);

#ifdef __cplusplus
}
#endif

#endif /* ETHIF_KSZ8851SNL_H */