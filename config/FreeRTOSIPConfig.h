/*
 * FreeRTOS+TCP V3.x configuration for SAME54 DoIP implementation
 */

#ifndef FREERTOS_IP_CONFIG_H
#define FREERTOS_IP_CONFIG_H

/* Include FreeRTOS configuration */
#include "FreeRTOSConfig.h"

/* Define byte order for network operations */
#define ipconfigBYTE_ORDER                       pdFREERTOS_LITTLE_ENDIAN

/* Forward declarations will be handled by FreeRTOS-Plus-TCP */

/*-----------------------------------------------------------*/
/* Basic Network Configuration                               */
/*-----------------------------------------------------------*/

/* Set to 1 to enable IPv4, 0 to disable */
#define ipconfigUSE_IPv4                         ( 1 )

/* Set to 1 to enable IPv6, 0 to disable */
#define ipconfigUSE_IPv6                         ( 0 )

/* Set to 1 to enable TCP, 0 to disable */
#define ipconfigUSE_TCP                          ( 1 )

/* Set to 1 to enable UDP, 0 to disable */
#define ipconfigUSE_UDP                          ( 1 )

/* Set to 1 to enable DHCP, 0 for static IP */
#define ipconfigUSE_DHCP                         ( 1 )

/* DHCP timeout in ticks */
#define ipconfigMAXIMUM_DISCOVER_TX_PERIOD       ( pdMS_TO_TICKS( 30000UL ) )

/*-----------------------------------------------------------*/
/* Memory and Buffer Configuration                           */
/*-----------------------------------------------------------*/

/* Total number of network buffer descriptors */
#define ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS   ( 45 )

/* Size of the buffer payload (must be multiple of 32) */
#define ipconfigNETWORK_MTU                      ( 1500 )

/* Size of one network buffer including descriptor */
#define ipconfigNETWORK_BUFFER_SIZE              ( ipconfigNETWORK_MTU + ipSIZE_OF_ETH_HEADER + ipBUFFER_PADDING )

/* Use heap_5.c for memory management */
#define ipconfigUSE_HEAP_ALLOCATION_TYPE_1       ( 0 )
#define ipconfigUSE_HEAP_ALLOCATION_TYPE_2       ( 1 )

/*-----------------------------------------------------------*/
/* TCP Configuration                                         */
/*-----------------------------------------------------------*/

/* TCP Maximum Segment Size */
#define ipconfigTCP_MSS                          ( 1460 )

/* TCP window size (should be multiple of MSS) */
#define ipconfigTCP_RX_BUFFER_LENGTH             ( 4 * ipconfigTCP_MSS )
#define ipconfigTCP_TX_BUFFER_LENGTH             ( 4 * ipconfigTCP_MSS )

/* Maximum number of TCP sockets that can exist simultaneously */
#define ipconfigMAX_TCP_SOCKETS                  ( 8 )

/* TCP keep alive settings */
#define ipconfigTCP_KEEP_ALIVE                   ( 1 )
#define ipconfigTCP_KEEP_ALIVE_INTERVAL          ( 1000 )  /* Ticks */

/* TCP hang protection - close sockets that don't respond */
#define ipconfigTCP_HANG_PROTECTION              ( 1 )
#define ipconfigTCP_HANG_PROTECTION_TIME         ( 30 )

/* Time to wait for graceful socket closure */
#define ipconfigTCP_FIN_WAIT2_TIME               ( 10000 )

/*-----------------------------------------------------------*/
/* UDP Configuration                                         */
/*-----------------------------------------------------------*/

/* Maximum number of UDP sockets */
#define ipconfigMAX_UDP_SOCKETS                  ( 8 )

/* Pass UDP packets with unknown port to application */
#define ipconfigUDP_PASS_ZERO_CHECKSUM_PACKETS   ( 0 )

/*-----------------------------------------------------------*/
/* DNS Configuration                                         */
/*-----------------------------------------------------------*/

/* Enable DNS resolution */
#define ipconfigUSE_DNS                          ( 1 )

/* DNS request timeout in ticks */
#define ipconfigDNS_REQUEST_ATTEMPTS             ( 5 )

/* Cache DNS responses */
#define ipconfigUSE_DNS_CACHE                    ( 1 )
#define ipconfigDNS_CACHE_NAME_LENGTH            ( 32 )
#define ipconfigDNS_CACHE_ENTRIES                ( 4 )

/*-----------------------------------------------------------*/
/* Task and Queue Configuration                              */
/*-----------------------------------------------------------*/

/* Priority of the IP task */
#define ipconfigIP_TASK_PRIORITY                 ( configMAX_PRIORITIES - 2 )

