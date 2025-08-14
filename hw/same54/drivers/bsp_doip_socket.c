#include "driver_doip.h"
// Include lwIP headers first to avoid ERR_TIMEOUT conflict with ASF4
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"
#include "eth_ipstack_main.h"
#include "utils_assert.h"
#include "printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h> // Required for errno


// Hardware context structure
typedef struct {
    // State management
    drv_doip_state_t current_state;
    TaskHandle_t client_task_handle;
    
    // Vehicle information
    drv_doip_vehicle_info_t current_vehicle;
    
    // Multi-ECU discovery cache
    doip_multi_ecu_cache_t discovery_cache;
    
    // Socket-based resources
    int tcp_socket;
    
    // System monitoring data
    drv_doip_system_monitoring_t monitoring_data;
    
    // Callbacks
    drv_doip_callback_t callbacks[7]; // Extended for new callback types
    
    // Raw packet handling
    TaskHandle_t packet_listener_task_handle;
    drv_doip_packet_listener_config_t packet_listener_config;
    drv_doip_packet_callback_t packet_callback;
    bool packet_listener_active;
    
    // Packet fragmentation handling
    drv_doip_raw_packet_t fragment_buffer[10]; // Buffer for assembling fragments
    uint8_t fragment_count;
} drv_doip_hw_context_t;

// Static hardware context
static drv_doip_hw_context_t drv_doip_hw_context_0 = {
    .current_state = DRV_DOIP_STATE_IDLE,
    .client_task_handle = NULL,
    .tcp_socket = -1,
    .packet_listener_task_handle = NULL,
    .packet_callback = NULL,
    .packet_listener_active = false,
    .fragment_count = 0,
};

// Helper functions
static bool doip_send_tcp_message_socket(int socket, const doip_message_t *msg);
static bool doip_receive_tcp_message_socket(int socket, doip_message_t *msg, uint32_t timeout_ms);

// Alive check functions
static drv_doip_status_t doip_send_alive_check_request_socket(drv_doip_hw_context_t *context);
static drv_doip_status_t doip_handle_alive_check_response_socket(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length);
static drv_doip_status_t doip_handle_alive_check_request_socket(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length);

// Forward declarations of implementation functions
static drv_doip_status_t drv_doip_init_impl(const void *hw_context);
static drv_doip_status_t drv_doip_deinit_impl(const void *hw_context);
static drv_doip_status_t drv_doip_discover_vehicles_impl(const void *hw_context, drv_doip_vehicle_info_t *vehicle_info);
static drv_doip_status_t drv_doip_connect_to_vehicle_impl(const void *hw_context, const drv_doip_vehicle_info_t *vehicle_info);
static drv_doip_status_t drv_doip_disconnect_impl(const void *hw_context);
static drv_doip_status_t drv_doip_send_diagnostic_request_impl(const void *hw_context, uint8_t service_id, uint16_t data_id,
                                                              const uint8_t *request_payload, size_t request_payload_len,
                                                              uint8_t *response_buffer, size_t max_response_len, size_t *actual_len);

// Raw DOIP messaging implementation functions (unified)
static drv_doip_status_t drv_doip_send_raw_message_impl(const void *hw_context, uint16_t payload_type,
                                                        const uint8_t *payload_data, uint32_t payload_length,
                                                        bool use_static_buffer);
static drv_doip_status_t drv_doip_start_packet_listener_impl(const void *hw_context, const drv_doip_packet_listener_config_t *config);
static drv_doip_status_t drv_doip_stop_packet_listener_impl(const void *hw_context);
static drv_doip_status_t drv_doip_register_packet_callback_impl(const void *hw_context, drv_doip_packet_callback_t callback);

// Background packet listener task
static void doip_packet_listener_task(void *pvParameters);
static bool doip_convert_message_to_raw_packet(const doip_message_t *msg, uint32_t source_ip, uint16_t source_port, drv_doip_raw_packet_t *raw_packet);

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
    
    // Raw DOIP messaging functions
    .send_raw_message = drv_doip_send_raw_message_impl,
    .start_packet_listener = drv_doip_start_packet_listener_impl,
    .stop_packet_listener = drv_doip_stop_packet_listener_impl,
    .register_packet_callback = drv_doip_register_packet_callback_impl,

    .get_status = drv_doip_get_status_impl,
    .register_callback = drv_doip_register_callback_impl,
};

// Implementation functions
static drv_doip_status_t drv_doip_init_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    printf("DOIP Client: Initializing socket resources\r\n");
    
    // Initialize socket
    context->tcp_socket = -1;
    

    
    // Initialize callbacks array
    memset(context->callbacks, 0, sizeof(context->callbacks));
    
    printf("DOIP Client: Socket resources initialized successfully\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_deinit_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    printf("DOIP Client: Cleaning up socket resources\r\n");
    
    // Close socket
    if (context->tcp_socket >= 0) {
        close(context->tcp_socket);
        context->tcp_socket = -1;
    }
    
    context->current_state = DRV_DOIP_STATE_IDLE;
    return DRV_DOIP_STATUS_OK;
}


