/*
 * KSZ8851SNL LwIP network interface implementation
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

// KSZ8851SNL driver includes
#include "bsp_ksz8851snl.h"
#include "driver_ksz8851snl.h"

#include <string.h>

// Network interface constants
#define KSZ8851SNL_MTU                    1500
#define KSZ8851SNL_MAX_PACKET_SIZE        1518
#define KSZ8851SNL_TASK_STACK_SIZE        512
#define KSZ8851SNL_TASK_PRIORITY          (configMAX_PRIORITIES - 1)
#define KSZ8851SNL_HOSTNAME               "ksz8851snl"

// Network interface context structure
typedef struct {
    drv_ksz8851snl_t *ksz_driver;
    struct netif *netif;
    sys_sem_t rx_sem;
    sys_thread_t task_id;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
} ethif_ksz8851snl_context_t;

// Global context instance
static ethif_ksz8851snl_context_t ksz_netif_context;

// Forward declarations
static void ksz8851snl_task(void *pvParameters);
static err_t ethif_ksz8851snl_linkoutput(struct netif *netif, struct pbuf *p);
static void ksz8851snl_rx_callback(void);
static uint16_t pbuf_to_buffer(struct pbuf *p, uint8_t *buffer, uint16_t buffer_size);
static struct pbuf *buffer_to_pbuf(const uint8_t *buffer, uint16_t length);

/**
 * Initialize the KSZ8851SNL network interface
 * This function should be passed as a parameter to netif_add().
 */
err_t ethif_ksz8851snl_init(struct netif *netif)
{
    LWIP_ASSERT("netif != NULL", (netif != NULL));
    
    printf("[KSZ8851SNL] Initializing network interface\r\n");
    
    // Get driver instance from netif state
    ethif_ksz8851snl_context_t *context = &ksz_netif_context;
    context->ksz_driver = (drv_ksz8851snl_t *)netif->state;
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
    netif->hwaddr[5] = 0x56;
    
    // Maximum transfer unit
    netif->mtu = KSZ8851SNL_MTU;
    
    // Device capabilities
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    
#if LWIP_NETIF_HOSTNAME
    // Initialize interface hostname
    netif->hostname = KSZ8851SNL_HOSTNAME;
#endif
    
    // Set interface name
    netif->name[0] = 'k';
    netif->name[1] = 's';
    
    // Set netif output functions
    netif->output = etharp_output;
    netif->linkoutput = ethif_ksz8851snl_linkoutput;
    
    // Initialize KSZ8851SNL driver
    drv_ksz8851snl_config_t config = {
        .mac_addr = {netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2], 
                     netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]},
        .auto_negotiation = true,
        .link_speed = 100,
        .full_duplex = true
    };
    
    drv_ksz8851snl_status_t status = hw_ksz8851snl_init(context->ksz_driver, &config);
    if (status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] Failed to initialize driver: %d\r\n", status);
        return ERR_IF;
    }
    
    // Register RX callback
    status = hw_ksz8851snl_register_callback(context->ksz_driver, 
                                            DRV_KSZ8851SNL_CB_RX_COMPLETE, 
                                            ksz8851snl_rx_callback);
    if (status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] Failed to register RX callback: %d\r\n", status);
        return ERR_IF;
    }
    
    // Enable the driver
    status = hw_ksz8851snl_enable(context->ksz_driver);
    if (status != DRV_KSZ8851SNL_STATUS_OK) {
        printf("[KSZ8851SNL] Failed to enable driver: %d\r\n", status);
        return ERR_IF;
    }
    
    // Create receive semaphore
    if (sys_sem_new(&context->rx_sem, 0) != ERR_OK) {
        printf("[KSZ8851SNL] Failed to create RX semaphore\r\n");
        return ERR_MEM;
    }
    
    // Create packet processing task
    context->task_id = sys_thread_new("KSZ8851SNL", ksz8851snl_task, context, 
                                     KSZ8851SNL_TASK_STACK_SIZE, 
                                     KSZ8851SNL_TASK_PRIORITY);
    if (context->task_id.thread_handle == NULL) {
        printf("[KSZ8851SNL] Failed to create packet processing task\r\n");
        sys_sem_free(&context->rx_sem);
        return ERR_MEM;
    }
    
    printf("[KSZ8851SNL] Network interface initialized successfully\r\n");
    return ERR_OK;
}

/**
 * Low level output function for KSZ8851SNL
 * This function is called by the TCP/IP stack when an IP packet is ready to be sent.
 */
