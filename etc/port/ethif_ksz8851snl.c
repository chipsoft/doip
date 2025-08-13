/**
 * \file ethif_ksz8851snl.c
 *
 * \brief LwIP network interface for KSZ8851SNL Ethernet PHY
 *
 * This file provides the LwIP network interface implementation for the
 * KSZ8851SNL Ethernet PHY controller via SPI interface.
 */

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/ethip6.h"
#include "netif/etharp.h"
#include "netif/ethernet.h"

#include "bsp_ksz8851snl.h"  
#include "printf.h"
#include <string.h>

#ifdef USE_KSZ8851SNL_INTERFACE

/* Define those to better describe your network interface. */
#define IFNAME0 'k'
#define IFNAME1 's'

/**
 * Maximum transfer unit for KSZ8851SNL
 */
#define KSZ8851SNL_MTU        1500

/**
 * Helper struct to hold private data used to operate your ethernet interface.
 */
struct ksz8851snl_if {
    struct eth_addr *ethaddr;
    /* Add whatever per-interface state that is needed here. */
    bool link_up;
};

/* Forward declarations. */
static void ksz8851snl_input(struct netif *netif);

/**
 * In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void low_level_init(struct netif *netif)
{
    struct ksz8851snl_if *ksz8851snl_if = netif->state;
    
    printf("[KSZ8851SNL] Low level init\r\n");
    
    /* set MAC hardware address length */
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    
    /* set MAC hardware address */
    drv_ksz8851snl_config_t config = {
        .mac_addr = {0x00, 0x04, 0x25, 0x1C, 0xA0, 0x02}, // Default MAC
        .auto_negotiation = true,
        .link_speed = 100,
        .full_duplex = true
    };
    
    /* Copy MAC address to netif */
    memcpy(netif->hwaddr, config.mac_addr, ETHARP_HWADDR_LEN);
    
    /* maximum transfer unit */
    netif->mtu = KSZ8851SNL_MTU;
    
    /* device capabilities */
    /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    
    /* KSZ8851SNL should already be initialized in main.c, just ensure it's enabled */
    drv_ksz8851snl_status_t status = hw_ksz8851snl_enable(&ksz8851snl_0);
    if (status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] Driver enable failed: %d\r\n", status);
        return;
    }
    
    ksz8851snl_if->link_up = true;
    printf("[KSZ8851SNL] Low level init completed\r\n");
}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the pbuf to send
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become available since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    struct pbuf *q;
    uint8_t *buffer;
    uint16_t total_len = 0;
    uint16_t offset = 0;
    drv_ksz8851snl_status_t status;
    
    printf("[KSZ8851SNL] Sending packet, length: %d\r\n", p->tot_len);
    
#if ETH_PAD_SIZE
    pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif
    
    /* Allocate buffer for the complete packet */
    buffer = (uint8_t *)mem_malloc(p->tot_len);
    if (buffer == NULL) {
        printf("[KSZ8851SNL] Failed to allocate buffer\r\n");
        LINK_STATS_INC(link.memerr);
        LINK_STATS_INC(link.drop);
        return ERR_MEM;
    }
    
    /* Copy data from pbuf chain to buffer */
    for (q = p; q != NULL; q = q->next) {
        memcpy(&buffer[offset], q->payload, q->len);
        offset += q->len;
        total_len += q->len;
    }
    
    /* Send the packet */
    status = hw_ksz8851snl_send_packet(&ksz8851snl_0, buffer, total_len);
    
    /* Free the allocated buffer */
    mem_free(buffer);
    
    if (status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] Send packet failed: %d\r\n", status);
        LINK_STATS_INC(link.err);
        LINK_STATS_INC(link.drop);
        return ERR_IF;
    }
    
    LINK_STATS_INC(link.xmit);
    
#if ETH_PAD_SIZE
    pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif
    
    printf("[KSZ8851SNL] Packet sent successfully\r\n");
    return ERR_OK;
}

/**
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         NULL on memory error
 */
static struct pbuf *low_level_input(struct netif *netif)
{
    struct pbuf *p, *q;
    uint16_t len = 1518; // Maximum Ethernet frame size
    uint8_t *buffer;
    uint16_t actual_len = 0;
    uint16_t offset = 0;
    drv_ksz8851snl_status_t status;
    bool rx_available = false;
    
    /* Check if there is a packet available */
    status = hw_ksz8851snl_check_rx_available(&ksz8851snl_0, &rx_available);
    if (status != DRV_KSZ8851SNL_STATUS_OK || !rx_available) {
        return NULL;
    }
    
