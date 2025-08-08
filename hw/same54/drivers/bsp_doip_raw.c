#include "driver_doip.h"
// Include lwIP headers first to avoid ERR_TIMEOUT conflict with ASF4
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip4.h"
#include "eth_ipstack_main.h"
#include "utils_assert.h"
#include "printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "semphr.h"
#include <string.h>


// Raw lwIP configuration - optimized buffer sizes for client
#define DOIP_STREAM_BUFFER_SIZE         (4096)  // Increased to 4KB for client reliability
#define DOIP_STREAM_TRIGGER_LEVEL       (1)

// Connection reliability constants
#define DOIP_CONNECTION_RETRY_MAX       5       /**< Maximum connection retry attempts */
#define DOIP_RETRY_BASE_DELAY_MS        50      /**< Base delay for exponential backoff (ms) */
#define DOIP_RETRY_MAX_DELAY_MS         5000    /**< Maximum retry delay (ms) */
#define DOIP_CONNECTION_HEALTH_CHECK_MS 30000   /**< Connection health check interval (30s) */
#define DOIP_ECU_RESPONSE_TIMEOUT_MIN   100     /**< Minimum ECU response timeout (ms) */
#define DOIP_ECU_RESPONSE_TIMEOUT_MAX   5000    /**< Maximum ECU response timeout (ms) */

// Discovery reliability constants
#define DOIP_DISCOVERY_RETRY_MAX        3       /**< Maximum discovery retry attempts */
#define DOIP_DISCOVERY_INITIAL_TIMEOUT  2000    /**< Initial discovery timeout (ms) */
#define DOIP_DISCOVERY_RETRY_TIMEOUT    5000    /**< Extended timeout for retries (ms) */
#define DOIP_DISCOVERY_INTER_ATTEMPT_DELAY 500  /**< Delay between discovery attempts (ms) */

// Diagnostic request retry and circuit breaker constants
#define DOIP_DIAGNOSTIC_RETRY_MAX       3       /**< Maximum diagnostic request retry attempts */
#define DOIP_DIAGNOSTIC_RETRY_DELAY_MS  100     /**< Delay between diagnostic retry attempts (ms) */
#define DOIP_CIRCUIT_BREAKER_FAILURE_THRESHOLD 5  /**< Failures before circuit breaker opens */
#define DOIP_CIRCUIT_BREAKER_TIMEOUT_MS 10000   /**< Circuit breaker timeout before attempting reset (ms) */

// Legacy chunking constants (kept for compatibility)
#define DOIP_CHUNK_RETRY_MAX         3       /**< Maximum retry attempts for chunk transmission */
#define DOIP_CHUNK_TIMEOUT_MS        5000    /**< Timeout for chunk transmission in milliseconds */

// Universal chunk size calculation based on lwIP parameters
#define DOIP_TCP_CHUNK_SIZE          (TCP_MSS - 60)  /**< Dynamic chunk size based on TCP_MSS with safety margin */
#define DOIP_CHUNK_SIZE_MIN          512     /**< Minimum chunk size for very small TCP_MSS values */
#define DOIP_CHUNK_SIZE_MAX          2048    /**< Maximum chunk size to prevent memory issues */

// Helper function to get optimal chunk size based on lwIP configuration
static inline uint32_t doip_get_optimal_chunk_size(void)
{
    uint32_t calculated_size = TCP_MSS - 60;  // Safety margin of 60 bytes
    
    // Ensure chunk size is within reasonable bounds
    if (calculated_size < DOIP_CHUNK_SIZE_MIN) {
        calculated_size = DOIP_CHUNK_SIZE_MIN;
        printf("DOIP Raw lwIP: Warning - TCP_MSS too small (%d), using minimum chunk size (%d)\r\n", 
               TCP_MSS, DOIP_CHUNK_SIZE_MIN);
    } else if (calculated_size > DOIP_CHUNK_SIZE_MAX) {
        calculated_size = DOIP_CHUNK_SIZE_MAX;
        printf("DOIP Raw lwIP: Warning - TCP_MSS too large (%d), using maximum chunk size (%d)\r\n", 
               TCP_MSS, DOIP_CHUNK_SIZE_MAX);
    }
    
    // Calculated optimal chunk size based on TCP_MSS with safety margin
    
    return calculated_size;
}


// Enhanced error classification for automotive diagnostics
typedef enum {
    DOIP_ERROR_NONE = 0,
    DOIP_ERROR_NETWORK_TIMEOUT,     /**< Network layer timeout */
    DOIP_ERROR_NETWORK_CONNECTION,  /**< TCP connection failed */
    DOIP_ERROR_NETWORK_RESET,       /**< Connection reset by peer */
    DOIP_ERROR_ECU_NO_RESPONSE,     /**< ECU not responding */
    DOIP_ERROR_ECU_NACK,            /**< ECU negative acknowledgment */
    DOIP_ERROR_PROTOCOL_VERSION,    /**< DoIP protocol version mismatch */
    DOIP_ERROR_PROTOCOL_PAYLOAD,    /**< Invalid payload format */
    DOIP_ERROR_RESOURCE_EXHAUSTED,  /**< Out of memory/buffers */
} doip_error_classification_t;

// Circuit breaker states for diagnostic requests
typedef enum {
    DOIP_CIRCUIT_CLOSED = 0,        /**< Normal operation - requests allowed */
    DOIP_CIRCUIT_OPEN = 1,          /**< Circuit breaker open - requests blocked */
    DOIP_CIRCUIT_HALF_OPEN = 2,     /**< Testing if ECU is responsive again */
} doip_circuit_breaker_state_t;

// ECU-specific connection parameters
typedef struct {
    uint32_t avg_response_time_ms;   /**< Average response time for this ECU */
    uint32_t timeout_ms;             /**< Adaptive timeout for this ECU */
    uint8_t  connection_failures;    /**< Recent connection failure count */
    uint32_t last_alive_check_ms;    /**< Last successful alive check timestamp */
    bool     is_responsive;          /**< ECU responsiveness flag */
    
    // Circuit breaker for diagnostic requests
    doip_circuit_breaker_state_t circuit_state; /**< Current circuit breaker state */
    uint8_t  diagnostic_failures;    /**< Recent diagnostic request failure count */
    uint32_t circuit_opened_ms;      /**< Timestamp when circuit breaker opened */
} doip_ecu_connection_params_t;

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
    
    // Multi-ECU discovery cache
    doip_multi_ecu_cache_t discovery_cache;
    
    // System monitoring data
    drv_doip_system_monitoring_t monitoring_data;
    
    // Connection reliability enhancements
    doip_ecu_connection_params_t ecu_params;
    doip_error_classification_t last_error;
    uint8_t connection_retry_count;
    uint32_t last_connection_attempt_ms;
    uint32_t next_retry_delay_ms;
    
    // Buffer monitoring
    uint32_t buffer_overflow_count;
    uint32_t max_buffer_usage;
    
    // Callbacks
    drv_doip_callback_t callbacks[5]; // Array for different callback types
} drv_doip_hw_context_t;

// Static message buffer for discovery request to avoid pbuf heap allocation
static uint8_t static_discovery_message_buffer[32]; // Discovery message is only 8 bytes, but allow extra space

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
};

// Forward declarations for helper functions
static drv_doip_status_t doip_send_routing_activation_request(drv_doip_hw_context_t *context);
static drv_doip_status_t doip_send_diagnostic_message(drv_doip_hw_context_t *context, uint8_t service_id, uint16_t data_id);
static drv_doip_status_t doip_send_chunked_payload(drv_doip_hw_context_t *context, const uint8_t *payload_data, uint32_t payload_length);

