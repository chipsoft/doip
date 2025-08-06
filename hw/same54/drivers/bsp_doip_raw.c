#include "driver_doip.h"
// Include lwIP headers first to avoid ERR_TIMEOUT conflict with ASF4
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip4_frag.h"
#include "lwip/ip4.h"
#include "eth_ipstack_main.h"
#include "utils_assert.h"
#include "printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "semphr.h"
#include <string.h>
#include <stdlib.h>


// Raw lwIP configuration
#define DOIP_STREAM_BUFFER_SIZE         (4096)
#define DOIP_STREAM_TRIGGER_LEVEL       (1)


// Hardware context structure
typedef struct {
    // State management
    drv_doip_state_t current_state;
    TaskHandle_t client_task_handle;
    
    // Vehicle information
    drv_doip_vehicle_info_t current_vehicle;
    
    // Raw lwIP resources
    struct tcp_pcb *tcp_pcb;
    struct udp_pcb *udp_pcb;
    StreamBufferHandle_t stream_buffer;
    StreamBufferHandle_t udp_stream_buffer;
    SemaphoreHandle_t connected_sem;
    SemaphoreHandle_t send_sem;
    SemaphoreHandle_t discovery_sem;
    
    // Discovery response tracking
    uint32_t discovered_ip_address;
    
    // Multi-vehicle discovery cache
    drv_doip_vehicle_info_t discovered_vehicles[8];
    uint8_t discovered_vehicle_count;
    uint8_t current_vehicle_index;
    
    // System monitoring data
    drv_doip_system_monitoring_t monitoring_data;
    
    // Callbacks
    drv_doip_callback_t callbacks[5]; // Array for different callback types
} drv_doip_hw_context_t;

// Static hardware context
static drv_doip_hw_context_t drv_doip_hw_context_0 = {
    .current_state = DRV_DOIP_STATE_IDLE,
    .client_task_handle = NULL,
    .tcp_pcb = NULL,
    .udp_pcb = NULL,
    .stream_buffer = NULL,
    .udp_stream_buffer = NULL,
    .connected_sem = NULL,
    .send_sem = NULL,
    .discovery_sem = NULL,
    .discovered_ip_address = 0,
    .discovered_vehicle_count = 0,
    .current_vehicle_index = 0,
};

// Helper functions
static drv_doip_status_t doip_send_routing_activation_request(drv_doip_hw_context_t *context);
static drv_doip_status_t doip_send_diagnostic_message(drv_doip_hw_context_t *context, uint8_t service_id, uint16_t data_id);

// Alive check functions
static drv_doip_status_t __attribute__((unused)) doip_send_alive_check_request(drv_doip_hw_context_t *context);
static drv_doip_status_t __attribute__((unused)) doip_handle_alive_check_response(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length);
static drv_doip_status_t __attribute__((unused)) doip_handle_alive_check_request(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length);