    /* Allocate buffer for maximum frame size */
    buffer = (uint8_t *)mem_malloc(len);
    if (buffer == NULL) {
        printf("[KSZ8851SNL] Failed to allocate receive buffer\r\n");
        LINK_STATS_INC(link.memerr);
        LINK_STATS_INC(link.drop);
        return NULL;
    }
    
    /* Receive the packet */
    actual_len = len;
    status = hw_ksz8851snl_receive_packet(&ksz8851snl_0, buffer, &actual_len);
    if (status != DRV_KSZ8851SNL_STATUS_OK || actual_len == 0) {
        mem_free(buffer);
        if (status != DRV_KSZ8851SNL_STATUS_OK) {
            printf("[KSZ8851SNL] Receive packet failed: %d\r\n", status);
            LINK_STATS_INC(link.err);
        }
        return NULL;
    }
    
    printf("[KSZ8851SNL] Received packet, length: %d\r\n", actual_len);
    
    len = actual_len;
    
#if ETH_PAD_SIZE
    len += ETH_PAD_SIZE; /* allow room for Ethernet padding */
#endif
    
    /* We allocate a pbuf chain of pbufs from the pool. */
    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    
    if (p != NULL) {
#if ETH_PAD_SIZE
        pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif
        
        /* Copy data from buffer to pbuf chain */
        for (q = p; q != NULL; q = q->next) {
            uint16_t copy_len = (actual_len - offset) > q->len ? q->len : (actual_len - offset);
            memcpy(q->payload, &buffer[offset], copy_len);
            offset += copy_len;
            if (offset >= actual_len) {
                break;
            }
        }
        
#if ETH_PAD_SIZE
        pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif
        
        LINK_STATS_INC(link.recv);
    } else {
        printf("[KSZ8851SNL] Failed to allocate pbuf\r\n");
        LINK_STATS_INC(link.memerr);
        LINK_STATS_INC(link.drop);
    }
    
    mem_free(buffer);
    return p;
}

/**
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
static void ksz8851snl_input(struct netif *netif)
{
    struct eth_hdr *ethhdr;
    struct pbuf *p;
    
    /* move received packet into a new pbuf */
    p = low_level_input(netif);
    /* no packet could be read, silently ignore this */
    if (p == NULL) return;
    
    /* points to packet payload, which starts with an Ethernet header */
    ethhdr = p->payload;
    
    switch (htons(ethhdr->type)) {
        /* IP or ARP packet? */
        case ETHTYPE_IP:
        case ETHTYPE_IPV6:
        case ETHTYPE_ARP:
            /* full packet send to tcpip_thread to process */
            if (netif->input(p, netif) != ERR_OK) {
                LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
                pbuf_free(p);
                p = NULL;
            }
            break;
            
        default:
            pbuf_free(p);
            p = NULL;
            break;
    }
}

/**
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
err_t ethif_ksz8851snl_init(struct netif *netif)
{
    struct ksz8851snl_if *ksz8851snl_if;
    
    printf("[KSZ8851SNL] Ethernet interface init\r\n");
    
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    
    ksz8851snl_if = mem_malloc(sizeof(struct ksz8851snl_if));
    if (ksz8851snl_if == NULL) {
        LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_init: out of memory\n"));
        return ERR_MEM;
    }
    
#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = "ksz8851snl";
#endif /* LWIP_NETIF_HOSTNAME */
    
    /*
     * Initialize the snmp variables and counters inside the struct netif.
     * The last argument should be replaced with your link speed, in units
     * of bits per second.
     */
    MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, 100000000);
    
    netif->state = ksz8851snl_if;
    netif->name[0] = IFNAME0;
    netif->name[1] = IFNAME1;
    /* We directly use etharp_output() here to save a function call.
     * You can instead declare your own function an call etharp_output()
     * from it if you have to do some checks before sending (e.g. if link
     * is available...) */
    netif->output = etharp_output;
#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */
    netif->linkoutput = low_level_output;
    
    ksz8851snl_if->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);
    
    /* initialize the hardware */
    low_level_init(netif);
    
    printf("[KSZ8851SNL] Ethernet interface init completed\r\n");
    return ERR_OK;
}

/**
 * This function should be called periodically to check for new packets
 * and process them.
 *
 * @param netif the network interface to check
 */
void ethif_ksz8851snl_input(struct netif *netif)
{
    ksz8851snl_input(netif);
}

#endif /* USE_KSZ8851SNL_INTERFACE */