// Connection reliability helper functions
static void doip_classify_error(drv_doip_hw_context_t *context, err_t lwip_error);
static uint32_t doip_calculate_retry_delay(drv_doip_hw_context_t *context);
static uint32_t doip_get_adaptive_timeout(drv_doip_hw_context_t *context);
static drv_doip_status_t doip_attempt_reconnection(drv_doip_hw_context_t *context, const drv_doip_vehicle_info_t *vehicle_info);
static bool doip_should_retry_connection(drv_doip_hw_context_t *context);
static void doip_update_ecu_response_time(drv_doip_hw_context_t *context, uint32_t response_time_ms);
static void doip_reset_connection_state(drv_doip_hw_context_t *context);
static drv_doip_status_t doip_perform_discovery_attempt(drv_doip_hw_context_t *context, uint8_t attempt_number, uint32_t timeout_ms);
static drv_doip_status_t doip_send_alive_check(drv_doip_hw_context_t *context);
static bool doip_is_connection_health_check_due(drv_doip_hw_context_t *context);
static bool doip_is_circuit_breaker_open(drv_doip_hw_context_t *context);
static void doip_update_circuit_breaker(drv_doip_hw_context_t *context, bool request_success);
static drv_doip_status_t doip_send_diagnostic_with_retry(drv_doip_hw_context_t *context, uint8_t service_id, uint16_t data_id,
                                                         const uint8_t *request_payload, size_t request_payload_len,
                                                         uint8_t *response_buffer, size_t max_response_len, size_t *actual_len);
