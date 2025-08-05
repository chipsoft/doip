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
static drv_doip_status_t drv_doip_start_task_impl(const void *hw_context);
static drv_doip_status_t drv_doip_stop_task_impl(const void *hw_context);
static drv_doip_status_t drv_doip_discover_vehicles_impl(const void *hw_context, drv_doip_vehicle_info_t *vehicle_info);
static drv_doip_status_t drv_doip_connect_to_vehicle_impl(const void *hw_context, const drv_doip_vehicle_info_t *vehicle_info);
static drv_doip_status_t drv_doip_disconnect_impl(const void *hw_context);
static drv_doip_status_t drv_doip_send_diagnostic_request_impl(const void *hw_context, uint8_t service_id, uint16_t data_id, uint8_t *response, size_t max_response_len, size_t *actual_len);
static drv_doip_status_t drv_doip_read_vin_impl(const void *hw_context, char *vin_buffer);
static drv_doip_status_t drv_doip_read_ecu_software_version_impl(const void *hw_context, char *version_buffer, size_t buffer_size);
static drv_doip_status_t drv_doip_read_ecu_hardware_version_impl(const void *hw_context, char *version_buffer, size_t buffer_size);
static drv_doip_status_t drv_doip_read_monitoring_data_impl(const void *hw_context, uint16_t did, uint8_t *response, size_t max_response_len, size_t *actual_len);
static drv_doip_status_t drv_doip_get_system_monitoring_data_impl(const void *hw_context, drv_doip_system_monitoring_t *monitoring_data);
static drv_doip_status_t drv_doip_read_active_diagnostic_session_impl(const void *hw_context, uint8_t *session_buffer, size_t buffer_size);
static drv_doip_status_t drv_doip_read_ecu_serial_number_impl(const void *hw_context, char *serial_buffer, size_t buffer_size);
static drv_doip_status_t drv_doip_read_vehicle_speed_impl(const void *hw_context, uint16_t *speed_kmh);
static drv_doip_status_t drv_doip_read_engine_rpm_impl(const void *hw_context, uint16_t *rpm);
static drv_doip_status_t drv_doip_read_battery_voltage_impl(const void *hw_context, uint16_t *voltage_mv);
static drv_doip_status_t drv_doip_read_temperature_data_impl(const void *hw_context, int16_t *temperature_celsius);
static drv_doip_status_t drv_doip_read_fuel_level_impl(const void *hw_context, uint8_t *fuel_percent);
static drv_doip_state_t drv_doip_get_status_impl(const void *hw_context);
static drv_doip_status_t drv_doip_register_callback_impl(const void *hw_context, drv_doip_cb_type_t type, drv_doip_callback_t callback);

// Task function
static void doip_client_task(void *pvParameters);