static err_t ethif_ksz8851snl_linkoutput(struct netif *netif, struct pbuf *p)
{
    ethif_ksz8851snl_context_t *context = (ethif_ksz8851snl_context_t *)netif->state;
    static uint8_t tx_buffer[KSZ8851SNL_MAX_PACKET_SIZE];
    
    LWIP_ASSERT("context != NULL", context != NULL);
    LWIP_ASSERT("context->ksz_driver != NULL", context->ksz_driver != NULL);
    
    if (p->tot_len > KSZ8851SNL_MAX_PACKET_SIZE) {
        printf("[KSZ8851SNL] Packet too large: %d bytes\r\n", p->tot_len);
        context->tx_errors++;
        return ERR_BUF;
    }
    
    // Convert pbuf chain to contiguous buffer
    uint16_t length = pbuf_to_buffer(p, tx_buffer, sizeof(tx_buffer));
    
    // Send packet via KSZ8851SNL driver
    drv_ksz8851snl_status_t status = hw_ksz8851snl_send_packet(context->ksz_driver, 
                                                              tx_buffer, length);
    
    if (status == DRV_KSZ8851SNL_STATUS_OK) {
        context->tx_packets++;
        LINK_STATS_INC(link.xmit);
        return ERR_OK;
    } else {
        context->tx_errors++;
        LINK_STATS_INC(link.err);
        printf("[KSZ8851SNL] Transmit failed: %d\r\n", status);
        return ERR_IF;
    }
}

/**
 * Process received packets and pass them to the LWIP stack
 */
void ethif_ksz8851snl_input(struct netif *netif)
{
    ethif_ksz8851snl_context_t *context = (ethif_ksz8851snl_context_t *)netif->state;
    static uint8_t rx_buffer[KSZ8851SNL_MAX_PACKET_SIZE];
    uint16_t length;
    
    LWIP_ASSERT("context != NULL", context != NULL);
    LWIP_ASSERT("context->ksz_driver != NULL", context->ksz_driver != NULL);
    
    // Check if packets are available
    bool rx_available = false;
    drv_ksz8851snl_status_t status = hw_ksz8851snl_check_rx_available(context->ksz_driver, &rx_available);
    
    if (status != DRV_KSZ8851SNL_STATUS_OK || !rx_available) {
        return;
    }
    
    // Process all available packets
    while (rx_available) {
        length = sizeof(rx_buffer);
        status = hw_ksz8851snl_receive_packet(context->ksz_driver, rx_buffer, &length);
        
        if (status == DRV_KSZ8851SNL_STATUS_OK && length > 0) {
            // Create pbuf from received data
            struct pbuf *p = buffer_to_pbuf(rx_buffer, length);
            
            if (p != NULL) {
                // Pass packet to LWIP stack
                if (netif->input(p, netif) != ERR_OK) {
                    printf("[KSZ8851SNL] Input error, dropping packet\r\n");
                    pbuf_free(p);
                    context->rx_errors++;
                    LINK_STATS_INC(link.err);
                } else {
                    context->rx_packets++;
                    LINK_STATS_INC(link.recv);
                }
            } else {
                printf("[KSZ8851SNL] Failed to allocate pbuf for packet\r\n");
                context->rx_errors++;
                LINK_STATS_INC(link.memerr);
            }
        } else {
            printf("[KSZ8851SNL] Receive failed: %d\r\n", status);
            context->rx_errors++;
            break;
        }
        
        // Check for more packets
        hw_ksz8851snl_check_rx_available(context->ksz_driver, &rx_available);
    }
}

/**
 * KSZ8851SNL packet processing task
 * Waits for interrupt notifications and processes received packets
 */
static void ksz8851snl_task(void *pvParameters)
{
    ethif_ksz8851snl_context_t *context = (ethif_ksz8851snl_context_t *)pvParameters;
    
    printf("[KSZ8851SNL] Packet processing task started\r\n");
    
    while (1) {
        // Wait for RX interrupt notification
        if (sys_arch_sem_wait(&context->rx_sem, portMAX_DELAY) == SYS_ARCH_TIMEOUT) {
            continue;
        }
        
        // Process all available packets
        ethif_ksz8851snl_input(context->netif);
    }
}

/**
 * RX callback function called from interrupt context
 * Signals the packet processing task that packets are available
 */
static void ksz8851snl_rx_callback(void)
{
    // Signal packet processing task from ISR
    sys_sem_signal(&ksz_netif_context.rx_sem);
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