static void doip_monitor_buffer_usage(drv_doip_hw_context_t *context);
static bool doip_is_buffer_space_available(drv_doip_hw_context_t *context, size_t required_space);


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
        // Check if this might be an alive check response (header + small payload)
        if (p->len >= 8 && p->len <= 12) {
            uint8_t *data = (uint8_t *)p->payload;
            uint16_t payload_type = (data[2] << 8) | data[3];
            
            if (payload_type == DOIP_ALIVE_CHECK_RESPONSE) {
                printf("DOIP Client: Received alive check response from ECU\r\n");
                context->ecu_params.last_alive_check_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                context->ecu_params.is_responsive = true;
                
                // ACK the alive check response but don't buffer it (it's handled here)
                tcp_recved(tpcb, p->len);
                pbuf_free(p);
                return ERR_OK;
            }
        }
        
        // Regular data processing
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        
        // Check buffer space before attempting to store data
        if (!doip_is_buffer_space_available(context, p->len)) {
            printf("DOIP Client: Buffer space check failed - dropping %d bytes\r\n", p->len);
            context->buffer_overflow_count++;
            // Don't ACK - this will cause TCP to apply backpressure
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            pbuf_free(p);
            return ERR_MEM;
        }
        size_t sent = xStreamBufferSendFromISR(
            context->stream_buffer,
            p->payload,
            p->len,
            &xHigherPriorityTaskWoken
        );
        
        if (sent == p->len) {
            tcp_recved(tpcb, p->len);
            printf("DOIP Client: Raw TCP received %d bytes, buffered and ACK sent\r\n", p->len);
            
            // Monitor buffer usage after successful reception
            doip_monitor_buffer_usage(context);
        } else {
            printf("DOIP Client: Stream buffer full, dropped %d bytes (buffered only %d) - no ACK\r\n", p->len, sent);
            
            // Update buffer monitoring statistics
            context->buffer_overflow_count++;
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
    
    // Classify error for automotive diagnostic context
    doip_classify_error(context, err);
    
    // Standardized error code mapping for lwIP errors
    const char* err_str = "UNKNOWN";
    const char* diagnostic_context = "UNKNOWN";
    switch(err) {
        case ERR_ABRT: 
            err_str = "ABORTED"; 
            diagnostic_context = "ECU_DISCONNECT";
            break;
        case ERR_RST: 
            err_str = "RESET"; 
            diagnostic_context = "NETWORK_RESET";
            break;
        case ERR_CONN: 
            err_str = "NOT_CONNECTED"; 
            diagnostic_context = "CONNECTION_FAILED";
            break;
        case ERR_TIMEOUT: 
            err_str = "TIMEOUT"; 
            diagnostic_context = "ECU_NO_RESPONSE";
            break;
        case ERR_MEM: 
            err_str = "OUT_OF_MEMORY"; 
            diagnostic_context = "RESOURCE_EXHAUSTED";
            break;
        default: 
            err_str = "UNKNOWN"; 
            diagnostic_context = "UNEXPECTED_ERROR";
            break;
    }
    
    printf("DOIP Client: TCP error - lwIP_err=%d (%s), diagnostic_context=%s\r\n", 
           err, err_str, diagnostic_context);
    
    context->tcp_pcb = NULL; // PCB is already freed by lwIP
    
    // Update failure statistics
    context->ecu_params.connection_failures++;
    
    // Handle different error types appropriately for automotive diagnostics
    if (err == ERR_ABRT) {
        printf("DOIP Client: ECU disconnected (may be normal session end)\r\n");
        context->current_state = DRV_DOIP_STATE_IDLE;  // Clean disconnect
    } else if (err == ERR_TIMEOUT) {
        printf("DOIP Client: ECU response timeout - connection lost\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        context->ecu_params.is_responsive = false;
    } else if (err == ERR_RST) {
        printf("DOIP Client: Network reset - possible ECU restart or network issue\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
    } else {
        printf("DOIP Client: Unexpected TCP error - connection unreliable\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
    }
    
    // Signal any waiting operations
    if (context->connected_sem != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(context->connected_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    
    // Clear stream buffer on any error (safe to call from ISR context)
    if (context->stream_buffer != NULL) {
        xStreamBufferReset(context->stream_buffer);
        
        // Update buffer monitoring statistics
        context->buffer_overflow_count++;
    }
}

//-----------------------------------------------------------------------------
// Connection Reliability Helper Functions
//-----------------------------------------------------------------------------

/**
 * @brief Classify lwIP error into automotive diagnostic context
 */
static void doip_classify_error(drv_doip_hw_context_t *context, err_t lwip_error)
{
    switch(lwip_error) {
        case ERR_TIMEOUT:
            context->last_error = DOIP_ERROR_NETWORK_TIMEOUT;
            break;
        case ERR_CONN:
        case ERR_CLSD:
            context->last_error = DOIP_ERROR_NETWORK_CONNECTION;
            break;
        case ERR_RST:
        case ERR_ABRT:
            context->last_error = DOIP_ERROR_NETWORK_RESET;
            break;
        case ERR_MEM:
        case ERR_BUF:
            context->last_error = DOIP_ERROR_RESOURCE_EXHAUSTED;
            break;
        default:
            context->last_error = DOIP_ERROR_NETWORK_CONNECTION;
            break;
    }
}

/**
 * @brief Calculate exponential backoff delay for connection retries
 */
static uint32_t doip_calculate_retry_delay(drv_doip_hw_context_t *context)
{
    if (context->connection_retry_count == 0) {
        context->next_retry_delay_ms = DOIP_RETRY_BASE_DELAY_MS;
    } else {
        // Exponential backoff: 50ms, 100ms, 200ms, 400ms, 800ms, then cap at 5000ms
        context->next_retry_delay_ms *= 2;
        if (context->next_retry_delay_ms > DOIP_RETRY_MAX_DELAY_MS) {
            context->next_retry_delay_ms = DOIP_RETRY_MAX_DELAY_MS;
        }
    }
    
    printf("DOIP Client: Retry delay calculated: %u ms (attempt %d)\r\n", 
           context->next_retry_delay_ms, context->connection_retry_count + 1);
    
    return context->next_retry_delay_ms;
}

/**
 * @brief Get adaptive timeout based on ECU response characteristics
 */
static uint32_t doip_get_adaptive_timeout(drv_doip_hw_context_t *context)
{
    // Start with baseline timeout
    uint32_t timeout = context->ecu_params.timeout_ms;
    
    if (timeout == 0) {
        // Initialize timeout based on ECU responsiveness
        timeout = context->ecu_params.is_responsive ? 
                  DOIP_ECU_RESPONSE_TIMEOUT_MIN : DOIP_ECU_RESPONSE_TIMEOUT_MAX;
    }
    
    // Adapt based on recent failures
    if (context->ecu_params.connection_failures > 2) {
        timeout *= 2;  // Double timeout after multiple failures
        if (timeout > DOIP_ECU_RESPONSE_TIMEOUT_MAX) {
            timeout = DOIP_ECU_RESPONSE_TIMEOUT_MAX;
        }
    }
    
    context->ecu_params.timeout_ms = timeout;
    return timeout;
}

/**
 * @brief Check if connection retry should be attempted
 */
static bool doip_should_retry_connection(drv_doip_hw_context_t *context)
{
    if (context->connection_retry_count >= DOIP_CONNECTION_RETRY_MAX) {
        printf("DOIP Client: Maximum retry attempts reached (%d)\r\n", DOIP_CONNECTION_RETRY_MAX);
        return false;
    }
    
    // Check minimum delay between attempts
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if ((current_time - context->last_connection_attempt_ms) < context->next_retry_delay_ms) {
        printf("DOIP Client: Too soon for retry (need %u ms delay)\r\n", 
               context->next_retry_delay_ms);
        return false;
    }
    
    return true;
}

/**
 * @brief Update ECU response time statistics for adaptive timeouts
 */
static void doip_update_ecu_response_time(drv_doip_hw_context_t *context, uint32_t response_time_ms)
{
    if (context->ecu_params.avg_response_time_ms == 0) {
        context->ecu_params.avg_response_time_ms = response_time_ms;
    } else {
        // Moving average: new_avg = old_avg * 0.8 + new_sample * 0.2
        context->ecu_params.avg_response_time_ms = 
            (context->ecu_params.avg_response_time_ms * 4 + response_time_ms) / 5;
    }
    
    // Determine ECU responsiveness
    context->ecu_params.is_responsive = 
        (context->ecu_params.avg_response_time_ms < DOIP_ECU_RESPONSE_TIMEOUT_MIN * 2);
    
    printf("DOIP Client: ECU response time updated - avg: %u ms, responsive: %s\r\n", 
           context->ecu_params.avg_response_time_ms,
           context->ecu_params.is_responsive ? "YES" : "NO");
}

/**
 * @brief Reset connection state for clean reconnection
 */
static void doip_reset_connection_state(drv_doip_hw_context_t *context)
{
    context->connection_retry_count = 0;
    context->next_retry_delay_ms = DOIP_RETRY_BASE_DELAY_MS;
    context->last_error = DOIP_ERROR_NONE;
    context->ecu_params.connection_failures = 0;
    
    printf("DOIP Client: Connection state reset for clean reconnection\r\n");
}

/**
 * @brief Attempt reconnection with reliability enhancements
 */
static drv_doip_status_t doip_attempt_reconnection(drv_doip_hw_context_t *context, 
                                                   const drv_doip_vehicle_info_t *vehicle_info)
{
    if (!doip_should_retry_connection(context)) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Update retry state
    context->connection_retry_count++;
    context->last_connection_attempt_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t retry_delay = doip_calculate_retry_delay(context);
    
    printf("DOIP Client: Attempting reconnection #%d after %u ms delay\r\n", 
           context->connection_retry_count, retry_delay);
    
    // Apply exponential backoff delay
    vTaskDelay(pdMS_TO_TICKS(retry_delay));
    
    // Clean up any existing connection with enhanced cleanup to prevent memory pool exhaustion
    if (context->tcp_pcb != NULL) {
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        printf("DOIP Client: Closing existing connection for reconnection\r\n");
        
        // Enhanced cleanup delay for PCB pool cleanup
        vTaskDelay(pdMS_TO_TICKS(500)); // Increase cleanup delay significantly
        printf("DOIP Client: Connection cleanup completed\r\n");
    }
    
    // Reset stream buffer
    if (context->stream_buffer != NULL) {
        xStreamBufferReset(context->stream_buffer);
    }
    
    // Attempt new connection using existing connect logic
    err_t err;
    ip_addr_t server_addr;
    uint32_t server_ip = vehicle_info->ip_address;
    uint16_t server_port = vehicle_info->tcp_port;
    
    printf("DOIP Client: Reconnecting to %lu.%lu.%lu.%lu:%d (attempt %d)\r\n", 
           server_ip & 0xFF, (server_ip >> 8) & 0xFF, 
           (server_ip >> 16) & 0xFF, (server_ip >> 24) & 0xFF, 
           server_port, context->connection_retry_count);
    
    // Convert IP address
    IP4_ADDR(&server_addr, 
             server_ip & 0xFF,
             (server_ip >> 8) & 0xFF, 
             (server_ip >> 16) & 0xFF,
             (server_ip >> 24) & 0xFF);
    
    // Create new TCP PCB
    context->tcp_pcb = tcp_new();
    if (context->tcp_pcb == NULL) {
        printf("DOIP Client: Failed to create TCP PCB for reconnection\r\n");
        doip_classify_error(context, ERR_MEM);
        context->ecu_params.connection_failures++;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Set up callbacks
    tcp_arg(context->tcp_pcb, context);
    tcp_recv(context->tcp_pcb, doip_tcp_recv);
    tcp_sent(context->tcp_pcb, doip_tcp_sent);
    tcp_err(context->tcp_pcb, doip_tcp_err);
    
    // Attempt connection
    context->current_state = DRV_DOIP_STATE_CONNECTING;
    err = tcp_connect(context->tcp_pcb, &server_addr, server_port, doip_tcp_connected);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_connect failed during reconnection - err=%d\r\n", err);
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        doip_classify_error(context, err);
        context->ecu_params.connection_failures++;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Wait for connection with adaptive timeout
    uint32_t timeout_ms = doip_get_adaptive_timeout(context);
    printf("DOIP Client: Waiting for reconnection with adaptive timeout: %u ms\r\n", timeout_ms);
    
    if (xSemaphoreTake(context->connected_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        printf("DOIP Client: Reconnection timeout after %u ms\r\n", timeout_ms);
        
        if (context->tcp_pcb != NULL) {
            tcp_close(context->tcp_pcb);
            context->tcp_pcb = NULL;
        }
        
        context->last_error = DOIP_ERROR_NETWORK_TIMEOUT;
        context->ecu_params.connection_failures++;
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    if (context->current_state != DRV_DOIP_STATE_CONNECTED) {
        printf("DOIP Client: Reconnection failed - unexpected state: %d\r\n", context->current_state);
        context->ecu_params.connection_failures++;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Success - reset failure count and update success metrics
    printf("DOIP Client: Reconnection successful on attempt %d\r\n", context->connection_retry_count);
    doip_reset_connection_state(context);
    
    return DRV_DOIP_STATUS_OK;
}

/**
 * @brief Perform a single discovery attempt with specified timeout
 */
static drv_doip_status_t doip_perform_discovery_attempt(drv_doip_hw_context_t *context, 
                                                        uint8_t attempt_number, 
                                                        uint32_t timeout_ms)
{
    err_t err;
    ip_addr_t broadcast_addr;
    struct pbuf *p;
    doip_message_t request_msg;
    
    printf("DOIP Client: Discovery attempt #%d with %u ms timeout\r\n", attempt_number, timeout_ms);
    
    // Clean up any existing UDP PCB
    if (context->udp_pcb != NULL) {
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        vTaskDelay(pdMS_TO_TICKS(50)); // Brief cleanup delay
    }
    
    // Create UDP PCB for this attempt
    context->udp_pcb = udp_new();
    if (context->udp_pcb == NULL) {
        printf("DOIP Client: Failed to create UDP PCB for discovery attempt #%d\r\n", attempt_number);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Set up UDP receive callback
    udp_recv(context->udp_pcb, doip_udp_recv, context);
    
    // Bind to local port (any port for sending)
    err = udp_bind(context->udp_pcb, IP_ADDR_ANY, 0);
    if (err != ERR_OK) {
        printf("DOIP Client: Failed to bind UDP PCB for discovery attempt #%d - err=%d\r\n", attempt_number, err);
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Clear UDP stream buffer
    xStreamBufferReset(context->udp_stream_buffer);
    
    // Prepare broadcast address
    IP4_ADDR(&broadcast_addr, 255, 255, 255, 255);
    
    // Create vehicle identification request using utility
    doip_utils_create_header(&request_msg, DOIP_VEHICLE_IDENTIFICATION_REQUEST, 0);
    
    // Convert message to static buffer to avoid heap allocation
    doip_utils_serialize_message(&request_msg, static_discovery_message_buffer);
    uint32_t message_len = DOIP_HEADER_SIZE + request_msg.payload_length;
    
    // Create pbuf using PBUF_ROM to reference static buffer (no heap allocation)
    p = pbuf_alloc(PBUF_TRANSPORT, message_len, PBUF_ROM);
    if (p == NULL) {
        printf("DOIP Client: Failed to allocate pbuf for discovery attempt #%d\r\n", attempt_number);
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Set payload to point to static buffer (PBUF_ROM approach)
    p->payload = static_discovery_message_buffer;
    
    // Send broadcast request
    err = udp_sendto(context->udp_pcb, p, &broadcast_addr, DOIP_UDP_DISCOVERY_PORT);
    pbuf_free(p);
    
    if (err != ERR_OK) {
        printf("DOIP Client: Failed to send discovery request attempt #%d - err=%d\r\n", attempt_number, err);
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Discovery request #%d sent, waiting for response...\r\n", attempt_number);
    
    // Wait for first response with specified timeout
    if (xSemaphoreTake(context->discovery_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        printf("DOIP Client: Discovery attempt #%d timeout after %u ms\r\n", attempt_number, timeout_ms);
        udp_remove(context->udp_pcb);
        context->udp_pcb = NULL;
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    printf("DOIP Client: Discovery attempt #%d received response\r\n", attempt_number);
    return DRV_DOIP_STATUS_OK;
}

/**
 * @brief Check if connection health check is due
 */
static bool doip_is_connection_health_check_due(drv_doip_hw_context_t *context)
{
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // Check if we've never done a health check or if it's time for the next one
    if (context->ecu_params.last_alive_check_ms == 0 || 
        (current_time - context->ecu_params.last_alive_check_ms) >= DOIP_CONNECTION_HEALTH_CHECK_MS) {
        return true;
    }
    
    return false;
}

/**
 * @brief Send DoIP alive check request to ECU
 */
static drv_doip_status_t doip_send_alive_check(drv_doip_hw_context_t *context)
{
    if (context->current_state != DRV_DOIP_STATE_ACTIVATED) {
        printf("DOIP Client: Cannot send alive check - not in activated state\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (context->tcp_pcb == NULL) {
        printf("DOIP Client: Cannot send alive check - no TCP connection\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Sending alive check to ECU 0x%04X\r\n", context->current_vehicle.logical_address);
    
    // Create alive check message (DoIP header + 2 bytes source address)
    uint8_t alive_check_buffer[10]; // 8-byte header + 2-byte payload
    uint8_t *ptr = alive_check_buffer;
    
    // DOIP Header
    *ptr++ = DOIP_PROTOCOL_VERSION;          // Protocol version
    *ptr++ = DOIP_INVERSE_PROTOCOL_VERSION;  // Inverse protocol version
    *ptr++ = (DOIP_ALIVE_CHECK_REQUEST >> 8) & 0xFF;  // Payload type high byte
    *ptr++ = DOIP_ALIVE_CHECK_REQUEST & 0xFF;         // Payload type low byte
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x02; // Payload length (2 bytes)
    
    // Alive Check Payload (source address)
    *ptr++ = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;  // Source address high byte
    *ptr++ = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;         // Source address low byte
    
    // Send the alive check
    err_t err = tcp_write(context->tcp_pcb, alive_check_buffer, sizeof(alive_check_buffer), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_write failed for alive check - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    err = tcp_output(context->tcp_pcb);
    if (err != ERR_OK) {
        printf("DOIP Client: tcp_output failed for alive check - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Update timestamp
    context->ecu_params.last_alive_check_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    printf("DOIP Client: Alive check sent successfully\r\n");
    return DRV_DOIP_STATUS_OK;
}

/**
 * @brief Check if circuit breaker is open (blocking diagnostic requests)
 */
static bool doip_is_circuit_breaker_open(drv_doip_hw_context_t *context)
{
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    switch (context->ecu_params.circuit_state) {
        case DOIP_CIRCUIT_CLOSED:
            return false;
            
        case DOIP_CIRCUIT_OPEN:
            // Check if enough time has passed to attempt a half-open state
            if ((current_time - context->ecu_params.circuit_opened_ms) >= DOIP_CIRCUIT_BREAKER_TIMEOUT_MS) {
                printf("DOIP Client: Circuit breaker timeout elapsed - transitioning to half-open\r\n");
                context->ecu_params.circuit_state = DOIP_CIRCUIT_HALF_OPEN;
                return false; // Allow one test request
            }
            return true;
            
        case DOIP_CIRCUIT_HALF_OPEN:
            return false; // Allow requests to test ECU responsiveness
            
        default:
            return false;
    }
}

/**
 * @brief Update circuit breaker state based on diagnostic request result
 */
static void doip_update_circuit_breaker(drv_doip_hw_context_t *context, bool request_success)
{
    if (request_success) {
        // Reset failure count on success
        context->ecu_params.diagnostic_failures = 0;
        
        // Close circuit if it was open or half-open
        if (context->ecu_params.circuit_state != DOIP_CIRCUIT_CLOSED) {
            printf("DOIP Client: Circuit breaker closed - ECU responding normally\r\n");
            context->ecu_params.circuit_state = DOIP_CIRCUIT_CLOSED;
        }
    } else {
        // Increment failure count
        context->ecu_params.diagnostic_failures++;
        
        printf("DOIP Client: Diagnostic failure #%d recorded\r\n", context->ecu_params.diagnostic_failures);
        
        // Open circuit if threshold exceeded
        if (context->ecu_params.diagnostic_failures >= DOIP_CIRCUIT_BREAKER_FAILURE_THRESHOLD) {
            if (context->ecu_params.circuit_state != DOIP_CIRCUIT_OPEN) {
                printf("DOIP Client: Circuit breaker opened - ECU not responding reliably\r\n");
                context->ecu_params.circuit_state = DOIP_CIRCUIT_OPEN;
                context->ecu_params.circuit_opened_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            }
        }
    }
}

/**
 * @brief Send diagnostic request with retry mechanism and circuit breaker
 */
static drv_doip_status_t doip_send_diagnostic_with_retry(drv_doip_hw_context_t *context, 
                                                         uint8_t service_id, uint16_t data_id,
                                                         const uint8_t *request_payload, size_t request_payload_len,
                                                         uint8_t *response_buffer, size_t max_response_len, size_t *actual_len)
{
    // Check circuit breaker first
    if (doip_is_circuit_breaker_open(context)) {
        printf("DOIP Client: Circuit breaker open - blocking diagnostic request\r\n");
        *actual_len = 0;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    drv_doip_status_t result = DRV_DOIP_STATUS_ERROR;
    
    // Retry loop for diagnostic requests
    for (uint8_t attempt = 1; attempt <= DOIP_DIAGNOSTIC_RETRY_MAX; attempt++) {
        if (attempt > 1) {
            printf("DOIP Client: Diagnostic retry attempt #%d after %d ms delay\r\n", 
                   attempt, DOIP_DIAGNOSTIC_RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(DOIP_DIAGNOSTIC_RETRY_DELAY_MS));
            
            // Clear stream buffer before retry
            if (context->stream_buffer != NULL) {
                xStreamBufferReset(context->stream_buffer);
            }
        }
        
        printf("DOIP Client: Sending diagnostic request attempt #%d\r\n", attempt);
        
        // For now, only support simple DID requests (no additional payload) in retry mechanism
        if (request_payload_len > 0) {
            printf("DOIP Client: Retry mechanism does not yet support diagnostic requests with additional payload\r\n");
            *actual_len = 0;
            return DRV_DOIP_STATUS_ERROR;
        }
        
        // Send diagnostic message (simple DID request)
        drv_doip_status_t send_result = doip_send_diagnostic_message(context, service_id, data_id);
        if (send_result != DRV_DOIP_STATUS_OK) {
            printf("DOIP Client: Failed to send diagnostic request on attempt #%d\r\n", attempt);
            continue;
        }
        
        // Enhanced buffer for diagnostic responses (4KB for better reliability)
        uint8_t buffer[4096];
        size_t received = 0;
        
        // Use adaptive timeout based on ECU characteristics
        uint32_t adaptive_timeout_ms = doip_get_adaptive_timeout(context);
        TickType_t timeout_ticks = pdMS_TO_TICKS(adaptive_timeout_ms);
        
        // Track request start time for response time measurement
        uint32_t request_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        printf("DOIP Client: Waiting for diagnostic response (adaptive timeout: %u ms, attempt #%d)\r\n", 
               adaptive_timeout_ms, attempt);
        
        // Try to receive DOIP header first (8 bytes)
        received = xStreamBufferReceive(context->stream_buffer, buffer, 8, timeout_ticks);
        if (received < 8) {
            printf("DOIP Client: Failed to receive response header on attempt #%d (got %zu bytes)\r\n", attempt, received);
            continue;
        }
        
        // Parse DOIP header
        uint8_t protocol_version = buffer[0];
        uint8_t inverse_protocol_version = buffer[1];
        uint16_t payload_type = (buffer[2] << 8) | buffer[3];
        uint32_t payload_length = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
        
        printf("DOIP Client: Received response on attempt #%d - Type: 0x%04X, Length: %lu bytes\r\n", 
               attempt, payload_type, payload_length);
        
        // Validate protocol version
        if (protocol_version != DOIP_PROTOCOL_VERSION || 
            inverse_protocol_version != DOIP_INVERSE_PROTOCOL_VERSION) {
            printf("DOIP Client: Invalid protocol version on attempt #%d: 0x%02X/0x%02X\r\n", 
                   attempt, protocol_version, inverse_protocol_version);
            continue;
        }
        
        // Handle negative ACK responses
        uint8_t payload_buffer[32];
        size_t payload_received = 0;
        if (payload_length > 0 && payload_length <= sizeof(payload_buffer)) {
            payload_received = xStreamBufferReceive(context->stream_buffer, 
                                                   payload_buffer, payload_length, pdMS_TO_TICKS(1000));
        }
        
        if (doip_utils_handle_negative_ack(payload_type, payload_buffer, payload_length, actual_len)) {
            printf("DOIP Client: Request handled as negative ACK on attempt #%d\r\n", attempt);
            result = DRV_DOIP_STATUS_OK;
            break;
        }
        
        // Check response type
        if (payload_type != DOIP_DIAGNOSTIC_MESSAGE) {
            printf("DOIP Client: Unexpected response type on attempt #%d: 0x%04X\r\n", attempt, payload_type);
            continue;
        }
        
        // Process successful response
        if (payload_length > 0 && payload_length <= sizeof(buffer) - 8) {
            if (payload_received > 0 && payload_received <= payload_length) {
                // Already received payload for NACK check, copy to buffer and get the rest if needed
                memcpy(&buffer[8], payload_buffer, payload_received);
                
                if (payload_received < payload_length) {
                    size_t remaining = payload_length - payload_received;
                    size_t remaining_received = xStreamBufferReceive(context->stream_buffer, 
                                                                   &buffer[8 + payload_received], remaining, timeout_ticks);
                    if (remaining_received != remaining) {
                        printf("DOIP Client: Failed to receive remaining payload on attempt #%d\r\n", attempt);
                        continue;
                    }
                }
            } else {
                // Get the full payload
                payload_received = xStreamBufferReceive(context->stream_buffer, 
                                                      &buffer[8], payload_length, timeout_ticks);
                if (payload_received != payload_length) {
                    printf("DOIP Client: Failed to receive complete payload on attempt #%d\r\n", attempt);
                    continue;
                }
            }
            
            // Extract diagnostic payload (skip DOIP addressing info - first 4 bytes of payload)
            if (payload_length > 4) {
                size_t diag_payload_len = payload_length - 4;
                size_t copy_len = (diag_payload_len > max_response_len) ? max_response_len : diag_payload_len;
                
                memcpy(response_buffer, &buffer[8 + 4], copy_len);  // Skip 8-byte header + 4-byte addressing
                *actual_len = copy_len;
                
                // Calculate and update response time statistics
                uint32_t response_time_ms = (xTaskGetTickCount() * portTICK_PERIOD_MS) - request_start_time;
                doip_update_ecu_response_time(context, response_time_ms);
                
                printf("DOIP Client: Diagnostic request successful on attempt #%d (%u ms, %zu bytes)\r\n", 
                       attempt, response_time_ms, diag_payload_len);
                result = DRV_DOIP_STATUS_OK;
                break;
            }
        }
    }
    
    // Update circuit breaker based on final result
    doip_update_circuit_breaker(context, (result == DRV_DOIP_STATUS_OK));
    
    return result;
}

/**
 * @brief Monitor stream buffer usage and update statistics
 */
static void doip_monitor_buffer_usage(drv_doip_hw_context_t *context)
{
    if (context->stream_buffer == NULL) {
        return;
    }
    
    // Get current buffer usage
    size_t bytes_available = xStreamBufferSpacesAvailable(context->stream_buffer);
    size_t total_size = DOIP_STREAM_BUFFER_SIZE;
    size_t current_usage = total_size - bytes_available;
    
    // Update maximum usage statistics
    if (current_usage > context->max_buffer_usage) {
        context->max_buffer_usage = current_usage;
    }
    
    // Calculate usage percentage
    uint32_t usage_percent = (current_usage * 100) / total_size;
    
    // Log buffer usage if it's high
    if (usage_percent > 75) {
        printf("DOIP Client: High buffer usage - %u%% (%zu/%zu bytes)\r\n", 
               usage_percent, current_usage, total_size);
    }
    
    // Log buffer statistics periodically (every 100 calls)
    static uint32_t monitor_call_count = 0;
    monitor_call_count++;
    
    if (monitor_call_count % 100 == 0) {
        printf("DOIP Client: Buffer stats - current: %zu bytes (%u%%), max: %zu bytes, overflows: %u\r\n",
               current_usage, usage_percent, context->max_buffer_usage, context->buffer_overflow_count);
    }
}

/**
 * @brief Check if sufficient buffer space is available
 */
static bool doip_is_buffer_space_available(drv_doip_hw_context_t *context, size_t required_space)
{
    if (context->stream_buffer == NULL) {
        return false;
    }
    
    size_t available_space = xStreamBufferSpacesAvailable(context->stream_buffer);
    
    if (available_space < required_space) {
        printf("DOIP Client: Insufficient buffer space - need: %zu, available: %zu\r\n", 
               required_space, available_space);
        return false;
    }
    
    return true;
}

//-----------------------------------------------------------------------------
// Helper Function Implementations
//-----------------------------------------------------------------------------

static drv_doip_status_t doip_send_chunked_payload(drv_doip_hw_context_t *context, 
                                                   const uint8_t *payload_data, 
                                                   uint32_t payload_length)
{
    uint32_t remaining_bytes = payload_length;
    uint32_t offset = 0;
    uint8_t retry_count = 0;
    
    printf("DOIP Raw lwIP: Starting chunked transmission of %u bytes\r\n", (unsigned int)payload_length);
    
    // Print TCP buffer info for debugging
    if (context->tcp_pcb != NULL) {
        printf("DOIP Raw lwIP: TCP Buffer Info - Send: %u, Window: %u, MSS: %u bytes\r\n",
               tcp_sndbuf(context->tcp_pcb), context->tcp_pcb->snd_wnd, context->tcp_pcb->mss);
    }
    
    while (remaining_bytes > 0) {
        // Calculate optimal chunk size based on lwIP configuration
        uint32_t optimal_chunk_size = doip_get_optimal_chunk_size();
        uint32_t chunk_size = (remaining_bytes > optimal_chunk_size) ? optimal_chunk_size : remaining_bytes;
        
        // Check TCP send buffer space
        uint16_t available_space = tcp_sndbuf(context->tcp_pcb);
        if (available_space < chunk_size) {
            printf("DOIP Raw lwIP: TCP buffer full (avail:%u, need:%u), waiting\r\n", 
                   available_space, chunk_size);
            
            // Wait for some space to become available
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // Send chunk
        err_t err = tcp_write(context->tcp_pcb, &payload_data[offset], chunk_size, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            printf("DOIP Raw lwIP: Chunk write failed (offset:%u, size:%u, err:%d)\r\n", 
                   offset, chunk_size, err);
            
            retry_count++;
            if (retry_count >= DOIP_CHUNK_RETRY_MAX) {
                printf("DOIP Raw lwIP: Chunk transmission failed - max retries exceeded\r\n");
                return DRV_DOIP_STATUS_ERROR;
            }
            
            // Wait before retry
            vTaskDelay(pdMS_TO_TICKS(100 * retry_count));
            continue;
        }
        
        // Force output for this chunk
        err = tcp_output(context->tcp_pcb);
        if (err != ERR_OK) {
            printf("DOIP Raw lwIP: tcp_output failed for chunk - err=%d\r\n", err);
            return DRV_DOIP_STATUS_ERROR;
        }
        
        // Progress: sent chunk
        
        // Update progress
        offset += chunk_size;
        remaining_bytes -= chunk_size;
        retry_count = 0; // Reset retry count on success
        
        // Small delay to allow TCP processing
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    
    printf("DOIP Raw lwIP: Chunked transmission completed successfully\r\n");
    return DRV_DOIP_STATUS_OK;
}

//-----------------------------------------------------------------------------
// Forward Declarations - Driver Implementation Functions
//-----------------------------------------------------------------------------
static drv_doip_status_t drv_doip_init_impl(const void *hw_context);
static drv_doip_status_t drv_doip_deinit_impl(const void *hw_context);
static drv_doip_status_t drv_doip_discover_vehicles_impl(const void *hw_context, drv_doip_vehicle_info_t *vehicle_info);
static drv_doip_status_t drv_doip_connect_to_vehicle_impl(const void *hw_context, const drv_doip_vehicle_info_t *vehicle_info);
static drv_doip_status_t drv_doip_disconnect_impl(const void *hw_context);
static drv_doip_status_t drv_doip_send_diagnostic_request_impl(const void *hw_context, uint8_t service_id, uint16_t data_id, 
                                                              const uint8_t *request_payload, size_t request_payload_len,
                                                              uint8_t *response_buffer, size_t max_response_len, size_t *actual_len);

static drv_doip_state_t drv_doip_get_status_impl(const void *hw_context);
static drv_doip_status_t drv_doip_register_callback_impl(const void *hw_context, drv_doip_cb_type_t type, drv_doip_callback_t callback);


//-----------------------------------------------------------------------------
// Raw Message and Packet Listener Implementation
//-----------------------------------------------------------------------------

static drv_doip_status_t drv_doip_send_raw_message_impl(const void *hw_context, uint16_t payload_type,
                                                        const uint8_t *payload_data, uint32_t payload_length,
                                                        bool use_static_buffer)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    (void)use_static_buffer; // Not used in raw implementation yet
    
    // Check if we're in a connected state (either CONNECTED or ACTIVATED is OK for raw messages)
    if (context->current_state != DRV_DOIP_STATE_CONNECTED && 
        context->current_state != DRV_DOIP_STATE_ACTIVATED) {
        printf("DOIP Raw lwIP: Not connected - cannot send raw message (state: %d)\r\n", context->current_state);
        printf("DOIP Raw lwIP: Current state: %s\r\n", 
               (context->current_state == DRV_DOIP_STATE_IDLE) ? "IDLE" :
               (context->current_state == DRV_DOIP_STATE_DISCOVERING) ? "DISCOVERING" :
               (context->current_state == DRV_DOIP_STATE_DISCOVERED) ? "DISCOVERED" :
               (context->current_state == DRV_DOIP_STATE_CONNECTING) ? "CONNECTING" :
               (context->current_state == DRV_DOIP_STATE_ERROR) ? "ERROR" : "UNKNOWN");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Raw lwIP: Unified raw message - type=0x%04X, length=%u bytes\r\n", 
           payload_type, (unsigned int)payload_length);
    
    // Validate TCP connection
    if (context->tcp_pcb == NULL) {
        printf("DOIP Raw lwIP: No TCP connection available\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // For large messages, use chunking approach
    if (payload_length > DOIP_SMALL_PAYLOAD_SIZE) {
        printf("DOIP Raw lwIP: Large message transmission (%u bytes) - using chunking approach\r\n", 
               (unsigned int)payload_length);
        
        // Send DOIP header first
        uint8_t header_buffer[DOIP_HEADER_SIZE];
        header_buffer[0] = DOIP_PROTOCOL_VERSION;
        header_buffer[1] = DOIP_INVERSE_PROTOCOL_VERSION;
        header_buffer[2] = (payload_type >> 8) & 0xFF;
        header_buffer[3] = payload_type & 0xFF;
        header_buffer[4] = (payload_length >> 24) & 0xFF;
        header_buffer[5] = (payload_length >> 16) & 0xFF;
        header_buffer[6] = (payload_length >> 8) & 0xFF;
        header_buffer[7] = payload_length & 0xFF;
        
        // Send header
        err_t err = tcp_write(context->tcp_pcb, header_buffer, DOIP_HEADER_SIZE, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            printf("DOIP Raw lwIP: tcp_write failed for header - err=%d\r\n", err);
            return DRV_DOIP_STATUS_ERROR;
        }
        
        // Force output for header
        err = tcp_output(context->tcp_pcb);
        if (err != ERR_OK) {
            printf("DOIP Raw lwIP: tcp_output failed for header - err=%d\r\n", err);
            return DRV_DOIP_STATUS_ERROR;
        }
        
        printf("DOIP Raw lwIP: Header sent successfully, starting payload chunking\r\n");
        
        // Send payload using chunking
        drv_doip_status_t chunk_result = doip_send_chunked_payload(context, payload_data, payload_length);
        if (chunk_result != DRV_DOIP_STATUS_OK) {
            printf("DOIP Raw lwIP: Chunked payload transmission failed\r\n");
            return chunk_result;
        }
        
        printf("DOIP Raw lwIP: Successfully sent large message using chunking (%u bytes total)\r\n", 
               (unsigned int)(DOIP_HEADER_SIZE + payload_length));
        return DRV_DOIP_STATUS_OK;
    }
    
    // Handle small messages using existing approach
    printf("DOIP Raw lwIP: Small message - using standard transmission\r\n");
    
    // Calculate total message size: DOIP header (8 bytes) + payload
    uint32_t total_message_size = DOIP_HEADER_SIZE + payload_length;
    uint8_t message_buffer[DOIP_HEADER_SIZE + DOIP_SMALL_PAYLOAD_SIZE];
    
    // Build DOIP header
    message_buffer[0] = DOIP_PROTOCOL_VERSION;
    message_buffer[1] = DOIP_INVERSE_PROTOCOL_VERSION;
    message_buffer[2] = (payload_type >> 8) & 0xFF;
    message_buffer[3] = payload_type & 0xFF;
    message_buffer[4] = (payload_length >> 24) & 0xFF;
    message_buffer[5] = (payload_length >> 16) & 0xFF;
    message_buffer[6] = (payload_length >> 8) & 0xFF;
    message_buffer[7] = payload_length & 0xFF;
    
    // Copy payload data
    if (payload_length > 0 && payload_data != NULL) {
        memcpy(&message_buffer[DOIP_HEADER_SIZE], payload_data, payload_length);
    }
    
    // Send message using TCP
    err_t err = tcp_write(context->tcp_pcb, message_buffer, total_message_size, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("DOIP Raw lwIP: tcp_write failed for small message - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    err = tcp_output(context->tcp_pcb);
    if (err != ERR_OK) {
        printf("DOIP Raw lwIP: tcp_output failed for small message - err=%d\r\n", err);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Raw lwIP: Successfully sent small raw message (%u bytes total)\r\n", 
           (unsigned int)total_message_size);
    return DRV_DOIP_STATUS_OK;
}

// Consolidated packet listener stub - not implemented in raw lwIP version
static drv_doip_status_t drv_doip_packet_listener_stub(void)
{
    printf("DOIP Raw lwIP: Packet listener functionality not implemented\r\n");
    return DRV_DOIP_STATUS_ERROR;
}

// Packet listener function wrappers
static drv_doip_status_t drv_doip_start_packet_listener_stub(const void *hw_context, const drv_doip_packet_listener_config_t *config)
{
    (void)hw_context; (void)config;
    return drv_doip_packet_listener_stub();
}

static drv_doip_status_t drv_doip_stop_packet_listener_stub(const void *hw_context)
{
    (void)hw_context;
    return drv_doip_packet_listener_stub();
}

static drv_doip_status_t drv_doip_register_packet_callback_stub(const void *hw_context, drv_doip_packet_callback_t callback)
{
    (void)hw_context; (void)callback;
    return drv_doip_packet_listener_stub();
}

//-----------------------------------------------------------------------------
// Global Driver Instance
//-----------------------------------------------------------------------------

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
    
    // Raw DOIP messaging functions (unified implementation)
    .send_raw_message = drv_doip_send_raw_message_impl,
    .start_packet_listener = drv_doip_start_packet_listener_stub,
    .stop_packet_listener = drv_doip_stop_packet_listener_stub,
    .register_packet_callback = drv_doip_register_packet_callback_stub,

    .get_status = drv_doip_get_status_impl,
    .register_callback = drv_doip_register_callback_impl,
};

//-----------------------------------------------------------------------------
// Driver Implementation Functions
//-----------------------------------------------------------------------------

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
    
    uint8_t buffer[2048];  // Increased for multi-ECU responses
    
    printf("DOIP Client: Discovering vehicles via raw UDP with enhanced reliability\r\n");
    context->current_state = DRV_DOIP_STATE_DISCOVERING;
    
    // Reset discovery cache if starting fresh discovery
    if (context->discovery_cache.current_index >= context->discovery_cache.count) {
        context->discovery_cache.current_index = 0;
        context->discovery_cache.count = 0;
        printf("DOIP Client: Starting fresh discovery cycle with retry mechanism\r\n");
    } else {
        // We have cached vehicles, return the next one without sending new discovery
        printf("DOIP Client: Using cached discovery results (%d vehicles, returning index %d)\r\n",
               context->discovery_cache.count, context->discovery_cache.current_index);
        
        memcpy(vehicle_info, &context->discovery_cache.vehicles[context->discovery_cache.current_index], sizeof(drv_doip_vehicle_info_t));
        context->discovery_cache.current_index++;
        
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
    
    // Enhanced discovery with retry mechanism
    drv_doip_status_t discovery_result = DRV_DOIP_STATUS_TIMEOUT;
    
    for (uint8_t attempt = 1; attempt <= DOIP_DISCOVERY_RETRY_MAX; attempt++) {
        // Use shorter timeout for first attempt, longer for subsequent attempts
        uint32_t timeout_ms = (attempt == 1) ? DOIP_DISCOVERY_INITIAL_TIMEOUT : DOIP_DISCOVERY_RETRY_TIMEOUT;
        
        // Add inter-attempt delay for retry attempts
        if (attempt > 1) {
            printf("DOIP Client: Waiting %d ms before retry attempt #%d\r\n", 
                   DOIP_DISCOVERY_INTER_ATTEMPT_DELAY, attempt);
            vTaskDelay(pdMS_TO_TICKS(DOIP_DISCOVERY_INTER_ATTEMPT_DELAY));
        }
        
        discovery_result = doip_perform_discovery_attempt(context, attempt, timeout_ms);
        if (discovery_result == DRV_DOIP_STATUS_OK) {
            printf("DOIP Client: Discovery successful on attempt #%d\r\n", attempt);
            break;
        }
        
        printf("DOIP Client: Discovery attempt #%d failed: %d\r\n", attempt, discovery_result);
    }
    
    if (discovery_result != DRV_DOIP_STATUS_OK) {
        printf("DOIP Client: All discovery attempts failed after %d tries\r\n", DOIP_DISCOVERY_RETRY_MAX);
        context->current_state = DRV_DOIP_STATE_IDLE;
        return discovery_result;
    }
    
    // Discovery successful - continue with response processing
    printf("DOIP Client: Discovery completed, processing responses\r\n");
    
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
            // Accumulating response data
        } else {
            printf("DOIP Client: Buffer overflow prevented - ignoring %d bytes\r\n", received);
            break;
        }
    }
    
    // Clean up UDP PCB
    udp_remove(context->udp_pcb);
    context->udp_pcb = NULL;
    
    if (total_received < DOIP_HEADER_SIZE) {
        printf("DOIP Client: Insufficient response data (%d bytes)\r\n", total_received);
        context->current_state = DRV_DOIP_STATE_IDLE;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Received total %d bytes response\r\n", total_received);
    
    // Parse multiple DOIP responses and return info for FIRST valid vehicle
    // (This maintains backward compatibility with existing single-vehicle API)
    
    // If this is the first discovery call, parse ALL vehicles from the buffer
    if (context->discovery_cache.current_index == 0) {
        printf("DOIP Client: Parsing multi-ECU responses using common utility (%d bytes total)\r\n", total_received);
        
        // Use common utility to parse multi-ECU discovery response
        uint8_t parsed_count = doip_utils_parse_multi_ecu_discovery_response(
            buffer, total_received, context->discovered_ip_address, &context->discovery_cache);
        
        printf("DOIP Client: Common utility parsed and cached %d vehicles from discovery response\r\n", parsed_count);
    }
    
    if (context->discovery_cache.count == 0) {
        printf("DOIP Client: No valid ECU responses found\r\n");
        context->current_state = DRV_DOIP_STATE_IDLE;
        return DRV_DOIP_STATUS_NO_VEHICLE;
    }
    
    // Return the next vehicle from cache
    if (context->discovery_cache.current_index >= context->discovery_cache.count) {
        // Reset for next discovery cycle
        context->discovery_cache.current_index = 0;
        printf("DOIP Client: No more vehicles in cache, resetting index\r\n");
        context->current_state = DRV_DOIP_STATE_IDLE;
        return DRV_DOIP_STATUS_NO_VEHICLE;
    }
    
    // Copy vehicle info from cache
    memcpy(vehicle_info, &context->discovery_cache.vehicles[context->discovery_cache.current_index], sizeof(drv_doip_vehicle_info_t));
    context->discovery_cache.current_index++;
    
    // Returning cached vehicle from discovery
    
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
    
    // Use tcp_new() but manage allocation better by reusing existing PCB when possible
    if (context->tcp_pcb != NULL) {
        // Close existing connection before creating new one
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        printf("DOIP Client: Reusing TCP PCB by closing existing connection\r\n");
        
        // Wait for complete PCB cleanup - increase delay to prevent memory pool exhaustion
        vTaskDelay(pdMS_TO_TICKS(500)); // Increase from 100ms to 500ms
        
        // Additional delay to ensure cleanup completion before new allocation
        vTaskDelay(pdMS_TO_TICKS(200));
        printf("DOIP Client: TCP PCB cleanup completed\r\n");
    }
    
    // Create new TCP PCB (simplified approach - keep some heap allocation but minimize)
    context->tcp_pcb = tcp_new();
    if (context->tcp_pcb == NULL) {
        printf("DOIP Client: Failed to create TCP PCB - attempting cleanup and retry\r\n");
        
        // Wait longer for memory pool cleanup to complete
        vTaskDelay(pdMS_TO_TICKS(700));
        
        // Retry TCP PCB allocation
        context->tcp_pcb = tcp_new();
        if (context->tcp_pcb == NULL) {
            printf("DOIP Client: Critical error - TCP PCB allocation failed after cleanup retry\r\n");
            context->current_state = DRV_DOIP_STATE_ERROR;
            return DRV_DOIP_STATUS_ERROR;
        }
        printf("DOIP Client: TCP PCB allocation succeeded after cleanup retry\r\n");
    }
    
    printf("DOIP Client: TCP PCB created\r\n");
    
    // Set up callbacks
    tcp_arg(context->tcp_pcb, context);
    tcp_recv(context->tcp_pcb, doip_tcp_recv);
    tcp_sent(context->tcp_pcb, doip_tcp_sent);
    tcp_err(context->tcp_pcb, doip_tcp_err);
    
    // Ensure we're starting from a clean state
    if (context->current_state == DRV_DOIP_STATE_ERROR) {
        printf("DOIP Client: Resetting error state before new connection\r\n");
        context->current_state = DRV_DOIP_STATE_IDLE;
        
        // Clear any lingering stream buffer data
        if (context->stream_buffer != NULL) {
            xStreamBufferReset(context->stream_buffer);
        }
        
        // Small delay to ensure TCP stack is ready
        vTaskDelay(pdMS_TO_TICKS(200)); // 200ms delay
    }
    
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
    
    // Wait for connection with adaptive timeout
    uint32_t adaptive_timeout_ms = doip_get_adaptive_timeout(context);
    printf("DOIP Client: Waiting for raw TCP connection (adaptive timeout: %u ms)...\r\n", adaptive_timeout_ms);
    
    if (xSemaphoreTake(context->connected_sem, pdMS_TO_TICKS(adaptive_timeout_ms)) != pdTRUE) {
        printf("DOIP Client: TCP connection timeout after %u ms\r\n", adaptive_timeout_ms);
        
        // Classify timeout error
        context->last_error = DOIP_ERROR_NETWORK_TIMEOUT;
        context->ecu_params.connection_failures++;
        
        if (context->tcp_pcb != NULL) {
            tcp_close(context->tcp_pcb);
            context->tcp_pcb = NULL;
        }
        
        // Attempt automatic reconnection if appropriate
        if (context->connection_retry_count < DOIP_CONNECTION_RETRY_MAX) {
            printf("DOIP Client: Attempting automatic reconnection...\r\n");
            drv_doip_status_t retry_result = doip_attempt_reconnection(context, vehicle_info);
            if (retry_result == DRV_DOIP_STATUS_OK) {
                printf("DOIP Client: Automatic reconnection successful\r\n");
                // Continue with routing activation below
            } else {
                context->current_state = DRV_DOIP_STATE_ERROR;
                return retry_result;
            }
        } else {
            printf("DOIP Client: Connection failed - maximum retry attempts exceeded\r\n");
            context->current_state = DRV_DOIP_STATE_ERROR;
            return DRV_DOIP_STATUS_TIMEOUT;
        }
    } else {
        printf("DOIP Client: Initial connection successful\r\n");
        doip_reset_connection_state(context); // Reset retry counters on successful connection
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
        printf("DOIP Client: Shutting down TCP connection gracefully\r\n");
        
        // Remove callbacks to prevent issues during shutdown
        tcp_err(context->tcp_pcb, NULL);
        tcp_recv(context->tcp_pcb, NULL);
        tcp_sent(context->tcp_pcb, NULL);
        
        // Gracefully close TCP connection
        printf("DOIP Client: Closing TCP PCB\r\n");
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
        
        // Add a small delay to allow TCP stack to process the disconnect
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay
    }
    
    context->current_state = DRV_DOIP_STATE_IDLE;
    
    // Clear stream buffer
    if (context->stream_buffer != NULL) {
        xStreamBufferReset(context->stream_buffer);
    }
    
    printf("DOIP Client: Disconnect completed\r\n");
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
static drv_doip_status_t drv_doip_send_diagnostic_request_impl(const void *hw_context, uint8_t service_id, uint16_t data_id, 
                                                              const uint8_t *request_payload, size_t request_payload_len,
                                                              uint8_t *response_buffer, size_t max_response_len, size_t *actual_len)
{
    ASSERT(hw_context != NULL);
    ASSERT(response_buffer != NULL);
    ASSERT(actual_len != NULL);
    // request_payload can be NULL for simple DID requests
    ASSERT(request_payload != NULL || request_payload_len == 0);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (context->current_state != DRV_DOIP_STATE_ACTIVATED) {
        printf("DOIP Client: Not activated - cannot send diagnostic request\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Proactive connection health check before diagnostic request
    if (doip_is_connection_health_check_due(context)) {
        printf("DOIP Client: Performing proactive health check before diagnostic request\r\n");
        drv_doip_status_t alive_result = doip_send_alive_check(context);
        if (alive_result != DRV_DOIP_STATUS_OK) {
            printf("DOIP Client: Health check failed - connection may be unreliable\r\n");
            context->ecu_params.connection_failures++;
            // Continue with diagnostic request but mark ECU as less responsive
            context->ecu_params.is_responsive = false;
        } else {
            printf("DOIP Client: Health check passed - connection is healthy\r\n");
        }
    }
    
    // Clear stream buffer before sending request
    if (context->stream_buffer != NULL) {
        xStreamBufferReset(context->stream_buffer);
    }
    
    // Log unified diagnostic request details
    size_t total_request_size = 3 + request_payload_len; // service_id(1) + data_id(2) + payload
    printf("DOIP Client: Unified diagnostic request - service=0x%02X, data_id=0x%04X, payload_len=%zu, total_size=%zu\r\n",
           service_id, data_id, request_payload_len, total_request_size);
    
    // Use the enhanced diagnostic request function with retry mechanism and circuit breaker
    printf("DOIP Client: Sending diagnostic request with enhanced reliability\r\n");
    return doip_send_diagnostic_with_retry(context, service_id, data_id, 
                                          request_payload, request_payload_len, 
                                          response_buffer, max_response_len, actual_len);
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