/* Stack size for the IP task */
#define ipconfigIP_TASK_STACK_SIZE_WORDS         ( 1024 )

/* Size of the queue used to pass events to the IP task */
#define ipconfigEVENT_QUEUE_LENGTH               ( 60 )

/* Use separate task for TCP */
#define ipconfigUSE_TCP_WIN                      ( 1 )

/*-----------------------------------------------------------*/
/* Driver Interface Configuration                            */
/*-----------------------------------------------------------*/

/* Driver can handle multiple packets per interrupt */
#define ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM   ( 0 )
#define ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM   ( 0 )

/* Allow socket send without copying data */
#define ipconfigZERO_COPY_TX_DRIVER              ( 0 )
#define ipconfigZERO_COPY_RX_DRIVER              ( 0 )

/*-----------------------------------------------------------*/
/* Debugging and Statistics                                  */
/*-----------------------------------------------------------*/

/* Include application defined hook/callback functions */
#define ipconfigUSE_NETWORK_EVENT_HOOK           ( 1 )

/* Include debug printf statements */
#define ipconfigHAS_DEBUG_PRINTF                 ( 1 )

/* Include printf for IP stack events */
#define ipconfigHAS_PRINTF                       ( 1 )

/* Generate checksum in software */
#define ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM   ( 0 )
#define ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM   ( 0 )

/*-----------------------------------------------------------*/
/* Socket Configuration                                      */
/*-----------------------------------------------------------*/

/* Support Berkeley sockets interface */
#define ipconfigSOCKET_HAS_USER_SEMAPHORE        ( 1 )
#define ipconfigSOCKET_HAS_USER_WAKE_CALLBACK    ( 1 )

/* Socket select functionality */
#define ipconfigSUPPORT_SELECT_FUNCTION          ( 1 )

/* Support signals for socket operations */
#define ipconfigSUPPORT_SIGNALS                  ( 0 )

/* Allow sockets to receive broadcast packets */
#define ipconfigETHERNET_DRIVER_FILTERS_PACKETS  ( 0 )

/*-----------------------------------------------------------*/
/* Optimization Settings                                     */
/*-----------------------------------------------------------*/

/* Allow packet buffers to be cached */
#define ipconfigBUFFER_ALLOC_USES_MALLOC         ( 0 )

/* Check for stack overflow in network tasks */
#define ipconfigCHECK_IP_QUEUE_SPACE             ( 1 )

/* Include code for network down event */
#define ipconfigSUPPORT_OUTGOING_PINGS           ( 1 )

/* Use linked RX messages to improve performance */
#define ipconfigUSE_LINKED_RX_MESSAGES           ( 0 )

/*-----------------------------------------------------------*/
/* DoIP Specific Optimizations                               */
/*-----------------------------------------------------------*/

/* DoIP uses both TCP and UDP on port 13400 */
/* Ensure adequate socket resources */
#define ipconfigSOCK_DEFAULT_RECEIVE_BLOCK_TIME  ( pdMS_TO_TICKS( 5000UL ) )
#define ipconfigSOCK_DEFAULT_SEND_BLOCK_TIME     ( pdMS_TO_TICKS( 5000UL ) )

/* Reduce ARP resolution time for faster connections */
#define ipconfigARP_CACHE_ENTRIES                ( 10 )
#define ipconfigMAX_ARP_RETRANSMISSIONS          ( 5 )
#define ipconfigMAX_ARP_AGE                      ( 150 )

/*-----------------------------------------------------------*/
/* Application Hooks                                         */
/*-----------------------------------------------------------*/

/* Hook function prototypes */
/* eIPCallbackEvent_t is defined by FreeRTOS-Plus-TCP in FreeRTOS_IP.h */
extern uint32_t ulApplicationGetTimestamp( void );
extern uint32_t ulApplicationGetNextSequenceNumber( uint32_t ulSourceAddress,
                                                    uint16_t usSourcePort, 
                                                    uint32_t ulDestinationAddress,
                                                    uint16_t usDestinationPort );

/* Memory allocation hooks */
extern void *pvPortMalloc( size_t xWantedSize );
extern void vPortFree( void *pv );

/* Debug printf function */
extern int printf( const char *pcFormat, ... );

/*-----------------------------------------------------------*/
/* Assertions and Error Handling                            */
/*-----------------------------------------------------------*/

/* Error handling macros */
#define ipconfigASSERT( x ) configASSERT( x )

/* Define FreeRTOS debug printf */
#define FreeRTOS_debug_printf(X) printf X

#endif /* FREERTOS_IP_CONFIG_H */