static drv_doip_status_t drv_doip_discover_vehicles_impl(const void *hw_context, drv_doip_vehicle_info_t *vehicle_info)
{
    ASSERT(hw_context != NULL);
    ASSERT(vehicle_info != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    int udp_socket = -1;
    struct sockaddr_in broadcast_addr, response_addr;
    socklen_t addr_len;
    doip_message_t request_msg;
    uint8_t buffer[2048];  // Larger buffer for multiple responses
    int result;
    
    printf("DOIP Client: Discovering vehicles via socket API (multi-ECU)\r\n");
    context->current_state = DRV_DOIP_STATE_DISCOVERING;
    
    // Initialize discovery cache for fresh discovery
    context->discovery_cache.count = 0;
    context->discovery_cache.current_index = 0;
    
    // Create UDP socket
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        printf("DOIP Client: Failed to create UDP socket (error: %d)\r\n", udp_socket);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Enable broadcast
    int broadcast_enable = 1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        printf("DOIP Client: Failed to enable broadcast\r\n");
        close(udp_socket);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Set socket to non-blocking mode for manual timeout handling
    int nonblock = 1;
    if (ioctlsocket(udp_socket, FIONBIO, &nonblock) != 0) {
        printf("DOIP Client: Warning - could not set non-blocking mode\r\n");
        // Continue anyway, we'll handle blocking behavior
    }
    
    // Prepare broadcast address
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(DOIP_UDP_DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = PP_HTONL(IPADDR_BROADCAST);
    
    // Create vehicle identification request
    doip_utils_create_header(&request_msg, DOIP_VEHICLE_IDENTIFICATION_REQUEST, 0);
    
    // Convert message to buffer
    doip_utils_serialize_message(&request_msg, buffer);
    
    result = sendto(udp_socket, buffer, DOIP_HEADER_SIZE + request_msg.payload_length, 0,
                    (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    if (result < 0) {
        printf("DOIP Client: Failed to send discovery request\r\n");
        close(udp_socket);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Discovery request sent, collecting ECU responses...\r\n");
    
    // Collect multiple responses with extended timeout
    addr_len = sizeof(response_addr);
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(DOIP_DISCOVERY_TIMEOUT_MS);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        result = recvfrom(udp_socket, buffer, sizeof(buffer), 0,
                          (struct sockaddr*)&response_addr, &addr_len);
        
        if (result > 0) {
            printf("DOIP Client: Received discovery response (%d bytes)\r\n", result);
            
            // Parse multi-ECU response using utility function
            uint32_t source_ip = ntohl(response_addr.sin_addr.s_addr);
            uint8_t parsed_count = doip_utils_parse_multi_ecu_discovery_response(
                buffer, result, source_ip, &context->discovery_cache);
            
            if (parsed_count > 0) {
                printf("DOIP Client: Parsed %d ECU(s) from this response\r\n", parsed_count);
            }
            
            // Continue listening for more responses
        } else if (result == 0) {
            printf("DOIP Client: Connection closed during discovery\r\n");
            break;
        } else {
            // No data available yet, wait a bit and try again
            vTaskDelay(pdMS_TO_TICKS(50)); // 50ms delay for multi-ECU responses
        }
    }
    
    close(udp_socket);
    
    if (context->discovery_cache.count == 0) {
        printf("DOIP Client: No ECUs discovered\r\n");
        context->current_state = DRV_DOIP_STATE_IDLE;
        return DRV_DOIP_STATUS_NO_VEHICLE;
    }
    
    // Reset current_index for proper iteration through discovered ECUs
    context->discovery_cache.current_index = 0;
    
    // Return the first discovered ECU for compatibility
    memcpy(vehicle_info, &context->discovery_cache.vehicles[0], sizeof(drv_doip_vehicle_info_t));
    
    context->current_state = DRV_DOIP_STATE_DISCOVERED;
    
    printf("DOIP Client: Multi-ECU discovery completed - %d ECU(s) found\r\n", context->discovery_cache.count);
    for (uint8_t i = 0; i < context->discovery_cache.count; i++) {
        printf("  ECU %d: VIN=%s, LA=0x%04X, IP=%u.%u.%u.%u:%d\r\n", 
               i + 1,
               context->discovery_cache.vehicles[i].vin,
               context->discovery_cache.vehicles[i].logical_address,
               (unsigned)((context->discovery_cache.vehicles[i].ip_address >> 24) & 0xFF),
               (unsigned)((context->discovery_cache.vehicles[i].ip_address >> 16) & 0xFF),
               (unsigned)((context->discovery_cache.vehicles[i].ip_address >> 8) & 0xFF),
               (unsigned)(context->discovery_cache.vehicles[i].ip_address & 0xFF),
               context->discovery_cache.vehicles[i].tcp_port);
    }
    
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_connect_to_vehicle_impl(const void *hw_context, const drv_doip_vehicle_info_t *vehicle_info)
{
    ASSERT(hw_context != NULL);
    ASSERT(vehicle_info != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    struct sockaddr_in server_addr;
    doip_message_t request_msg, response_msg;
    int result;
    
    printf("🔍 [DEBUG] drv_doip_connect_to_vehicle_impl() called:\n");
    printf("   Vehicle IP: 0x%08X\n", vehicle_info->ip_address);
    printf("   Vehicle port: %d\n", vehicle_info->tcp_port);
    printf("   Logical address: 0x%04X\n", vehicle_info->logical_address);
    printf("   Current state: %d\n", context->current_state);
    
    printf("DOIP Client: Connecting to vehicle via socket\r\n");
    context->current_state = DRV_DOIP_STATE_CONNECTING;
    
    // Create TCP socket
    printf("🔍 [DEBUG] Creating TCP socket...\n");
    context->tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (context->tcp_socket < 0) {
        printf("❌ [DEBUG] Failed to create TCP socket (error: %d)\n", context->tcp_socket);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("✅ [DEBUG] TCP socket created successfully: %d\n", context->tcp_socket);
    
    // Note: lwIP socket timeouts are not supported, we'll handle timeouts manually
    
    // Prepare server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(vehicle_info->tcp_port);
    server_addr.sin_addr.s_addr = htonl(vehicle_info->ip_address);
    
    printf("🔍 [DEBUG] Server address prepared:\n");
    printf("   Family: %d\n", server_addr.sin_family);
    printf("   Port: %d (host: %d)\n", server_addr.sin_port, vehicle_info->tcp_port);
    printf("   IP: 0x%08X (host: 0x%08X)\n", server_addr.sin_addr.s_addr, vehicle_info->ip_address);
    
    // Connect to server
    printf("DOIP Client: Attempting to connect to %u.%u.%u.%u:%d\r\n",
           (unsigned)((vehicle_info->ip_address >> 24) & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 16) & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 8) & 0xFF),
           (unsigned)(vehicle_info->ip_address & 0xFF),
           vehicle_info->tcp_port);
    
    printf("🔍 [DEBUG] Calling connect() function...\n");
    result = connect(context->tcp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (result < 0) {
        printf("❌ [DEBUG] connect() failed:\n");
        printf("   Return value: %d\n", result);
        printf("   errno: %d\n", errno);
        
        // Common errno values for connect failures
        switch (errno) {
            case ECONNREFUSED:
                printf("   Error: ECONNREFUSED - Connection refused by server\n");
                break;
            case ETIMEDOUT:
                printf("   Error: ETIMEDOUT - Connection timed out\n");
                break;
            case ENETUNREACH:
                printf("   Error: ENETUNREACH - Network unreachable\n");
                break;
            case EHOSTUNREACH:
                printf("   Error: EHOSTUNREACH - Host unreachable\n");
                break;
            case EINPROGRESS:
                printf("   Error: EINPROGRESS - Connection in progress (non-blocking)\n");
                break;
            default:
                printf("   Error: Unknown error code %d\n", errno);
                break;
        }
        
        printf("DOIP Client: Failed to connect to vehicle (error: %d)\r\n", result);
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("✅ [DEBUG] connect() successful\n");
    printf("DOIP Client: TCP connection established successfully\r\n");
    
    // Send routing activation request to specific ECU
    printf("DOIP Client: Sending routing activation to ECU 0x%04X\r\n", vehicle_info->logical_address);
    
    // Build routing activation request
    doip_utils_create_header(&request_msg, DOIP_ROUTING_ACTIVATION_REQUEST, 7);
    
    // Add routing activation payload
    request_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    request_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    request_msg.payload[2] = (vehicle_info->logical_address >> 8) & 0xFF;
    request_msg.payload[3] = vehicle_info->logical_address & 0xFF;
    request_msg.payload[4] = 0x00; // Routing activation type
    request_msg.payload[5] = 0x00; // Reserved
    request_msg.payload[6] = 0x00; // Reserved
    
    printf("🔍 [DEBUG] Routing activation request built:\n");
    printf("   Payload type: 0x%04X\n", request_msg.payload_type);
    printf("   Payload length: %lu bytes\n", request_msg.payload_length);
    printf("   Source address: 0x%04X\n", DOIP_CLIENT_SOURCE_ADDRESS);
    printf("   Target address: 0x%04X\n", vehicle_info->logical_address);
    
    // Send routing activation request
    printf("🔍 [DEBUG] Sending routing activation request...\n");
    if (!doip_send_tcp_message_socket(context->tcp_socket, &request_msg)) {
        printf("❌ [DEBUG] Failed to send routing activation request\n");
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("✅ [DEBUG] Routing activation request sent successfully\n");
    
    // Wait for routing activation response
    printf("🔍 [DEBUG] Waiting for routing activation response...\n");
    if (!doip_receive_tcp_message_socket(context->tcp_socket, &response_msg, DOIP_TCP_TIMEOUT_MS)) {
        printf("❌ [DEBUG] Failed to receive routing activation response\n");
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    printf("✅ [DEBUG] Routing activation response received\n");
    
    // Check response type
    if (response_msg.payload_type != DOIP_ROUTING_ACTIVATION_RESPONSE) {
        printf("❌ [DEBUG] Unexpected response type: 0x%04X (expected: 0x%04X)\n", 
               response_msg.payload_type, DOIP_ROUTING_ACTIVATION_RESPONSE);
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("✅ [DEBUG] Routing activation response type confirmed\n");
    
    // Check response payload length
    if (response_msg.payload_length < 9) {
        printf("❌ [DEBUG] Routing activation response too short: %lu bytes (expected: 9+)\n", 
               response_msg.payload_length);
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("✅ [DEBUG] Routing activation response length confirmed: %lu bytes\n", response_msg.payload_length);
    
    // Check response code
    uint8_t response_code = response_msg.payload[4];
    printf("🔍 [DEBUG] Routing activation response code: 0x%02X\n", response_code);
    
    if (response_code != 0x10) { // 0x10 = Routing activation successful
        printf("❌ [DEBUG] Routing activation failed with code: 0x%02X\n", response_code);
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("✅ [DEBUG] Routing activation successful\n");
    
    // Store vehicle information
    context->current_vehicle = *vehicle_info;
    context->current_state = DRV_DOIP_STATE_ACTIVATED;
    
    printf("✅ [DEBUG] Connection established successfully:\n");
    printf("   State: %d (ACTIVATED)\n", context->current_state);
    printf("   TCP socket: %d\n", context->tcp_socket);
    printf("   Vehicle IP: 0x%08X\n", context->current_vehicle.ip_address);
    printf("   Vehicle port: %d\n", context->current_vehicle.tcp_port);
    printf("   Logical address: 0x%04X\n", context->current_vehicle.logical_address);
    
    printf("DOIP Client: Connection to vehicle established successfully\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_disconnect_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    printf("DOIP Client: Disconnecting socket\r\n");
    
    if (context->tcp_socket >= 0) {
        close(context->tcp_socket);
        context->tcp_socket = -1;
    }
    
    context->current_state = DRV_DOIP_STATE_IDLE;
    return DRV_DOIP_STATUS_OK;
}

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
    
    printf("🔍 [DEBUG] drv_doip_send_diagnostic_request_impl() called:\n");
    printf("   Service ID: 0x%02X\n", service_id);
    printf("   Data ID: 0x%04X\n", data_id);
    printf("   Request payload length: %zu bytes\n", request_payload_len);
    printf("   Max response length: %zu bytes\n", max_response_len);
    printf("   Current state: %d\n", context->current_state);
    printf("   TCP socket: %d\n", context->tcp_socket);
    
    if (context->current_state != DRV_DOIP_STATE_ACTIVATED) {
        printf("❌ [DEBUG] Not connected or activated (state: %d)\n", context->current_state);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Calculate total payload size: addressing(4) + service_id(1) + data_id(2) + additional_payload
    uint32_t total_payload_size = 4 + 3 + request_payload_len;
    
    printf("🔍 [DEBUG] Payload calculation:\n");
    printf("   Addressing: 4 bytes\n");
    printf("   Service ID: 1 byte\n");
    printf("   Data ID: 2 bytes\n");
    printf("   Additional payload: %zu bytes\n", request_payload_len);
    printf("   Total payload size: %u bytes\n", total_payload_size);
    
    printf("DOIP Socket: Unified diagnostic request - service=0x%02X, data_id=0x%04X, payload_len=%zu, total_size=%u\r\n",
           service_id, data_id, request_payload_len, (unsigned int)total_payload_size);
    
    // Determine if we need large message handling
    if (total_payload_size > DOIP_SMALL_PAYLOAD_SIZE) {
        printf("🔍 [DEBUG] Large message detected (%u > %d)\n", total_payload_size, DOIP_SMALL_PAYLOAD_SIZE);
        
        // For large messages, we'll use a different approach
        doip_large_message_t *large_msg = doip_utils_alloc_large_message(total_payload_size);
        if (large_msg == NULL) {
            printf("❌ [DEBUG] Failed to allocate large message structure\n");
            return DRV_DOIP_STATUS_ERROR;
        }
        
        // Fill large message
        doip_utils_create_header((doip_message_t*)large_msg, DOIP_DIAGNOSTIC_MESSAGE, total_payload_size);
        
        uint8_t *payload_ptr = large_msg->payload;
        
        // Add addressing (4 bytes)
        *payload_ptr++ = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
        *payload_ptr++ = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
        *payload_ptr++ = (context->current_vehicle.logical_address >> 8) & 0xFF;
        *payload_ptr++ = context->current_vehicle.logical_address & 0xFF;
        
        // Add diagnostic payload
        *payload_ptr++ = service_id;
        *payload_ptr++ = (data_id >> 8) & 0xFF;
        *payload_ptr++ = data_id & 0xFF;
        
        // Add additional payload if provided
        if (request_payload_len > 0 && request_payload != NULL) {
            memcpy(payload_ptr, request_payload, request_payload_len);
        }
        
        // TODO: Implement large message socket transmission
        printf("❌ [DEBUG] Large message socket transmission needs full implementation\n");
        
        doip_utils_free_large_message(large_msg);
        *actual_len = 0;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("🔍 [DEBUG] Building small message...\n");
    
    // Handle small messages using existing approach
    doip_message_t request_msg, response_msg;
    doip_utils_create_header(&request_msg, DOIP_DIAGNOSTIC_MESSAGE, total_payload_size);
    
    printf("🔍 [DEBUG] Message structure created:\n");
    printf("   Protocol version: 0x%02X\n", request_msg.protocol_version);
    printf("   Inverse protocol version: 0x%02X\n", request_msg.inverse_protocol_version);
    printf("   Payload type: 0x%04X\n", request_msg.payload_type);
    printf("   Payload length: %lu bytes\n", request_msg.payload_length);
    
    // Add addressing and diagnostic data
    request_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    request_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    request_msg.payload[2] = (context->current_vehicle.logical_address >> 8) & 0xFF;
    request_msg.payload[3] = context->current_vehicle.logical_address & 0xFF;
    request_msg.payload[4] = service_id;
    request_msg.payload[5] = (data_id >> 8) & 0xFF;
    request_msg.payload[6] = data_id & 0xFF;
    
    printf("🔍 [DEBUG] Addressing and diagnostic data added:\n");
    printf("   Source address: 0x%04X\n", DOIP_CLIENT_SOURCE_ADDRESS);
    printf("   Target address: 0x%04X\n", context->current_vehicle.logical_address);
    printf("   Service ID: 0x%02X\n", service_id);
    printf("   Data ID: 0x%04X\n", data_id);
    
    // Add additional payload if provided (for small messages only)
    if (request_payload_len > 0 && request_payload != NULL) {
        if (request_payload_len <= DOIP_SMALL_PAYLOAD_SIZE - 7) { // Ensure we don't overflow
            memcpy(&request_msg.payload[7], request_payload, request_payload_len);
            printf("🔍 [DEBUG] Additional payload copied (%zu bytes)\n", request_payload_len);
        } else {
            printf("❌ [DEBUG] Additional payload too large for small message buffer\n");
            return DRV_DOIP_STATUS_ERROR;
        }
    }
    
    printf("🔍 [DEBUG] About to send diagnostic request...\n");
    
    // Send diagnostic request
    if (!doip_send_tcp_message_socket(context->tcp_socket, &request_msg)) {
        printf("❌ [DEBUG] Failed to send diagnostic request\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("✅ [DEBUG] Diagnostic request sent successfully\n");
    
    // Wait for response
    printf("🔍 [DEBUG] Waiting for diagnostic response...\n");
    if (!doip_receive_tcp_message_socket(context->tcp_socket, &response_msg, DOIP_TCP_TIMEOUT_MS)) {
        printf("❌ [DEBUG] Failed to receive diagnostic response\n");
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    printf("✅ [DEBUG] Diagnostic response received successfully\n");
    
    // Check for negative ACK response first
    if (doip_utils_handle_negative_ack(response_msg.payload_type, response_msg.payload, 
                                       response_msg.payload_length, actual_len)) {
        printf("✅ [DEBUG] Request handled as negative ACK by ECU 0x%04X\n", 
               context->current_vehicle.logical_address);
        return DRV_DOIP_STATUS_OK; // Not an error - ECU doesn't support this request
    }
    
    // Check response type
    if (response_msg.payload_type != DOIP_DIAGNOSTIC_MESSAGE) {
        printf("❌ [DEBUG] Unexpected response type: 0x%04X\n", response_msg.payload_type);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("✅ [DEBUG] Diagnostic response from ECU 0x%04X (%lu bytes)\n", 
           context->current_vehicle.logical_address, response_msg.payload_length);
    
    // Extract diagnostic payload (skip DOIP header and addressing info)
    if (response_msg.payload_length > 4) {
        size_t diag_payload_len = response_msg.payload_length - 4;
        size_t copy_len = (diag_payload_len > max_response_len) ? max_response_len : diag_payload_len;
        
        memcpy(response_buffer, &response_msg.payload[4], copy_len);
        *actual_len = copy_len;
        
        printf("✅ [DEBUG] Diagnostic payload extracted successfully (%zu bytes)\n", copy_len);
        return DRV_DOIP_STATUS_OK;
    }
    
    printf("❌ [DEBUG] Response payload too short (%lu bytes)\n", response_msg.payload_length);
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
    
    if (type < 7) {  // Updated for new callback types
        context->callbacks[type] = callback;
        return DRV_DOIP_STATUS_OK;
    }
    
    return DRV_DOIP_STATUS_ERROR;
}

// Raw DOIP messaging implementations

static drv_doip_status_t drv_doip_send_raw_message_impl(const void *hw_context, uint16_t payload_type,
                                                        const uint8_t *payload_data, uint32_t payload_length,
                                                        bool use_static_buffer)
{
    ASSERT(hw_context != NULL);
    ASSERT(payload_data != NULL || payload_length == 0);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (context->current_state != DRV_DOIP_STATE_CONNECTED && 
        context->current_state != DRV_DOIP_STATE_ACTIVATED) {
        printf("DOIP Socket: Cannot send message - not connected (state: %d)\r\n", context->current_state);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (context->tcp_socket < 0) {
        printf("DOIP Socket: Invalid TCP socket\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Socket: Unified raw message - type=0x%04X, length=%u, static_buffer=%s\r\n", 
           payload_type, (unsigned int)payload_length, use_static_buffer ? "true" : "false");
    
    // Determine if we need large message handling
    if (payload_length > DOIP_SMALL_PAYLOAD_SIZE) {
        printf("DOIP Socket: Large raw message detected (%u bytes) - using dynamic allocation\r\n", (unsigned int)payload_length);
        
        if (use_static_buffer) {
            // Use static buffer allocation
            doip_large_message_t *large_msg = doip_utils_init_static_large_message((uint8_t*)payload_data, payload_length);
            if (large_msg == NULL) {
                printf("DOIP Socket: Failed to initialize static large message\r\n");
                return DRV_DOIP_STATUS_ERROR;
            }
            
            large_msg->protocol_version = DOIP_PROTOCOL_VERSION;
            large_msg->inverse_protocol_version = DOIP_INVERSE_PROTOCOL_VERSION;
            large_msg->payload_type = payload_type;
            large_msg->payload_length = payload_length;
            
            // TODO: Implement large message socket transmission
            printf("DOIP Socket: Large message socket transmission needs implementation\r\n");
            
            doip_utils_release_static_large_message(large_msg);
            return DRV_DOIP_STATUS_ERROR;
        } else {
            // Use heap allocation
            doip_large_message_t *large_msg = doip_utils_alloc_large_message(payload_length);
            if (large_msg == NULL) {
                printf("DOIP Socket: Failed to allocate large message\r\n");
                return DRV_DOIP_STATUS_ERROR;
            }
            
            large_msg->protocol_version = DOIP_PROTOCOL_VERSION;
            large_msg->inverse_protocol_version = DOIP_INVERSE_PROTOCOL_VERSION;
            large_msg->payload_type = payload_type;
            large_msg->payload_length = payload_length;
            
            if (payload_length > 0 && payload_data != NULL) {
                memcpy(large_msg->payload, payload_data, payload_length);
            }
            
            // TODO: Implement large message socket transmission
            printf("DOIP Socket: Large message socket transmission needs implementation\r\n");
            
            doip_utils_free_large_message(large_msg);
            return DRV_DOIP_STATUS_ERROR;
        }
    }
    
    // Handle small messages using existing approach
    doip_message_t msg;
    msg.protocol_version = DOIP_PROTOCOL_VERSION;
    msg.inverse_protocol_version = DOIP_INVERSE_PROTOCOL_VERSION;
    msg.payload_type = payload_type;
    msg.payload_length = payload_length;
    
    if (payload_length > 0 && payload_data != NULL) {
        memcpy(msg.payload, payload_data, payload_length);
    }
    
    // Send the message
    if (!doip_send_tcp_message_socket(context->tcp_socket, &msg)) {
        printf("DOIP Socket: Failed to send raw message (type: 0x%04X)\r\n", payload_type);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Socket: Sent raw message (type: 0x%04X, length: %u)\r\n", 
           payload_type, (unsigned int)payload_length);
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_start_packet_listener_impl(const void *hw_context, const drv_doip_packet_listener_config_t *config)
{
    ASSERT(hw_context != NULL);
    ASSERT(config != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (context->packet_listener_active) {
        printf("DOIP Raw: Packet listener already active\r\n");
        return DRV_DOIP_STATUS_OK;
    }
    
    // Store configuration
    memcpy(&context->packet_listener_config, config, sizeof(drv_doip_packet_listener_config_t));
    context->packet_callback = config->packet_callback;
    
    // Create packet listener task
    BaseType_t result = xTaskCreate(
        doip_packet_listener_task,
        "DOIPPacketListener",
        DOIP_CLIENT_TASK_STACK_SIZE,
        context,
        DOIP_CLIENT_TASK_PRIORITY,
        &context->packet_listener_task_handle
    );
    
    if (result != pdPASS) {
        printf("DOIP Raw: Failed to create packet listener task\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    context->packet_listener_active = true;
    printf("DOIP Raw: Packet listener started (timeout: %lu ms)\r\n", config->timeout_ms);
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_stop_packet_listener_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (!context->packet_listener_active) {
        return DRV_DOIP_STATUS_OK;
    }
    
    context->packet_listener_active = false;
    
    if (context->packet_listener_task_handle != NULL) {
        vTaskDelete(context->packet_listener_task_handle);
        context->packet_listener_task_handle = NULL;
    }
    
    printf("DOIP Raw: Packet listener stopped\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_register_packet_callback_impl(const void *hw_context, drv_doip_packet_callback_t callback)
{
    ASSERT(hw_context != NULL);
    ASSERT(callback != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    context->packet_callback = callback;
    printf("DOIP Raw: Packet callback registered\r\n");
    return DRV_DOIP_STATUS_OK;
}

// Helper function implementations

// Static buffer for sending large messages (8KB) - used only when needed
static uint8_t large_send_buffer[DOIP_HEADER_SIZE + DOIP_LARGE_SEND_BUFFER_SIZE];

static bool doip_send_tcp_message_socket(int socket, const doip_message_t *msg)
{
    uint32_t total_length = DOIP_HEADER_SIZE + msg->payload_length;
    uint8_t *send_buffer;
    
    // ENHANCED DEBUGGING: Log all function parameters and state
    printf("🔍 [DEBUG] doip_send_tcp_message_socket() called:\n");
    printf("   Socket: %d\n", socket);
    printf("   Payload length: %lu bytes\n", msg->payload_length);
    printf("   Total length: %lu bytes\n", total_length);
    printf("   Protocol version: 0x%02X\n", msg->protocol_version);
    printf("   Payload type: 0x%04X\n", msg->payload_type);
    
    // Validate socket
    if (socket < 0) {
        printf("❌ [DEBUG] Invalid socket: %d\n", socket);
        return false;
    }
    
    // Validate message
    if (msg == NULL) {
        printf("❌ [DEBUG] NULL message pointer\n");
        return false;
    }
    
    // Choose buffer based on message size
    if (msg->payload_length <= DOIP_SMALL_PAYLOAD_SIZE) {
        // Use small stack buffer for normal messages
        uint8_t small_buffer[DOIP_HEADER_SIZE + DOIP_SMALL_PAYLOAD_SIZE];
        send_buffer = small_buffer;
        printf("✅ [DEBUG] Using small stack buffer for %lu byte payload\n", msg->payload_length);
    } else if (msg->payload_length <= DOIP_LARGE_SEND_BUFFER_SIZE) {
        // Use static buffer for large messages
        send_buffer = large_send_buffer;
        printf("✅ [DEBUG] Using static large buffer for %lu byte payload\n", msg->payload_length);
    } else {
        printf("❌ [DEBUG] Message too large (%lu bytes, max: %d)\n", 
               msg->payload_length, DOIP_LARGE_SEND_BUFFER_SIZE);
        return false;
    }
    
    // Serialize message using utility function
    printf("🔍 [DEBUG] Serializing message...\n");
    doip_utils_serialize_message(msg, send_buffer);
    printf("✅ [DEBUG] Message serialized successfully\n");
    
    printf("🔍 [DEBUG] About to call send() function:\n");
    printf("   Socket: %d\n", socket);
    printf("   Buffer: %p\n", send_buffer);
    printf("   Length: %lu bytes\n", total_length);
    printf("   Flags: 0\n");
    
    // Send message
    int result = send(socket, send_buffer, total_length, 0);
    
    // ENHANCED ERROR ANALYSIS
    if (result < 0) {
        printf("❌ [DEBUG] send() failed with error:\n");
        printf("   Return value: %d\n", result);
        printf("   errno: %d\n", errno);
        
        // Common errno values and their meanings
        switch (errno) {
            case EBADF:
                printf("   Error: EBADF - Invalid socket descriptor\n");
                break;
            case ENOTSOCK:
                printf("   Error: ENOTSOCK - Not a socket\n");
                break;
            case EFAULT:
                printf("   Error: EFAULT - Invalid buffer address\n");
                break;
            case EMSGSIZE:
                printf("   Error: EMSGSIZE - Message too large\n");
                break;
            case ENOBUFS:
                printf("   Error: ENOBUFS - No buffer space available\n");
                break;
            case ENOTCONN:
                printf("   Error: ENOTCONN - Socket not connected\n");
                break;
            case EPIPE:
                printf("   Error: EPIPE - Connection broken\n");
                break;
            default:
                printf("   Error: Unknown error code %d\n", errno);
                break;
        }
        return false;
    } else if (result != (int)total_length) {
        printf("❌ [DEBUG] send() partial success:\n");
        printf("   Expected: %lu bytes\n", total_length);
        printf("   Actually sent: %d bytes\n", result);
        return false;
    }
    
    printf("✅ [DEBUG] TCP message sent successfully (%d bytes)\n", result);
    printf("✅ [DEBUG] doip_send_tcp_message_socket() completed successfully\n");
    return true;
}

static bool doip_receive_tcp_message_socket(int socket, doip_message_t *msg, uint32_t timeout_ms)
{
    uint8_t header_buffer[DOIP_HEADER_SIZE];
    size_t header_received = 0;
    int bytes_received;
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    // First, receive the header with timeout
    while (header_received < DOIP_HEADER_SIZE) {
        if ((xTaskGetTickCount() - start_time) >= timeout_ticks) {
            printf("DOIP Client: TCP receive timeout during header\r\n");
            return false;
        }
        
        bytes_received = recv(socket, header_buffer + header_received, DOIP_HEADER_SIZE - header_received, 0);
        if (bytes_received > 0) {
            header_received += bytes_received;
        } else if (bytes_received == 0) {
            printf("DOIP Client: TCP connection closed during header receive\r\n");
            return false;
        } else {
            // No data available yet, wait a bit and try again
            vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay
        }
    }
    
    // Parse header using utility function
    msg->protocol_version = header_buffer[0];
    msg->inverse_protocol_version = header_buffer[1];
    msg->payload_type = (header_buffer[2] << 8) | header_buffer[3];
    msg->payload_length = (header_buffer[4] << 24) | (header_buffer[5] << 16) | (header_buffer[6] << 8) | header_buffer[7];
    
    if (!doip_utils_validate_protocol(msg->protocol_version, msg->inverse_protocol_version)) {
        return false;
    }
    
    // Size check - we can receive larger messages but only store up to small buffer size
    if (msg->payload_length > DOIP_LARGE_SEND_BUFFER_SIZE) {
        printf("DOIP Client: Payload too large: %lu bytes (max %d)\r\n", 
               msg->payload_length, DOIP_LARGE_SEND_BUFFER_SIZE);
        return false;
    }
    
    // For large messages, we'll need to handle them differently
    if (msg->payload_length > DOIP_SMALL_PAYLOAD_SIZE) {
        printf("DOIP Client: Large message received (%lu bytes) - will discard excess data\r\n", 
               msg->payload_length);
    }
    
    printf("DOIP Client: Received DOIP header - Type: 0x%04X, Length: %lu\r\n", 
           msg->payload_type, msg->payload_length);
    
    // Receive payload if present
    if (msg->payload_length > 0) {
        size_t payload_received = 0;
        size_t payload_to_store = (msg->payload_length > DOIP_SMALL_PAYLOAD_SIZE) ? DOIP_SMALL_PAYLOAD_SIZE : msg->payload_length;
        uint32_t receive_chunks = 0;
        uint8_t discard_buffer[512]; // Small buffer for discarding excess data
        
        printf("DOIP Client: Starting to receive payload (%lu bytes, storing %zu bytes)\r\n", 
               msg->payload_length, payload_to_store);
        
        while (payload_received < msg->payload_length) {
            if ((xTaskGetTickCount() - start_time) >= timeout_ticks) {
                printf("DOIP Client: TCP receive timeout during payload (received %zu/%lu bytes)\r\n",
                       payload_received, msg->payload_length);
                return false;
            }
            
            size_t bytes_to_receive;
            uint8_t *receive_buffer;
            
            if (payload_received < payload_to_store) {
                // Still receiving data that we want to store
                bytes_to_receive = payload_to_store - payload_received;
                receive_buffer = msg->payload + payload_received;
            } else {
                // Receiving excess data that we need to discard
                bytes_to_receive = msg->payload_length - payload_received;
                if (bytes_to_receive > sizeof(discard_buffer)) {
                    bytes_to_receive = sizeof(discard_buffer);
                }
                receive_buffer = discard_buffer;
            }
            
            bytes_received = recv(socket, receive_buffer, bytes_to_receive, 0);
            if (bytes_received > 0) {
                payload_received += bytes_received;
                receive_chunks++;
                if (receive_chunks % 10 == 0 || msg->payload_length > 1024) {
                    printf("DOIP Client: Payload progress: %zu/%lu bytes (chunk #%lu)\r\n", 
                           payload_received, msg->payload_length, receive_chunks);
                }
            } else if (bytes_received == 0) {
                printf("DOIP Client: TCP connection closed during payload receive (received %zu/%lu bytes)\r\n",
                       payload_received, msg->payload_length);
                return false;
            } else {
                // No data available yet, wait a bit and try again
                vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay
            }
        }
        
        // Update payload length to reflect what we actually stored
        msg->payload_length = payload_to_store;
        
        printf("DOIP Client: Payload reception completed (%zu bytes stored in %lu chunks)\r\n", 
               payload_to_store, receive_chunks);
    }
    
    printf("DOIP Client: Successfully received DOIP message (%zu total bytes)\r\n", 
           DOIP_HEADER_SIZE + msg->payload_length);
    return true;
}



// Alive check function implementations
static drv_doip_status_t doip_send_alive_check_request_socket(drv_doip_hw_context_t *context)
{
    doip_message_t request_msg;
    
    // Create alive check request message using utility
    doip_utils_create_header(&request_msg, DOIP_ALIVE_CHECK_REQUEST, 2);
    request_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    request_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    
    // Send the message
    if (!doip_send_tcp_message_socket(context->tcp_socket, &request_msg)) {
        printf("DOIP Client: Failed to send alive check request\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Alive check request sent (10 bytes)\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t doip_handle_alive_check_response_socket(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length)
{
    uint16_t source_address;
    
    printf("DOIP Client: Alive check response received\r\n");
    
    if (doip_utils_handle_alive_check_payload(payload, payload_length, &source_address)) {
        printf("DOIP Client: Alive check response received from 0x%04X\r\n", source_address);
    }
    
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t doip_handle_alive_check_request_socket(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length)
{
    doip_message_t response_msg;
    
    if (payload_length < 2) {
        printf("DOIP Client: Invalid alive check request payload length\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Alive check request received, sending response\r\n");
    
    // Create alive check response message using utility
    doip_utils_create_header(&response_msg, DOIP_ALIVE_CHECK_RESPONSE, 2);
    response_msg.payload[0] = payload[0];
    response_msg.payload[1] = payload[1];
    
    // Send the response
    if (!doip_send_tcp_message_socket(context->tcp_socket, &response_msg)) {
        printf("DOIP Client: Failed to send alive check response\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Alive check response sent (10 bytes)\r\n");
    return DRV_DOIP_STATUS_OK;
}

// Background packet listener task implementation
static void doip_packet_listener_task(void *pvParameters)
{
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)pvParameters;
    doip_message_t received_msg;
    drv_doip_raw_packet_t raw_packet;
    
    printf("DOIP Listener: Packet listener task started\r\n");
    
    while (context->packet_listener_active) {
        // Wait for incoming DOIP packets
        if (context->tcp_socket >= 0 && 
            (context->current_state == DRV_DOIP_STATE_CONNECTED || 
             context->current_state == DRV_DOIP_STATE_ACTIVATED)) {
            
            // Try to receive a message with timeout
            if (doip_receive_tcp_message_socket(context->tcp_socket, &received_msg, 
                                              context->packet_listener_config.timeout_ms)) {
                
                printf("DOIP Listener: Received packet (type: 0x%04X, length: %lu)\r\n", 
                       received_msg.payload_type, received_msg.payload_length);
                
                // Convert to raw packet format
                if (doip_convert_message_to_raw_packet(&received_msg, 
                                                     context->current_vehicle.ip_address,
                                                     context->current_vehicle.tcp_port,
                                                     &raw_packet)) {
                    
                    // Add timing information
                    raw_packet.timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    
                    // Call packet callback if registered
                    if (context->packet_callback != NULL) {
                        context->packet_callback(&raw_packet);
                    }
                    
                    // Also trigger generic callback if registered
                    if (context->callbacks[DRV_DOIP_CB_RAW_PACKET_RECEIVED] != NULL) {
                        context->callbacks[DRV_DOIP_CB_RAW_PACKET_RECEIVED](
                            DRV_DOIP_CB_RAW_PACKET_RECEIVED, 
                            &raw_packet, 
                            sizeof(drv_doip_raw_packet_t));
                    }
                }
            }
        } else {
            // No active connection, wait a bit before retrying
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        // Give other tasks a chance to run
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    printf("DOIP Listener: Packet listener task terminating\r\n");
    context->packet_listener_task_handle = NULL;
    vTaskDelete(NULL);
}

// Convert DOIP message to raw packet structure
static bool doip_convert_message_to_raw_packet(const doip_message_t *msg, uint32_t source_ip, uint16_t source_port, drv_doip_raw_packet_t *raw_packet)
{
    if (msg == NULL || raw_packet == NULL) {
        return false;
    }
    
    // Clear the raw packet structure
    memset(raw_packet, 0, sizeof(drv_doip_raw_packet_t));
    
    // Copy header information
    raw_packet->protocol_version = msg->protocol_version;
    raw_packet->inverse_protocol_version = msg->inverse_protocol_version;
    raw_packet->payload_type = msg->payload_type;
    raw_packet->payload_length = msg->payload_length;
    
    // Copy payload data
    if (msg->payload_length > 0 && msg->payload_length <= DOIP_MAX_PAYLOAD_SIZE) {
        memcpy(raw_packet->payload, msg->payload, msg->payload_length);
        raw_packet->actual_payload_length = msg->payload_length;
    } else {
        raw_packet->actual_payload_length = 0;
    }
    
    // Set source information
    raw_packet->source_ip_address = source_ip;
    raw_packet->source_port = source_port;
    
    // For now, assume single packet (no fragmentation)
    raw_packet->is_fragmented = false;
    raw_packet->fragment_index = 0;
    raw_packet->total_fragments = 1;
    raw_packet->total_message_length = msg->payload_length;
    
    return true;
}