// Raw lwIP UDP callback functions
static void doip_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port)
{
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)arg;
    
    if (p == NULL) {
        return;
    }
    
    printf("DOIP Client: Raw UDP received %d bytes from %u.%u.%u.%u:%d\r\n", 
           p->tot_len,
           (unsigned)(ip4_addr_get_u32(addr) & 0xFF),
           (unsigned)((ip4_addr_get_u32(addr) >> 8) & 0xFF),
           (unsigned)((ip4_addr_get_u32(addr) >> 16) & 0xFF),
           (unsigned)((ip4_addr_get_u32(addr) >> 24) & 0xFF),
           port);
    
    // Store source IP address for later use
    context->discovered_ip_address = ip4_addr_get_u32(addr);
    
    if (context->udp_stream_buffer != NULL && p->tot_len > 0) {
        // Copy pbuf data to stream buffer
        uint8_t *buffer = (uint8_t *)p->payload;
        if (p->len == p->tot_len) {
            // Single pbuf - direct copy
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            size_t sent = xStreamBufferSendFromISR(
                context->udp_stream_buffer,
                buffer,
                p->len,
                &xHigherPriorityTaskWoken
            );
            
            if (sent == p->len) {
                printf("DOIP Client: UDP data buffered successfully (%d bytes)\r\n", p->len);
                
                // Signal discovery completion
                if (context->discovery_sem != NULL) {
                    xSemaphoreGiveFromISR(context->discovery_sem, &xHigherPriorityTaskWoken);
                }
            } else {
                printf("DOIP Client: UDP buffer full, dropped %d bytes\r\n", p->len);
            }
            
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        } else {
            // Multiple pbufs - need to copy sequentially
            printf("DOIP Client: Multi-pbuf UDP packet - handling sequentially\r\n");
            struct pbuf *q;
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            
            for (q = p; q != NULL; q = q->next) {
                size_t sent = xStreamBufferSendFromISR(
                    context->udp_stream_buffer,
                    q->payload,
                    q->len,
                    &xHigherPriorityTaskWoken
                );
                
                if (sent != q->len) {
                    printf("DOIP Client: UDP buffer overflow during multi-pbuf copy\r\n");
                    break;
                }
            }
            
            if (context->discovery_sem != NULL) {
                xSemaphoreGiveFromISR(context->discovery_sem, &xHigherPriorityTaskWoken);
            }
            
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
    
    pbuf_free(p);
}

// Raw lwIP TCP callback functions
static err_t doip_tcp_connected(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)arg;
    
    printf("DOIP Client: Raw TCP connection callback - err=%d\r\n", err);
    
    if (err == ERR_OK) {
        printf("DOIP Client: Raw TCP connection established successfully\r\n");
        context->current_state = DRV_DOIP_STATE_CONNECTED;
        
        if (context->connected_sem != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(context->connected_sem, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } else {
        printf("DOIP Client: Raw TCP connection failed - err=%d\r\n", err);
        context->current_state = DRV_DOIP_STATE_ERROR;
    }
    
    return ERR_OK;
}

static err_t doip_tcp_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)arg;
    
    if (p == NULL) {
        printf("DOIP Client: Raw TCP connection closed by peer\r\n");
        context->current_state = DRV_DOIP_STATE_IDLE;
        return ERR_OK;
    }
    
    if (err != ERR_OK) {
        printf("DOIP Client: Raw TCP receive error - err=%d\r\n", err);
        pbuf_free(p);
        return err;
    }
    
    if (p->len > 0 && context->stream_buffer != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        size_t sent = xStreamBufferSendFromISR(
            context->stream_buffer,
            p->payload,
            p->len,
            &xHigherPriorityTaskWoken
        );
        
        if (sent == p->len) {
            tcp_recved(tpcb, p->len);
            printf("DOIP Client: Raw TCP received %d bytes, buffered and ACK sent\r\n", p->len);
        } else {
            printf("DOIP Client: Stream buffer full, dropped %d bytes (buffered only %d) - no ACK\r\n", p->len, sent);
        }
        
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    
    pbuf_free(p);
    return ERR_OK;
}

static err_t doip_tcp_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)arg;
    
    printf("DOIP Client: Raw TCP sent %d bytes acknowledged\r\n", len);
    
    if (context->send_sem != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(context->send_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    
    return ERR_OK;
}

static void doip_tcp_err(void *arg, err_t err)
{
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)arg;
    
    printf("DOIP Client: Raw TCP error callback - err=%d\r\n", err);
    
    context->tcp_pcb = NULL; // PCB is already freed by lwIP
    context->current_state = DRV_DOIP_STATE_ERROR;
    
    if (context->connected_sem != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(context->connected_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// Forward declarations of implementation functions
static drv_doip_status_t drv_doip_init_impl(const void *hw_context);
static drv_doip_status_t drv_doip_deinit_impl(const void *hw_context);
static drv_doip_status_t drv_doip_discover_vehicles_impl(const void *hw_context, drv_doip_vehicle_info_t *vehicle_info);
static drv_doip_status_t drv_doip_connect_to_vehicle_impl(const void *hw_context, const drv_doip_vehicle_info_t *vehicle_info);
static drv_doip_status_t drv_doip_disconnect_impl(const void *hw_context);
static drv_doip_status_t drv_doip_send_diagnostic_request_impl(const void *hw_context, uint8_t service_id, uint16_t data_id, uint8_t *response, size_t max_response_len, size_t *actual_len);

static drv_doip_state_t drv_doip_get_status_impl(const void *hw_context);
static drv_doip_status_t drv_doip_register_callback_impl(const void *hw_context, drv_doip_cb_type_t type, drv_doip_callback_t callback);


// Global driver instance
drv_doip_t doip_0 = {
    .is_init = false,
    .current_state = DRV_DOIP_STATE_IDLE,
    .hw_context = &drv_doip_hw_context_0,
    .init = drv_doip_init_impl,
    .deinit = drv_doip_deinit_impl,
    .discover_vehicles = drv_doip_discover_vehicles_impl,
    .connect_to_vehicle = drv_doip_connect_to_vehicle_impl,
    .disconnect = drv_doip_disconnect_impl,
    .send_diagnostic_request = drv_doip_send_diagnostic_request_impl,

    .get_status = drv_doip_get_status_impl,
    .register_callback = drv_doip_register_callback_impl,
};

// Implementation functions
static drv_doip_status_t drv_doip_init_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    printf("DOIP Client: Initializing raw lwIP resources\r\n");
    
    // Create stream buffer for received TCP data
    context->stream_buffer = xStreamBufferCreate(DOIP_STREAM_BUFFER_SIZE, DOIP_STREAM_TRIGGER_LEVEL);
    if (context->stream_buffer == NULL) {
        printf("DOIP Client: Failed to create TCP stream buffer\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Create stream buffer for received UDP data
    context->udp_stream_buffer = xStreamBufferCreate(DOIP_STREAM_BUFFER_SIZE, DOIP_STREAM_TRIGGER_LEVEL);
    if (context->udp_stream_buffer == NULL) {
        printf("DOIP Client: Failed to create UDP stream buffer\r\n");
        vStreamBufferDelete(context->stream_buffer);
        context->stream_buffer = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Create semaphores for synchronization
    context->connected_sem = xSemaphoreCreateBinary();
    if (context->connected_sem == NULL) {
        printf("DOIP Client: Failed to create connection semaphore\r\n");
        vStreamBufferDelete(context->udp_stream_buffer);
        context->udp_stream_buffer = NULL;
        vStreamBufferDelete(context->stream_buffer);
        context->stream_buffer = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    context->send_sem = xSemaphoreCreateBinary();
    if (context->send_sem == NULL) {
        printf("DOIP Client: Failed to create send semaphore\r\n");
        vSemaphoreDelete(context->connected_sem);
        context->connected_sem = NULL;
        vStreamBufferDelete(context->udp_stream_buffer);
        context->udp_stream_buffer = NULL;
        vStreamBufferDelete(context->stream_buffer);
        context->stream_buffer = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    context->discovery_sem = xSemaphoreCreateBinary();
    if (context->discovery_sem == NULL) {
        printf("DOIP Client: Failed to create discovery semaphore\r\n");
        vSemaphoreDelete(context->send_sem);
        context->send_sem = NULL;
        vSemaphoreDelete(context->connected_sem);
        context->connected_sem = NULL;
        vStreamBufferDelete(context->udp_stream_buffer);
        context->udp_stream_buffer = NULL;
        vStreamBufferDelete(context->stream_buffer);
        context->stream_buffer = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    

    
    // Initialize callbacks array
    memset(context->callbacks, 0, sizeof(context->callbacks));
    
    printf("DOIP Client: Raw lwIP resources initialized successfully\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    printf("DOIP Client: Cleaning up raw lwIP resources\r\n");
    
    // Close TCP connection
    if (context->tcp_pcb != NULL) {
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
    }
    
    // Close UDP connection
    if (context->udp_pcb != NULL) {
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
    }
    
    // Clean up resources
    if (context->stream_buffer != NULL) {
        vStreamBufferDelete(context->stream_buffer);
        context->stream_buffer = NULL;
    }
    
    if (context->udp_stream_buffer != NULL) {
        vStreamBufferDelete(context->udp_stream_buffer);
        context->udp_stream_buffer = NULL;
    }
    
    if (context->connected_sem != NULL) {
        vSemaphoreDelete(context->connected_sem);
        context->connected_sem = NULL;
    }
    
    if (context->send_sem != NULL) {
        vSemaphoreDelete(context->send_sem);
        context->send_sem = NULL;
    }
    
    if (context->discovery_sem != NULL) {
        vSemaphoreDelete(context->discovery_sem);
        context->discovery_sem = NULL;
    }
    
    context->current_state = DRV_DOIP_STATE_IDLE;
    return DRV_DOIP_STATUS_OK;
}


static drv_doip_status_t drv_doip_discover_vehicles_impl(const void *hw_context, drv_doip_vehicle_info_t *vehicle_info)
{
    ASSERT(hw_context != NULL);
    ASSERT(vehicle_info != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    doip_message_t request_msg, response_msg;
    uint8_t buffer[1024];
    err_t err;
    ip_addr_t broadcast_addr;
    struct pbuf *p;
    
    printf("DOIP Client: Discovering vehicles via raw UDP\r\n");
    context->current_state = DRV_DOIP_STATE_DISCOVERING;
    
    // Reset discovery cache if starting fresh discovery
    if (context->current_vehicle_index >= context->discovered_vehicle_count) {
        context->current_vehicle_index = 0;
        context->discovered_vehicle_count = 0;
        printf("DOIP Client: Starting fresh discovery cycle\r\n");
    } else {
        // We have cached vehicles, return the next one without sending new discovery
        printf("DOIP Client: Using cached discovery results (%d vehicles, returning index %d)\r\n",
               context->discovered_vehicle_count, context->current_vehicle_index);
        
        memcpy(vehicle_info, &context->discovered_vehicles[context->current_vehicle_index], sizeof(drv_doip_vehicle_info_t));
        context->current_vehicle_index++;
        
        // Store vehicle info in context
        memcpy(&context->current_vehicle, vehicle_info, sizeof(drv_doip_vehicle_info_t));
        context->current_state = DRV_DOIP_STATE_DISCOVERED;
        
        printf("DOIP Client: Vehicle discovered from cache\r\n");
        printf("  VIN: %s\r\n", vehicle_info->vin);
        printf("  Logical Address: 0x%04X\r\n", vehicle_info->logical_address);
        printf("  IP Address: %u.%u.%u.%u:%d\r\n",
               (unsigned)(vehicle_info->ip_address & 0xFF),
               (unsigned)((vehicle_info->ip_address >> 8) & 0xFF),
               (unsigned)((vehicle_info->ip_address >> 16) & 0xFF),
               (unsigned)((vehicle_info->ip_address >> 24) & 0xFF),
               vehicle_info->tcp_port);
        
        return DRV_DOIP_STATUS_OK;
    }
    
    // Create UDP PCB
    context->udp_pcb = udp_new();
    if (context->udp_pcb == NULL) {
        printf("DOIP Client: Failed to create UDP PCB\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: UDP PCB created successfully\r\n");
    
    // Set up UDP receive callback
    udp_recv(context->udp_pcb, doip_udp_recv, context);
    
    // Bind to local port (any port for sending)
    err = udp_bind(context->udp_pcb, IP_ADDR_ANY, 0);
    if (err != ERR_OK) {
        printf("DOIP Client: Failed to bind UDP PCB - err=%d\r\n", err);
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Clear UDP stream buffer
    xStreamBufferReset(context->udp_stream_buffer);
    
    // Prepare broadcast address
    IP4_ADDR(&broadcast_addr, 255, 255, 255, 255);
    
    // Create vehicle identification request using utility
    doip_utils_create_header(&request_msg, DOIP_VEHICLE_IDENTIFICATION_REQUEST, 0);
    
    // Convert message to buffer
    doip_utils_serialize_message(&request_msg, buffer);
    uint32_t message_len = DOIP_HEADER_SIZE + request_msg.payload_length;
    
    // Create pbuf for sending
    p = pbuf_alloc(PBUF_TRANSPORT, message_len, PBUF_RAM);
    if (p == NULL) {
        printf("DOIP Client: Failed to allocate pbuf for discovery\r\n");
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Copy data to pbuf
    memcpy(p->payload, buffer, message_len);
    
    // Send broadcast request
    err = udp_sendto(context->udp_pcb, p, &broadcast_addr, DOIP_UDP_DISCOVERY_PORT);
    pbuf_free(p);
    
    if (err != ERR_OK) {
        printf("DOIP Client: Failed to send discovery request - err=%d\r\n", err);
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Discovery request sent, waiting for response...\r\n");
    
    // Wait for first response with timeout
    if (xSemaphoreTake(context->discovery_sem, pdMS_TO_TICKS(DOIP_DISCOVERY_TIMEOUT_MS)) != pdTRUE) {
        printf("DOIP Client: Discovery timeout - no response received\r\n");
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_IDLE;
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    // Give additional time for multiple ECU responses to arrive
    printf("DOIP Client: First response received, waiting for additional ECU responses...\r\n");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second for all ECU responses
    
    // Read ALL responses from UDP stream buffer  
    size_t total_received = 0;
    size_t received;
    uint8_t temp_buffer[512];
    
    // Read all available data from stream buffer
    while ((received = xStreamBufferReceive(context->udp_stream_buffer, temp_buffer, sizeof(temp_buffer), pdMS_TO_TICKS(100))) > 0) {
        if (total_received + received <= sizeof(buffer)) {
            memcpy(buffer + total_received, temp_buffer, received);
            total_received += received;
            printf("DOIP Client: Read %d bytes from stream buffer (total: %d)\r\n", received, total_received);
        } else {
            printf("DOIP Client: Buffer overflow prevented - ignoring %d bytes\r\n", received);
            break;
        }
    }
    
    // Clean up UDP PCB
    udp_remove(context->udp_pcb);
    context->udp_pcb = NULL;
    
    if (total_received < DOIP_HEADER_SIZE) {
        printf("DOIP Client: Insufficient data received (%d bytes)\r\n", total_received);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Received total %d bytes response\r\n", total_received);
    
    // Parse multiple DOIP responses and return info for FIRST valid vehicle
    // (This maintains backward compatibility with existing single-vehicle API)
    
    // If this is the first discovery call, parse ALL vehicles from the buffer
    if (context->current_vehicle_index == 0) {
        context->discovered_vehicle_count = 0;
        
        // Parse all DOIP messages in the buffer
        size_t buffer_offset = 0;
        
        printf("DOIP Client: Parsing multiple DOIP responses from buffer (%d bytes total)\r\n", total_received);
        
        while (buffer_offset < total_received && context->discovered_vehicle_count < 8) {
            size_t remaining = total_received - buffer_offset;
            
            if (remaining < DOIP_HEADER_SIZE) {
                printf("DOIP Client: Insufficient remaining data for DOIP header\r\n");
                break;
            }
            
            // Parse response header using utility
            if (!doip_utils_parse_header(buffer + buffer_offset, remaining, &response_msg)) {
                printf("DOIP Client: Invalid discovery response header at offset %d\r\n", buffer_offset);
                // Try to find next DOIP header by looking for protocol version pattern
                buffer_offset++;
                continue;
            }
            
            printf("DOIP Client: Found DOIP message - Type: 0x%04X, Length: %lu\r\n", 
                   response_msg.payload_type, response_msg.payload_length);
            
            // Check if this is a vehicle identification response
            if (response_msg.payload_type == DOIP_VEHICLE_IDENTIFICATION_RESPONSE && 
                response_msg.payload_length >= 17 + 2 + 6) { // VIN + Logical Address + EID minimum
                
                // Extract vehicle information and store in cache
                drv_doip_vehicle_info_t *cached_vehicle = &context->discovered_vehicles[context->discovered_vehicle_count];
                
                // Parse VIN (17 bytes)
                memcpy(cached_vehicle->vin, response_msg.payload, 17);
                cached_vehicle->vin[17] = '\0';
                
                // Parse Logical Address (2 bytes)
                cached_vehicle->logical_address = (response_msg.payload[17] << 8) | response_msg.payload[18];
                
                // Parse Entity ID (6 bytes)  
                memcpy(cached_vehicle->entity_id, &response_msg.payload[19], 6);
                
                // Parse Group ID (6 bytes)
                if (response_msg.payload_length >= 17 + 2 + 6 + 6) {
                    memcpy(cached_vehicle->group_id, &response_msg.payload[25], 2); // Only first 2 bytes
                }
                
                // Set IP and port from discovery
                cached_vehicle->ip_address = context->discovered_ip_address;
                cached_vehicle->tcp_port = DOIP_TCP_DATA_PORT;
                
                context->discovered_vehicle_count++;
                printf("DOIP Client: Cached vehicle %d - VIN=%s, LA=0x%04X\r\n",
                       context->discovered_vehicle_count, cached_vehicle->vin, cached_vehicle->logical_address);
            }
            
            // Move to next message
            buffer_offset += DOIP_HEADER_SIZE + response_msg.payload_length;
        }
        
        printf("DOIP Client: Parsed and cached %d vehicles from discovery response\r\n", context->discovered_vehicle_count);
    }
    
    if (context->discovered_vehicle_count == 0) {
        printf("DOIP Client: No valid vehicle identification responses found\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Return the next vehicle from cache
    if (context->current_vehicle_index >= context->discovered_vehicle_count) {
        // Reset for next discovery cycle
        context->current_vehicle_index = 0;
        printf("DOIP Client: No more vehicles in cache, resetting index\r\n");
        context->current_state = DRV_DOIP_STATE_IDLE;
        return DRV_DOIP_STATUS_NO_VEHICLE;
    }
    
    // Copy vehicle info from cache
    memcpy(vehicle_info, &context->discovered_vehicles[context->current_vehicle_index], sizeof(drv_doip_vehicle_info_t));
    context->current_vehicle_index++;
    
    printf("DOIP Client: Returning cached vehicle %d/%d\r\n", context->current_vehicle_index, context->discovered_vehicle_count);
    
    // Store vehicle info in context
    memcpy(&context->current_vehicle, vehicle_info, sizeof(drv_doip_vehicle_info_t));
    context->current_state = DRV_DOIP_STATE_DISCOVERED;
    
    printf("DOIP Client: Vehicle discovered successfully\r\n");
    printf("  VIN: %s\r\n", vehicle_info->vin);
    printf("  Logical Address: 0x%04X\r\n", vehicle_info->logical_address);
    printf("  IP Address: %u.%u.%u.%u:%d\r\n", 
           (unsigned)(vehicle_info->ip_address & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 8) & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 16) & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 24) & 0xFF),
           vehicle_info->tcp_port);
    
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t doip_send_routing_activation_request(drv_doip_hw_context_t *context)
{
    uint8_t message_buffer[15]; // DOIP header (8) + routing activation payload (7)
    uint8_t *ptr = message_buffer;
    
    // Get target ECU logical address from current vehicle info
    uint16_t target_address = context->current_vehicle.logical_address;
    
    printf("DOIP Client: Sending routing activation request to ECU 0x%04X\r\n", target_address);
    
    // DOIP Header
    *ptr++ = DOIP_PROTOCOL_VERSION;          // Protocol version
    *ptr++ = DOIP_INVERSE_PROTOCOL_VERSION;  // Inverse protocol version
    *ptr++ = (DOIP_ROUTING_ACTIVATION_REQUEST >> 8) & 0xFF;  // Payload type high byte
    *ptr++ = DOIP_ROUTING_ACTIVATION_REQUEST & 0xFF;         // Payload type low byte
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x07; // Payload length (7 bytes)
    
    // Routing Activation Payload
    *ptr++ = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;  // Source address high byte
    *ptr++ = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;         // Source address low byte
    *ptr++ = 0x00;  // Activation type (default)
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; // Reserved
    
    // Send the message
    err_t err = tcp_write(context->tcp_pcb, message_buffer, sizeof(message_buffer), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_write failed - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    err = tcp_output(context->tcp_pcb);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_output failed - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Routing activation request sent to ECU 0x%04X (15 bytes)\r\n", target_address);
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_connect_to_vehicle_impl(const void *hw_context, const drv_doip_vehicle_info_t *vehicle_info)
{
    ASSERT(hw_context != NULL);
    ASSERT(vehicle_info != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    err_t err;
    ip_addr_t server_addr;
    uint32_t server_ip = vehicle_info->ip_address;
    uint16_t server_port = vehicle_info->tcp_port;
    
    printf("DOIP Client: Raw TCP connecting to %lu.%lu.%lu.%lu:%d\r\n", 
           server_ip & 0xFF, (server_ip >> 8) & 0xFF, 
           (server_ip >> 16) & 0xFF, (server_ip >> 24) & 0xFF, server_port);
    
    // Convert IP address
    IP4_ADDR(&server_addr, 
             server_ip & 0xFF,
             (server_ip >> 8) & 0xFF, 
             (server_ip >> 16) & 0xFF,
             (server_ip >> 24) & 0xFF);
    
    // Create new TCP PCB
    context->tcp_pcb = tcp_new();
    if (context->tcp_pcb == NULL) {
        printf("DOIP Client: Failed to create TCP PCB\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Set up callbacks
    tcp_arg(context->tcp_pcb, context);
    tcp_recv(context->tcp_pcb, doip_tcp_recv);
    tcp_sent(context->tcp_pcb, doip_tcp_sent);
    tcp_err(context->tcp_pcb, doip_tcp_err);
    
    // Connect to server
    context->current_state = DRV_DOIP_STATE_CONNECTING;
    err = tcp_connect(context->tcp_pcb, &server_addr, server_port, doip_tcp_connected);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_connect failed - err=%d\r\n", err);
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Wait for connection with timeout
    printf("DOIP Client: Waiting for raw TCP connection (timeout: %d ms)...\r\n", DOIP_TCP_TIMEOUT_MS);
    if (xSemaphoreTake(context->connected_sem, pdMS_TO_TICKS(DOIP_TCP_TIMEOUT_MS)) != pdTRUE) {
        printf("DOIP Client: [EXPECTED] Raw TCP connection timeout - no DOIP server running\r\n");
        printf("DOIP Client: This is normal for demonstration without actual DOIP server\r\n");
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    if (context->current_state != DRV_DOIP_STATE_CONNECTED) {
        printf("DOIP Client: Raw TCP connection failed\r\n");
        if (context->tcp_pcb != NULL) {
            tcp_close(context->tcp_pcb);
            context->tcp_pcb = NULL;
        }
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Store vehicle info
    memcpy(&context->current_vehicle, vehicle_info, sizeof(drv_doip_vehicle_info_t));
    
    printf("DOIP Client: Raw TCP connection established\r\n");
    
    // Send DOIP routing activation request
    printf("DOIP Client: Sending routing activation request...\r\n");
    if (doip_send_routing_activation_request(context) != DRV_DOIP_STATUS_OK) {
        printf("DOIP Client: Failed to send routing activation request\r\n");
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Wait for routing activation response
    printf("DOIP Client: Waiting for routing activation response...\r\n");
    uint8_t buffer[256];
    size_t received = xStreamBufferReceive(context->stream_buffer, buffer, 13, pdMS_TO_TICKS(DOIP_TCP_TIMEOUT_MS)); // Header (8) + payload (5)
    if (received < 13) {
        printf("DOIP Client: Failed to receive routing activation response (got %zu bytes)\r\n", received);
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    // Parse DOIP header
    uint8_t protocol_version = buffer[0];
    uint8_t inverse_protocol_version = buffer[1];
    uint16_t payload_type = (buffer[2] << 8) | buffer[3];
    uint32_t payload_length = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
    
    printf("DOIP Client: Routing activation response - Type: 0x%04X, Length: %lu bytes\r\n", 
           payload_type, payload_length);
    
    // Validate routing activation response
    if (protocol_version != DOIP_PROTOCOL_VERSION || 
        inverse_protocol_version != DOIP_INVERSE_PROTOCOL_VERSION ||
        payload_type != DOIP_ROUTING_ACTIVATION_RESPONSE ||
        payload_length != 5) {
        printf("DOIP Client: Invalid routing activation response\r\n");
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Check response code (last byte of payload)
    uint8_t response_code = buffer[12];
    if (response_code != 0x10) { // Success
        printf("DOIP Client: Routing activation failed with response code: 0x%02X\r\n", response_code);
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    context->current_state = DRV_DOIP_STATE_ACTIVATED;
    printf("DOIP Client: DOIP routing activation completed successfully\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_disconnect_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    printf("DOIP Client: Raw TCP disconnecting\r\n");
    
    if (context->tcp_pcb != NULL) {
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
    }
    
    context->current_state = DRV_DOIP_STATE_IDLE;
    
    // Clear stream buffer
    if (context->stream_buffer != NULL) {
        xStreamBufferReset(context->stream_buffer);
    }
    
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t doip_send_diagnostic_message(drv_doip_hw_context_t *context, uint8_t service_id, uint16_t data_id)
{
    uint8_t message_buffer[15]; // DOIP header (8) + diagnostic payload (7)
    uint8_t *ptr = message_buffer;
    
    // Get target ECU logical address from current vehicle info
    uint16_t target_address = context->current_vehicle.logical_address;
    
    // DOIP Header
    *ptr++ = DOIP_PROTOCOL_VERSION;          // Protocol version
    *ptr++ = DOIP_INVERSE_PROTOCOL_VERSION;  // Inverse protocol version
    *ptr++ = (DOIP_DIAGNOSTIC_MESSAGE >> 8) & 0xFF;  // Payload type high byte
    *ptr++ = DOIP_DIAGNOSTIC_MESSAGE & 0xFF;         // Payload type low byte
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x07; // Payload length (7 bytes)
    
    // Diagnostic Message Payload
    *ptr++ = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;  // Source address high byte
    *ptr++ = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;         // Source address low byte
    *ptr++ = (target_address >> 8) & 0xFF;  // Target address high byte
    *ptr++ = target_address & 0xFF;         // Target address low byte
    *ptr++ = service_id;           // UDS Service ID
    *ptr++ = (data_id >> 8) & 0xFF;  // Data identifier high byte
    *ptr++ = data_id & 0xFF;         // Data identifier low byte
    
    // Send the message
    err_t err = tcp_write(context->tcp_pcb, message_buffer, sizeof(message_buffer), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_write failed - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    err = tcp_output(context->tcp_pcb);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_output failed - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Diagnostic message sent to ECU 0x%04X - Service:0x%02X, DID:0x%04X\r\n", 
           target_address, service_id, data_id);
    return DRV_DOIP_STATUS_OK;
}

// Simplified implementations for the remaining functions
static drv_doip_status_t drv_doip_send_diagnostic_request_impl(const void *hw_context, uint8_t service_id, uint16_t data_id, uint8_t *response, size_t max_response_len, size_t *actual_len)
{
    ASSERT(hw_context != NULL);
    ASSERT(response != NULL);
    ASSERT(actual_len != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (context->current_state != DRV_DOIP_STATE_ACTIVATED) {
        printf("DOIP Client: Not activated - cannot send diagnostic request\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Clear stream buffer before sending request
    if (context->stream_buffer != NULL) {
        xStreamBufferReset(context->stream_buffer);
    }
    
    // Send diagnostic message
    drv_doip_status_t status = doip_send_diagnostic_message(context, service_id, data_id);
    if (status != DRV_DOIP_STATUS_OK) {
        return status;
    }
    
    // Wait for response in stream buffer
    uint8_t buffer[256];
    size_t received = 0;
    TickType_t timeout_ticks = pdMS_TO_TICKS(DOIP_TCP_TIMEOUT_MS);
    
    // Try to receive DOIP header first (8 bytes)
    received = xStreamBufferReceive(context->stream_buffer, buffer, 8, timeout_ticks);
    if (received < 8) {
        printf("DOIP Client: Failed to receive response header (got %zu bytes)\r\n", received);
        *actual_len = 0;
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    // Parse DOIP header
    uint8_t protocol_version = buffer[0];
    uint8_t inverse_protocol_version = buffer[1];
    uint16_t payload_type = (buffer[2] << 8) | buffer[3];
    uint32_t payload_length = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
    
    printf("DOIP Client: Received response - Type: 0x%04X, Length: %lu bytes\r\n", 
           payload_type, payload_length);
    
    // Validate protocol version
    if (protocol_version != DOIP_PROTOCOL_VERSION || 
        inverse_protocol_version != DOIP_INVERSE_PROTOCOL_VERSION) {
        printf("DOIP Client: Invalid protocol version: 0x%02X/0x%02X\r\n", 
               protocol_version, inverse_protocol_version);
        *actual_len = 0;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Check response type - handle both positive responses and negative ACKs
    if (payload_type == DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK) {
        printf("DOIP Client: Received negative ACK response (ECU does not support this request)\r\n");
        if (payload_length >= 1) {
            // Try to receive and parse the NACK code
            uint8_t nack_payload;
            size_t nack_received = xStreamBufferReceive(context->stream_buffer, 
                                                       &nack_payload, 1, pdMS_TO_TICKS(1000));
            if (nack_received == 1) {
                printf("DOIP Client: NACK code: 0x%02X (Request out of range)\r\n", nack_payload);
            }
        }
        *actual_len = 0;
        // Return OK with 0 bytes to indicate "request not supported by this ECU"
        // This allows the application to handle gracefully rather than treating as error
        return DRV_DOIP_STATUS_OK;
    } else if (payload_type != DOIP_DIAGNOSTIC_MESSAGE) {
        printf("DOIP Client: Unexpected response type: 0x%04X\r\n", payload_type);
        *actual_len = 0;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Receive payload if present
    if (payload_length > 0 && payload_length <= sizeof(buffer) - 8) {
        size_t payload_received = xStreamBufferReceive(context->stream_buffer, 
                                                      &buffer[8], payload_length, timeout_ticks);
        if (payload_received != payload_length) {
            printf("DOIP Client: Failed to receive complete payload (got %zu of %lu bytes)\r\n", 
                   payload_received, payload_length);
            *actual_len = 0;
            return DRV_DOIP_STATUS_TIMEOUT;
        }
        
        // Debug: Print raw payload bytes
        printf("DOIP Client: Payload bytes: ");
        for (uint32_t i = 0; i < payload_length && i < 16; i++) {
            printf("0x%02X ", buffer[8 + i]);
        }
        printf("\r\n");
        
        // Extract diagnostic payload (skip DOIP addressing info - first 4 bytes of payload)
        if (payload_length > 4) {
            size_t diag_payload_len = payload_length - 4;
            size_t copy_len = (diag_payload_len > max_response_len) ? max_response_len : diag_payload_len;
            
            memcpy(response, &buffer[8 + 4], copy_len);  // Skip 8-byte header + 4-byte addressing
            *actual_len = copy_len;
            
            printf("DOIP Client: Diagnostic payload length: %zu bytes\r\n", diag_payload_len);
            printf("DOIP Client: Diagnostic request completed\r\n");
            return DRV_DOIP_STATUS_OK;
        } else {
            printf("DOIP Client: Short payload - likely negative response or ACK only\r\n");
        }
    } else if (payload_length > sizeof(buffer) - 8) {
        printf("DOIP Client: Payload too large: %lu bytes (max %zu)\r\n", 
               payload_length, sizeof(buffer) - 8);
    }
    
    *actual_len = 0;
    return DRV_DOIP_STATUS_ERROR;
}



static drv_doip_state_t drv_doip_get_status_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    return context->current_state;
}

static drv_doip_status_t drv_doip_register_callback_impl(const void *hw_context, drv_doip_cb_type_t type, drv_doip_callback_t callback)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (type < 5) {
        context->callbacks[type] = callback;
        return DRV_DOIP_STATUS_OK;
    }
    
    return DRV_DOIP_STATUS_ERROR;
}

// Helper function implementations

// Alive check function implementations
static drv_doip_status_t doip_send_alive_check_request(drv_doip_hw_context_t *context)
{
    uint8_t message_buffer[10];
    
    // Create alive check request using utility
    doip_utils_create_alive_check_request(message_buffer, DOIP_CLIENT_SOURCE_ADDRESS);
    
    // Send the message
    err_t err = tcp_write(context->tcp_pcb, message_buffer, sizeof(message_buffer), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_write alive check failed - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    err = tcp_output(context->tcp_pcb);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_output alive check failed - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Alive check request sent (10 bytes)\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t doip_handle_alive_check_response(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length)
{
    uint16_t source_address;
    
    printf("DOIP Client: Alive check response received\r\n");
    
    if (doip_utils_handle_alive_check_payload(payload, payload_length, &source_address)) {
        printf("DOIP Client: Alive check response received from 0x%04X\r\n", source_address);
    }
    
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t doip_handle_alive_check_request(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length)
{
    uint8_t message_buffer[10];
    
    if (payload_length < 2) {
        printf("DOIP Client: Invalid alive check request payload length\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Alive check request received, sending response\r\n");
    
    // Create alive check response using utility
    doip_utils_create_alive_check_response(message_buffer, payload);
    
    // Send the response
    err_t err = tcp_write(context->tcp_pcb, message_buffer, sizeof(message_buffer), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_write alive check response failed - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    err = tcp_output(context->tcp_pcb);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_output alive check response failed - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Alive check response sent (10 bytes)\r\n");
    return DRV_DOIP_STATUS_OK;
}

