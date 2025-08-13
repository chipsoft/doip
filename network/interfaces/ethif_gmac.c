/*
 * GMAC LwIP network interface implementation
 * Provides full integration with LWIP stack and FreeRTOS for built-in GMAC
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

// GMAC driver includes
#include "bsp_ethernet.h"
#include "driver_ethernet.h"

// External GMAC driver instance
extern drv_eth_t eth_communication;

#include <string.h>

// Network interface constants
#define GMAC_MTU                    1500
#define GMAC_MAX_PACKET_SIZE        1518
#define GMAC_TASK_STACK_SIZE        512
#define GMAC_TASK_PRIORITY          (configMAX_PRIORITIES - 1)
#define GMAC_HOSTNAME               "gmac"

// Network interface context structure
typedef struct {
    drv_eth_t *gmac_driver;
    struct netif *netif;
    sys_sem_t rx_sem;
    sys_thread_t task_id;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
} ethif_gmac_context_t;

// Global context instance
static ethif_gmac_context_t gmac_netif_context;

// Forward declarations
static void gmac_task(void *pvParameters);
static void gmac_rx_callback(void);
static uint16_t pbuf_to_buffer(struct pbuf *p, uint8_t *buffer, uint16_t buffer_size);
static struct pbuf *buffer_to_pbuf(const uint8_t *buffer, uint16_t length);

/**
 * Initialize the GMAC network interface
 * This function should be passed as a parameter to netif_add().
 */
err_t ethif_gmac_init(struct netif *netif)
{
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    
    printf("[GMAC] Initializing network interface\\r\\n");
    
    // Get driver instance from netif state
    ethif_gmac_context_t *context = &gmac_netif_context;
    context->gmac_driver = &eth_communication;
    context->netif = netif;
    context->rx_packets = 0;
    context->tx_packets = 0;
    context->rx_errors = 0;
    context->tx_errors = 0;
    
    // Set netif context
    netif->state = context;
    
    // Set MAC hardware address length
    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    
    // Set MAC hardware address (will be set by driver)
    netif->hwaddr[0] = 0x00;
    netif->hwaddr[1] = 0x04;
    netif->hwaddr[2] = 0x25;
    netif->hwaddr[3] = 0x12;
    netif->hwaddr[4] = 0x34;
    netif->hwaddr[5] = 0x56;
    
    // Maximum transfer unit
    netif->mtu = GMAC_MTU;
    
    // Device capabilities
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    
#if LWIP_NETIF_HOSTNAME
    // Initialize interface hostname
    netif->hostname = GMAC_HOSTNAME;
#endif
    
    // Set interface name
    netif->name[0] = 'g'; // gmac
    netif->name[1] = 'm';
    
    // Set netif output functions
    netif->output = etharp_output;
    netif->linkoutput = mac_low_level_output;
    
    // Initialize GMAC hardware
    mac_low_level_init(netif);
    
    // Create receive semaphore
    if (sys_sem_new(&context->rx_sem, 0) != ERR_OK) {
        printf("[GMAC] Failed to create RX semaphore\\r\\n");
        return ERR_MEM;
    }
    
    // Create packet processing task
    context->task_id = sys_thread_new("GMAC", gmac_task, context, 
                                     GMAC_TASK_STACK_SIZE, 
                                     GMAC_TASK_PRIORITY);
    if (context->task_id.thread_handle == NULL) {
        printf("[GMAC] Failed to create packet processing task\\r\\n");
        sys_sem_free(&context->rx_sem);
        return ERR_MEM;
    }
    
    printf("[GMAC] Network interface initialized successfully\\r\\n");
    return ERR_OK;
}

/**
 * Low level output function for GMAC
 * This function is called by the TCP/IP stack when an IP packet is ready to be sent.
 */
err_t mac_low_level_output(struct netif *netif, struct pbuf *p)
{
    ethif_gmac_context_t *context = (ethif_gmac_context_t *)netif->state;
    static uint8_t tx_buffer[GMAC_MAX_PACKET_SIZE];
    
    LWIP_ASSERT("context != NULL", context != NULL);
    LWIP_ASSERT("context->gmac_driver != NULL", context->gmac_driver != NULL);
    
    if (p->tot_len > GMAC_MAX_PACKET_SIZE) {
        printf("[GMAC] Packet too large: %d bytes\\r\\n", p->tot_len);
        context->tx_errors++;
        return ERR_BUF;
    }
    
    // Convert pbuf chain to contiguous buffer
    uint16_t length = pbuf_to_buffer(p, tx_buffer, sizeof(tx_buffer));
    
    // Send packet via GMAC driver
    drv_ethernet_status_t status = hw_ethernet_send_packet(context->gmac_driver, 
                                                          tx_buffer, length);
    
    if (status == DRV_ETHERNET_STATUS_OK) {
        context->tx_packets++;
        LINK_STATS_INC(link.xmit);
        return ERR_OK;
    } else {
        context->tx_errors++;
        LINK_STATS_INC(link.err);
        printf("[GMAC] Transmit failed: %d\\r\\n", status);
        return ERR_IF;
    }
}