// Global driver instance
drv_doip_t doip_0 = {
    .is_init = false,
    .is_task_running = false,
    .current_state = DRV_DOIP_STATE_IDLE,
    .hw_context = &drv_doip_hw_context_0,
    .init = drv_doip_init_impl,
    .deinit = drv_doip_deinit_impl,
    .start_task = drv_doip_start_task_impl,
    .stop_task = drv_doip_stop_task_impl,
    .discover_vehicles = drv_doip_discover_vehicles_impl,
    .connect_to_vehicle = drv_doip_connect_to_vehicle_impl,
    .disconnect = drv_doip_disconnect_impl,
    .send_diagnostic_request = drv_doip_send_diagnostic_request_impl,
    .read_vin = drv_doip_read_vin_impl,
    .read_ecu_software_version = drv_doip_read_ecu_software_version_impl,
    .read_ecu_hardware_version = drv_doip_read_ecu_hardware_version_impl,
    .read_monitoring_data = drv_doip_read_monitoring_data_impl,
    .get_system_monitoring_data = drv_doip_get_system_monitoring_data_impl,
    .read_active_diagnostic_session = drv_doip_read_active_diagnostic_session_impl,
    .read_ecu_serial_number = drv_doip_read_ecu_serial_number_impl,
    .read_vehicle_speed = drv_doip_read_vehicle_speed_impl,
    .read_engine_rpm = drv_doip_read_engine_rpm_impl,
    .read_battery_voltage = drv_doip_read_battery_voltage_impl,
    .read_temperature_data = drv_doip_read_temperature_data_impl,
    .read_fuel_level = drv_doip_read_fuel_level_impl,
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
    
    // Initialize system monitoring data
    doip_utils_init_monitoring_data(&context->monitoring_data);
    
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
    
    // Stop task if running
    if (context->client_task_handle != NULL) {
        vTaskDelete(context->client_task_handle);
        context->client_task_handle = NULL;
    }
    
    // Close socket
    if (context->tcp_socket >= 0) {
        close(context->tcp_socket);
        context->tcp_socket = -1;
    }
    
    context->current_state = DRV_DOIP_STATE_IDLE;
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_start_task_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (context->client_task_handle != NULL) {
        return DRV_DOIP_STATUS_OK; // Already running
    }
    
    if (xTaskCreate(doip_client_task,
                    "DOIPClient",
                    DOIP_CLIENT_TASK_STACK_SIZE,
                    (void *)context,
                    DOIP_CLIENT_TASK_PRIORITY,
                    &context->client_task_handle) != pdPASS) {
        printf("DOIP Client: Failed to create client task\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Task started successfully\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_stop_task_impl(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (context->client_task_handle != NULL) {
        vTaskDelete(context->client_task_handle);
        context->client_task_handle = NULL;
    }
    
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

static drv_doip_status_t drv_doip_read_vin_impl(const void *hw_context, char *vin_buffer)
{
    ASSERT(hw_context != NULL);
    ASSERT(vin_buffer != NULL);
    
    uint8_t response[32];
    size_t response_len;
    
    drv_doip_status_t result = drv_doip_send_diagnostic_request_impl(hw_context, UDS_READ_DATA_BY_IDENTIFIER, DID_VIN, response, sizeof(response), &response_len);
    
    if (result == DRV_DOIP_STATUS_OK && response_len > 3 && response[0] == (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK)) {
        // Extract VIN from positive response
        size_t vin_len = response_len - 3;  // Skip service ID and DID
        if (vin_len > 17) vin_len = 17;
        
        memcpy(vin_buffer, &response[3], vin_len);
        vin_buffer[vin_len] = '\0';
        
        printf("DOIP Client: VIN: %s\r\n", vin_buffer);
        return DRV_DOIP_STATUS_OK;
    }
    
    // Return mock VIN on failure
    strcpy(vin_buffer, "MOCK_VIN_SOCKET");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_ecu_software_version_impl(const void *hw_context, char *version_buffer, size_t buffer_size)
{
    ASSERT(hw_context != NULL);
    ASSERT(version_buffer != NULL);
    
    strncpy(version_buffer, "v1.2.3-socket", buffer_size - 1);
    version_buffer[buffer_size - 1] = '\0';
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_ecu_hardware_version_impl(const void *hw_context, char *version_buffer, size_t buffer_size)
{
    ASSERT(hw_context != NULL);
    ASSERT(version_buffer != NULL);
    
    strncpy(version_buffer, "HW_v2.0-socket", buffer_size - 1);
    version_buffer[buffer_size - 1] = '\0';
    return DRV_DOIP_STATUS_OK;
}

// Simplified implementations for remaining functions
static drv_doip_status_t drv_doip_read_monitoring_data_impl(const void *hw_context, uint16_t did, uint8_t *response, size_t max_response_len, size_t *actual_len)
{
    doip_utils_update_dynamic_data(&((drv_doip_hw_context_t *)hw_context)->monitoring_data);
    return drv_doip_send_diagnostic_request_impl(hw_context, UDS_READ_DATA_BY_IDENTIFIER, did, response, max_response_len, actual_len);
}

static drv_doip_status_t drv_doip_get_system_monitoring_data_impl(const void *hw_context, drv_doip_system_monitoring_t *monitoring_data)
{
    ASSERT(hw_context != NULL);
    ASSERT(monitoring_data != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    doip_utils_update_dynamic_data(&context->monitoring_data);
    memcpy(monitoring_data, &context->monitoring_data, sizeof(drv_doip_system_monitoring_t));
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_active_diagnostic_session_impl(const void *hw_context, uint8_t *session_buffer, size_t buffer_size)
{
    if (buffer_size > 0) {
        session_buffer[0] = 0x01; // Default session
    }
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_ecu_serial_number_impl(const void *hw_context, char *serial_buffer, size_t buffer_size)
{
    strncpy(serial_buffer, "SAME54P20A-SOCKET-001", buffer_size - 1);
    serial_buffer[buffer_size - 1] = '\0';
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_vehicle_speed_impl(const void *hw_context, uint16_t *speed_kmh)
{
    *speed_kmh = 0; // Mock stationary
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_engine_rpm_impl(const void *hw_context, uint16_t *rpm)
{
    *rpm = 800; // Mock idle RPM
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_battery_voltage_impl(const void *hw_context, uint16_t *voltage_mv)
{
    *voltage_mv = 12750; // Mock 12.75V
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_temperature_data_impl(const void *hw_context, int16_t *temperature_celsius)
{
    *temperature_celsius = 250; // Mock 25.0°C
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_fuel_level_impl(const void *hw_context, uint8_t *fuel_percent)
{
    *fuel_percent = 85; // Mock 85%
    return DRV_DOIP_STATUS_OK;
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

// Task function - performs full DOIP operations
static void doip_client_task(void *pvParameters)
{
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)pvParameters;
    drv_doip_vehicle_info_t vehicle_info;
    char vin_buffer[18];
    char version_buffer[64];
    uint16_t speed_kmh, rpm, voltage_mv;
    int16_t temperature;
    uint8_t fuel_percent;
    
    printf("DOIP Client: Task started (Socket mode)\r\n");
    
    // Wait a bit for network to be fully ready
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    while (1) {
        // Only proceed if we're in idle state (not connected)
        if (context->current_state == DRV_DOIP_STATE_IDLE) {
            printf("DOIP Client: Starting vehicle discovery (Socket)...\r\n");
            
            // Discover vehicles
            if (drv_doip_discover_vehicles_impl(context, &vehicle_info) == DRV_DOIP_STATUS_OK) {
                printf("DOIP Client: Vehicle discovered, attempting connection (Socket)...\r\n");
                
                // Connect to discovered vehicle
                if (drv_doip_connect_to_vehicle_impl(context, &vehicle_info) == DRV_DOIP_STATUS_OK) {
                    printf("\r\n--- Reading Vehicle Information (Socket) ---\r\n");
                    
                    // Read VIN
                    if (drv_doip_read_vin_impl(context, vin_buffer) == DRV_DOIP_STATUS_OK) {
                        printf("VIN: %s\r\n", vin_buffer);
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    // Read ECU software version
                    if (drv_doip_read_ecu_software_version_impl(context, version_buffer, sizeof(version_buffer)) == DRV_DOIP_STATUS_OK) {
                        printf("ECU SW Version: %s\r\n", version_buffer);
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    // Read ECU hardware version
                    if (drv_doip_read_ecu_hardware_version_impl(context, version_buffer, sizeof(version_buffer)) == DRV_DOIP_STATUS_OK) {
                        printf("ECU HW Version: %s\r\n", version_buffer);
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    printf("\r\n--- Reading Monitoring Data (Socket) ---\r\n");
                    
                    // Read vehicle speed
                    if (drv_doip_read_vehicle_speed_impl(context, &speed_kmh) == DRV_DOIP_STATUS_OK) {
                        printf("Vehicle Speed: %d km/h\r\n", speed_kmh);
                    }
                    
                    // Read engine RPM
                    if (drv_doip_read_engine_rpm_impl(context, &rpm) == DRV_DOIP_STATUS_OK) {
                        printf("Engine RPM: %d\r\n", rpm);
                    }
                    
                    // Read battery voltage
                    if (drv_doip_read_battery_voltage_impl(context, &voltage_mv) == DRV_DOIP_STATUS_OK) {
                        printf("Battery Voltage: %d.%03d V\r\n", voltage_mv / 1000, voltage_mv % 1000);
                    }
                    
                    // Read temperature
                    if (drv_doip_read_temperature_data_impl(context, &temperature) == DRV_DOIP_STATUS_OK) {
                        printf("Temperature: %d.%d °C\r\n", temperature / 10, temperature % 10);
                    }
                    
                    // Read fuel level
                    if (drv_doip_read_fuel_level_impl(context, &fuel_percent) == DRV_DOIP_STATUS_OK) {
                        printf("Fuel Level: %d%%\r\n", fuel_percent);
                    }
                    
                    printf("\r\n--- DOIP Communication Complete (Socket) ---\r\n");
                    
                    // Display comprehensive monitoring data
                    doip_utils_update_dynamic_data(&context->monitoring_data);
                    doip_utils_display_server_data(&context->monitoring_data);
                    
                    // Test alive check functionality
                    printf("\r\n--- Testing Alive Check ---\r\n");
                    if (doip_send_alive_check_request_socket(context) == DRV_DOIP_STATUS_OK) {
                        printf("DOIP Client: Alive check request sent successfully\r\n");
                        
                        // Set socket to non-blocking mode for alive check listening
                        int nonblock = 1;
                        if (ioctlsocket(context->tcp_socket, FIONBIO, &nonblock) != 0) {
                            printf("DOIP Client: Warning - could not set socket to non-blocking mode\r\n");
                        }
                        
                        // Listen for incoming messages for a short time
                        printf("DOIP Client: Listening for ECU messages...\r\n");
                        TickType_t start_time = xTaskGetTickCount();
                        TickType_t timeout_ticks = pdMS_TO_TICKS(3000); // 3 second timeout
                        
                        while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
                            uint8_t buffer[1024];
                            int bytes_received = recv(context->tcp_socket, buffer, sizeof(buffer), 0);
                            
                            if (bytes_received > 0) {
                                printf("DOIP Client: Received %d bytes during alive check listening\r\n", bytes_received);
                                
                                // Parse DOIP header if we have enough data
                                if (bytes_received >= DOIP_HEADER_SIZE) {
                                    uint16_t payload_type = (buffer[2] << 8) | buffer[3];
                                    uint32_t payload_length = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
                                    
                                    printf("DOIP Client: Received message - Type: 0x%04X, Length: %lu bytes\r\n", 
                                           payload_type, payload_length);
                                    
                                    // Handle alive check messages
                                    if (payload_type == DOIP_ALIVE_CHECK_REQUEST && bytes_received >= (DOIP_HEADER_SIZE + payload_length)) {
                                        printf("DOIP Client: Handling alive check request from ECU\r\n");
                                        doip_handle_alive_check_request_socket(context, &buffer[DOIP_HEADER_SIZE], payload_length);
                                    } else if (payload_type == DOIP_ALIVE_CHECK_RESPONSE && bytes_received >= (DOIP_HEADER_SIZE + payload_length)) {
                                        printf("DOIP Client: Handling alive check response from ECU\r\n");
                                        doip_handle_alive_check_response_socket(context, &buffer[DOIP_HEADER_SIZE], payload_length);
                                    } else {
                                        printf("DOIP Client: Received other message type: 0x%04X\r\n", payload_type);
                                    }
                                }
                            } else if (bytes_received == 0) {
                                printf("DOIP Client: TCP connection closed during alive check listening\r\n");
                                break;
                            } else {
                                // No data available, continue waiting
                                vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay
                            }
                        }
                        
                        printf("DOIP Client: Alive check testing completed\r\n");
                    } else {
                        printf("DOIP Client: Failed to send alive check request\r\n");
                    }
                    
                    // Disconnect after reading data
                    drv_doip_disconnect_impl(context);
                    
                    // Wait before next cycle
                    vTaskDelay(pdMS_TO_TICKS(30000)); // 30 seconds
                } else {
                    printf("DOIP Client: [SOCKET] Connection failed\r\n");
                    vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds before retry
                }
            } else {
                printf("DOIP Client: No vehicles discovered (Socket)\r\n");
                vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds before retry
            }
        } else {
            // If in connected state, perform periodic alive checks or monitoring
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
        
        // Update dynamic monitoring data
        doip_utils_update_dynamic_data(&context->monitoring_data);
    }
}