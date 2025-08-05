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


// Hardware context structure
typedef struct {
    // State management
    drv_doip_state_t current_state;
    TaskHandle_t client_task_handle;
    
    // Vehicle information
    drv_doip_vehicle_info_t current_vehicle;
    
    // Socket-based resources
    int tcp_socket;
    
    // System monitoring data
    drv_doip_system_monitoring_t monitoring_data;
    
    // Callbacks
    drv_doip_callback_t callbacks[5]; // Array for different callback types
} drv_doip_hw_context_t;

// Static hardware context
static drv_doip_hw_context_t drv_doip_hw_context_0 = {
    .current_state = DRV_DOIP_STATE_IDLE,
    .client_task_handle = NULL,
    .tcp_socket = -1,
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
    doip_message_t request_msg, response_msg;
    uint8_t buffer[1024];
    int result;
    
    printf("DOIP Client: Discovering vehicles via socket API\r\n");
    context->current_state = DRV_DOIP_STATE_DISCOVERING;
    
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
    
    printf("DOIP Client: Discovery request sent, waiting for response...\r\n");
    
    // Wait for response with manual timeout handling
    addr_len = sizeof(response_addr);
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(DOIP_DISCOVERY_TIMEOUT_MS);
    
    result = -1;
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        result = recvfrom(udp_socket, buffer, sizeof(buffer), 0,
                          (struct sockaddr*)&response_addr, &addr_len);
        
        if (result > 0) {
            printf("DOIP Client: Received response (%d bytes)\r\n", result);
            break;
        } else if (result == 0) {
            printf("DOIP Client: Connection closed during discovery\r\n");
            break;
        } else {
            // No data available yet, wait a bit and try again
            vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay
        }
    }
    
    close(udp_socket);
    
    if (result <= 0) {
        if ((xTaskGetTickCount() - start_time) >= timeout_ticks) {
            printf("DOIP Client: Discovery timeout - no response received\r\n");
        } else {
            printf("DOIP Client: Discovery failed - connection issue\r\n");
        }
        context->current_state = DRV_DOIP_STATE_IDLE;
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    // Parse response
    if (!doip_utils_parse_header(buffer, result, &response_msg)) {
        printf("DOIP Client: Invalid discovery response header\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (response_msg.payload_type != DOIP_VEHICLE_IDENTIFICATION_RESPONSE) {
        printf("DOIP Client: Unexpected response type: 0x%04X\r\n", response_msg.payload_type);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Extract vehicle information (simplified parsing)
    if (response_msg.payload_length >= 17) {
        memcpy(vehicle_info->vin, response_msg.payload, 17);
        vehicle_info->vin[17] = '\0';
    } else {
        strcpy(vehicle_info->vin, "MOCK_VIN_SOCKET");
    }
    
    vehicle_info->logical_address = 0x1001;
    vehicle_info->ip_address = ntohl(response_addr.sin_addr.s_addr);
    vehicle_info->tcp_port = DOIP_TCP_DATA_PORT;
    
    context->current_state = DRV_DOIP_STATE_DISCOVERED;
    
    printf("DOIP Client: Vehicle discovered via socket\r\n");
    printf("  VIN: %s\r\n", vehicle_info->vin);
    printf("  Logical Address: 0x%04X\r\n", vehicle_info->logical_address);
    printf("  IP Address: %u.%u.%u.%u:%d\r\n", 
           (unsigned)((vehicle_info->ip_address >> 24) & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 16) & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 8) & 0xFF),
           (unsigned)(vehicle_info->ip_address & 0xFF),
           vehicle_info->tcp_port);
    
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
    
    printf("DOIP Client: Connecting to vehicle via socket\r\n");
    context->current_state = DRV_DOIP_STATE_CONNECTING;
    
    // Create TCP socket
    context->tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (context->tcp_socket < 0) {
        printf("DOIP Client: Failed to create TCP socket\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Note: lwIP socket timeouts are not supported, we'll handle timeouts manually
    
    // Prepare server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(vehicle_info->tcp_port);
    server_addr.sin_addr.s_addr = htonl(vehicle_info->ip_address);
    
    // Connect to server
    printf("DOIP Client: Attempting to connect to %u.%u.%u.%u:%d\r\n",
           (unsigned)((vehicle_info->ip_address >> 24) & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 16) & 0xFF),
           (unsigned)((vehicle_info->ip_address >> 8) & 0xFF),
           (unsigned)(vehicle_info->ip_address & 0xFF),
           vehicle_info->tcp_port);
    
    result = connect(context->tcp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0) {
        printf("DOIP Client: Failed to connect to vehicle (error: %d)\r\n", result);
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: TCP connection established successfully\r\n");
    
    // Send routing activation request
    doip_utils_create_header(&request_msg, DOIP_ROUTING_ACTIVATION_REQUEST, 7);
    request_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    request_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    request_msg.payload[2] = 0x00; // Activation type
    request_msg.payload[3] = 0x00;
    request_msg.payload[4] = 0x00;
    request_msg.payload[5] = 0x00;
    request_msg.payload[6] = 0x00; // Reserved
    
    if (!doip_send_tcp_message_socket(context->tcp_socket, &request_msg)) {
        printf("DOIP Client: Failed to send routing activation request\r\n");
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Wait for routing activation response
    if (!doip_receive_tcp_message_socket(context->tcp_socket, &response_msg, DOIP_TCP_TIMEOUT_MS)) {
        printf("DOIP Client: Failed to receive routing activation response\r\n");
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (response_msg.payload_type != DOIP_ROUTING_ACTIVATION_RESPONSE) {
        printf("DOIP Client: Unexpected response type: 0x%04X\r\n", response_msg.payload_type);
        close(context->tcp_socket);
        context->tcp_socket = -1;
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Debug: Print payload bytes
    printf("DOIP Client: Routing activation response payload (%lu bytes): ", response_msg.payload_length);
    for (uint32_t i = 0; i < response_msg.payload_length && i < 16; i++) {
        printf("0x%02X ", response_msg.payload[i]);
    }
    printf("\r\n");
    
    // Check activation response code - correct format
    if (response_msg.payload_length >= 5) {
        uint8_t response_code = response_msg.payload[4]; // Response code at 5th byte
        printf("DOIP Client: Routing activation response code: 0x%02X\r\n", response_code);
        
        if (response_code == 0x10) {
            printf("DOIP Client: Routing activation successful\r\n");
            context->current_state = DRV_DOIP_STATE_ACTIVATED;
            
            // Store vehicle info
            memcpy(&context->current_vehicle, vehicle_info, sizeof(drv_doip_vehicle_info_t));
            
            return DRV_DOIP_STATUS_OK;
        } else {
            printf("DOIP Client: Routing activation failed with response code: 0x%02X\r\n", response_code);
        }
    } else {
        printf("DOIP Client: Routing activation response payload too short: %lu bytes (expected >= 5)\r\n", response_msg.payload_length);
    }
    
    close(context->tcp_socket);
    context->tcp_socket = -1;
    context->current_state = DRV_DOIP_STATE_ERROR;
    return DRV_DOIP_STATUS_ERROR;
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

static drv_doip_status_t drv_doip_send_diagnostic_request_impl(const void *hw_context, uint8_t service_id, uint16_t data_id, uint8_t *response, size_t max_response_len, size_t *actual_len)
{
    ASSERT(hw_context != NULL);
    ASSERT(response != NULL);
    ASSERT(actual_len != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    doip_message_t request_msg, response_msg;
    
    if (context->current_state != DRV_DOIP_STATE_ACTIVATED) {
        printf("DOIP Client: Not connected or activated\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Prepare diagnostic message
    doip_utils_create_header(&request_msg, DOIP_DIAGNOSTIC_MESSAGE, 7);
    request_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    request_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    request_msg.payload[2] = (context->current_vehicle.logical_address >> 8) & 0xFF;
    request_msg.payload[3] = context->current_vehicle.logical_address & 0xFF;
    request_msg.payload[4] = service_id;
    request_msg.payload[5] = (data_id >> 8) & 0xFF;
    request_msg.payload[6] = data_id & 0xFF;
    
    // Send diagnostic request
    if (!doip_send_tcp_message_socket(context->tcp_socket, &request_msg)) {
        printf("DOIP Client: Failed to send diagnostic request\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Wait for response
    if (!doip_receive_tcp_message_socket(context->tcp_socket, &response_msg, DOIP_TCP_TIMEOUT_MS)) {
        printf("DOIP Client: Failed to receive diagnostic response\r\n");
        return DRV_DOIP_STATUS_TIMEOUT;
    }
    
    // Check response type
    if (response_msg.payload_type != DOIP_DIAGNOSTIC_MESSAGE) {
        printf("DOIP Client: Unexpected response type: 0x%04X\r\n", response_msg.payload_type);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Extract diagnostic payload (skip DOIP header and addressing info)
    if (response_msg.payload_length > 4) {
        size_t diag_payload_len = response_msg.payload_length - 4;
        size_t copy_len = (diag_payload_len > max_response_len) ? max_response_len : diag_payload_len;
        
        memcpy(response, &response_msg.payload[4], copy_len);
        *actual_len = copy_len;
        
        return DRV_DOIP_STATUS_OK;
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

static bool doip_send_tcp_message_socket(int socket, const doip_message_t *msg)
{
    uint8_t buffer[DOIP_HEADER_SIZE + DOIP_MAX_PAYLOAD_SIZE];
    uint32_t total_length = DOIP_HEADER_SIZE + msg->payload_length;
    
    // Serialize message using utility function
    doip_utils_serialize_message(msg, buffer);
    
    // Send message
    int result = send(socket, buffer, total_length, 0);
    return (result == (int)total_length);
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
    
    if (msg->payload_length > DOIP_MAX_PAYLOAD_SIZE) {
        printf("DOIP Client: Payload too large: %lu bytes (max %d)\r\n", 
               msg->payload_length, DOIP_MAX_PAYLOAD_SIZE);
        return false;
    }
    
    printf("DOIP Client: Received DOIP header - Type: 0x%04X, Length: %lu\r\n", 
           msg->payload_type, msg->payload_length);
    
    // Receive payload if present
    if (msg->payload_length > 0) {
        size_t payload_received = 0;
        
        while (payload_received < msg->payload_length) {
            if ((xTaskGetTickCount() - start_time) >= timeout_ticks) {
                printf("DOIP Client: TCP receive timeout during payload\r\n");
                return false;
            }
            
            bytes_received = recv(socket, msg->payload + payload_received, msg->payload_length - payload_received, 0);
            if (bytes_received > 0) {
                payload_received += bytes_received;
            } else if (bytes_received == 0) {
                printf("DOIP Client: TCP connection closed during payload receive\r\n");
                return false;
            } else {
                // No data available yet, wait a bit and try again
                vTaskDelay(pdMS_TO_TICKS(10)); // 10ms delay
            }
        }
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