/**
 * Low level initialization function for GMAC hardware
 */
void mac_low_level_init(struct netif *netif)
{
    ethif_gmac_context_t *context = (ethif_gmac_context_t *)netif->state;
    
    printf("[GMAC] Initializing hardware\\r\\n");
    
    // Initialize GMAC driver
    drv_ethernet_status_t status = hw_ethernet_init(context->gmac_driver);
    if (status != DRV_ETHERNET_STATUS_OK) {
        printf("[GMAC] Failed to initialize driver: %d\\r\\n", status);
        return;
    }
    
    // Register RX callback
    status = hw_ethernet_register_callback(context->gmac_driver, 
                                          DRV_ETHERNET_CB_RX_COMPLETE, 
                                          gmac_rx_callback);
    if (status != DRV_ETHERNET_STATUS_OK) {
        printf("[GMAC] Failed to register RX callback: %d\\r\\n", status);
        return;
    }
    
    // Enable the driver
    status = hw_ethernet_enable(context->gmac_driver);
    if (status != DRV_ETHERNET_STATUS_OK) {
        printf("[GMAC] Failed to enable driver: %d\\r\\n", status);
        return;
    }
    
    // Get MAC address from driver and update netif
    drv_ethernet_config_t config;
    if (hw_ethernet_get_config(context->gmac_driver, &config) == DRV_ETHERNET_STATUS_OK) {
        memcpy(netif->hwaddr, config.mac_addr, ETHARP_HWADDR_LEN);
    }
    
    printf("[GMAC] Hardware initialized successfully\\r\\n");
}

/**
 * Process received packets and pass them to the LWIP stack
 */
void ethernetif_mac_input(struct netif *netif)
{
    ethif_gmac_context_t *context = (ethif_gmac_context_t *)netif->state;
    static uint8_t rx_buffer[GMAC_MAX_PACKET_SIZE];
    uint16_t length;
    
    LWIP_ASSERT("context != NULL", context != NULL);
    LWIP_ASSERT("context->gmac_driver != NULL", context->gmac_driver != NULL);
    
    // Check if packets are available
    bool rx_available = false;
    drv_ethernet_status_t status = hw_ethernet_check_rx_available(context->gmac_driver, &rx_available);
    
    if (status != DRV_ETHERNET_STATUS_OK || !rx_available) {
        return;
    }
    
    // Process all available packets
    while (rx_available) {
        length = sizeof(rx_buffer);
        status = hw_ethernet_receive_packet(context->gmac_driver, rx_buffer, &length);
        
        if (status == DRV_ETHERNET_STATUS_OK && length > 0) {
            // Create pbuf from received data
            struct pbuf *p = buffer_to_pbuf(rx_buffer, length);
            
            if (p != NULL) {
                // Pass packet to LWIP stack
                if (netif->input(p, netif) != ERR_OK) {
                    printf("[GMAC] Input error, dropping packet\\r\\n");
                    pbuf_free(p);
                    context->rx_errors++;
                    LINK_STATS_INC(link.err);
                } else {
                    context->rx_packets++;
                    LINK_STATS_INC(link.recv);
                }
            } else {
                printf("[GMAC] Failed to allocate pbuf for packet\\r\\n");
                context->rx_errors++;
                LINK_STATS_INC(link.memerr);
            }
        } else {
            printf("[GMAC] Receive failed: %d\\r\\n", status);
            context->rx_errors++;
            break;
        }
        
        // Check for more packets
        hw_ethernet_check_rx_available(context->gmac_driver, &rx_available);
    }
}

/**
 * GMAC packet processing task
 * Waits for interrupt notifications and processes received packets
 */
static void gmac_task(void *pvParameters)
{
    ethif_gmac_context_t *context = (ethif_gmac_context_t *)pvParameters;
    
    printf("[GMAC] Packet processing task started\\r\\n");
    
    while (1) {
        // Wait for RX interrupt notification
        if (sys_arch_sem_wait(&context->rx_sem, portMAX_DELAY) == SYS_ARCH_TIMEOUT) {
            continue;
        }
        
        // Process all available packets
        ethernetif_mac_input(context->netif);
    }
}

/**
 * RX callback function called from interrupt context
 * Signals the packet processing task that packets are available
 */
static void gmac_rx_callback(void)
{
    // Signal packet processing task from ISR
    sys_sem_signal(&gmac_netif_context.rx_sem);
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