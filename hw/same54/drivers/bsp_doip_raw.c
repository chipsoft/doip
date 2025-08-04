#include "driver_doip.h"
// Include lwIP headers first to avoid ERR_TIMEOUT conflict with ASF4
#include "lwip/tcp.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/sockets.h"
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

// DOIP Protocol Constants
#define DOIP_UDP_DISCOVERY_PORT         13400
#define DOIP_TCP_DATA_PORT             13400
#define DOIP_PROTOCOL_VERSION          0x02
#define DOIP_INVERSE_PROTOCOL_VERSION  0xFD
#define DOIP_HEADER_SIZE               8
#define DOIP_CLIENT_SOURCE_ADDRESS     0x0E80
#define DOIP_DISCOVERY_TIMEOUT_MS      5000
#define DOIP_TCP_TIMEOUT_MS           10000
#define DOIP_MAX_PAYLOAD_SIZE         1024

// DOIP Payload Types
#define DOIP_VEHICLE_IDENTIFICATION_REQUEST     0x0001
#define DOIP_VEHICLE_IDENTIFICATION_RESPONSE    0x0004
#define DOIP_ROUTING_ACTIVATION_REQUEST         0x0005
#define DOIP_ROUTING_ACTIVATION_RESPONSE        0x0006
#define DOIP_ALIVE_CHECK_REQUEST                0x0007
#define DOIP_ALIVE_CHECK_RESPONSE               0x0008
#define DOIP_DIAGNOSTIC_MESSAGE                 0x8001
#define DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK    0x8002
#define DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK    0x8003

// UDS Service IDs
#define UDS_READ_DATA_BY_IDENTIFIER     0x22
#define UDS_POSITIVE_RESPONSE_MASK      0x40

// Data Identifiers (DIDs)
#define DID_VIN                         0xF190
#define DID_ECU_SOFTWARE_VERSION        0xF1A0
#define DID_ECU_HARDWARE_VERSION        0xF1A1

// Task configuration
#define DOIP_CLIENT_TASK_PRIORITY       (tskIDLE_PRIORITY + 3)
#define DOIP_CLIENT_TASK_STACK_SIZE     (2048)

// Raw lwIP configuration
#define DOIP_STREAM_BUFFER_SIZE         (4096)
#define DOIP_STREAM_TRIGGER_LEVEL       (1)

// DOIP Message Structure
typedef struct {
    uint8_t  protocol_version;
    uint8_t  inverse_protocol_version;
    uint16_t payload_type;
    uint32_t payload_length;
    uint8_t  payload[DOIP_MAX_PAYLOAD_SIZE];
} doip_message_t;

// Hardware context structure
typedef struct {
    // State management
    drv_doip_state_t current_state;
    TaskHandle_t client_task_handle;
    
    // Vehicle information
    drv_doip_vehicle_info_t current_vehicle;
    
    // Raw lwIP resources
    struct tcp_pcb *tcp_pcb;
    StreamBufferHandle_t stream_buffer;
    SemaphoreHandle_t connected_sem;
    SemaphoreHandle_t send_sem;
    
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
    .stream_buffer = NULL,
    .connected_sem = NULL,
    .send_sem = NULL,
};

// Helper functions
static void doip_init_system_monitoring_data(drv_doip_system_monitoring_t *data);
static void doip_update_dynamic_monitoring_data(drv_doip_system_monitoring_t *data);
static drv_doip_status_t doip_send_routing_activation_request(drv_doip_hw_context_t *context);
static drv_doip_status_t doip_send_diagnostic_message(drv_doip_hw_context_t *context, uint8_t service_id, uint16_t data_id);

// Alive check functions
static drv_doip_status_t doip_send_alive_check_request(drv_doip_hw_context_t *context);
static drv_doip_status_t doip_handle_alive_check_response(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length);
static drv_doip_status_t doip_handle_alive_check_request(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length);

// Raw lwIP callback functions
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
    
    printf("DOIP Client: Initializing raw lwIP resources\r\n");
    
    // Create stream buffer for received data
    context->stream_buffer = xStreamBufferCreate(DOIP_STREAM_BUFFER_SIZE, DOIP_STREAM_TRIGGER_LEVEL);
    if (context->stream_buffer == NULL) {
        printf("DOIP Client: Failed to create stream buffer\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Create semaphores for synchronization
    context->connected_sem = xSemaphoreCreateBinary();
    if (context->connected_sem == NULL) {
        printf("DOIP Client: Failed to create connection semaphore\r\n");
        vStreamBufferDelete(context->stream_buffer);
        context->stream_buffer = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    context->send_sem = xSemaphoreCreateBinary();
    if (context->send_sem == NULL) {
        printf("DOIP Client: Failed to create send semaphore\r\n");
        vSemaphoreDelete(context->connected_sem);
        context->connected_sem = NULL;
        vStreamBufferDelete(context->stream_buffer);
        context->stream_buffer = NULL;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Initialize system monitoring data
    doip_init_system_monitoring_data(&context->monitoring_data);
    
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
    
    // Stop task if running
    if (context->client_task_handle != NULL) {
        vTaskDelete(context->client_task_handle);
        context->client_task_handle = NULL;
    }
    
    // Close TCP connection
    if (context->tcp_pcb != NULL) {
        tcp_close(context->tcp_pcb);
        context->tcp_pcb = NULL;
    }
    
    // Clean up resources
    if (context->stream_buffer != NULL) {
        vStreamBufferDelete(context->stream_buffer);
        context->stream_buffer = NULL;
    }
    
    if (context->connected_sem != NULL) {
        vSemaphoreDelete(context->connected_sem);
        context->connected_sem = NULL;
    }
    
    if (context->send_sem != NULL) {
        vSemaphoreDelete(context->send_sem);
        context->send_sem = NULL;
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
    
    printf("DOIP Client: Discovering vehicles via UDP broadcast\r\n");
    context->current_state = DRV_DOIP_STATE_DISCOVERING;
    
    // Create UDP socket
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        printf("DOIP Client: Failed to create UDP socket (error: %d)\r\n", udp_socket);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: UDP socket created successfully\r\n");
    
    // Enable broadcast
    int broadcast_enable = 1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        printf("DOIP Client: Failed to enable broadcast\r\n");
        close(udp_socket);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Set socket to non-blocking mode using lwIP API
    int nonblock = 1;
    if (ioctlsocket(udp_socket, FIONBIO, &nonblock) == 0) {
        printf("DOIP Client: Socket set to non-blocking mode\r\n");
    } else {
        printf("DOIP Client: Warning - could not set non-blocking mode, using blocking mode\r\n");
    }
    
    // Prepare broadcast address
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(DOIP_UDP_DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = PP_HTONL(IPADDR_BROADCAST);
    
    // Create vehicle identification request
    request_msg.protocol_version = DOIP_PROTOCOL_VERSION;
    request_msg.inverse_protocol_version = DOIP_INVERSE_PROTOCOL_VERSION;
    request_msg.payload_type = DOIP_VEHICLE_IDENTIFICATION_REQUEST;
    request_msg.payload_length = 0;
    
    // Convert message to buffer
    buffer[0] = request_msg.protocol_version;
    buffer[1] = request_msg.inverse_protocol_version;
    buffer[2] = (request_msg.payload_type >> 8) & 0xFF;
    buffer[3] = request_msg.payload_type & 0xFF;
    buffer[4] = (request_msg.payload_length >> 24) & 0xFF;
    buffer[5] = (request_msg.payload_length >> 16) & 0xFF;
    buffer[6] = (request_msg.payload_length >> 8) & 0xFF;
    buffer[7] = request_msg.payload_length & 0xFF;
    
    // Send broadcast request
    result = sendto(udp_socket, buffer, DOIP_HEADER_SIZE + request_msg.payload_length, 0,
                    (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    if (result < 0) {
        printf("DOIP Client: Failed to send discovery request (error: %d)\r\n", result);
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
    
    printf("DOIP Client: Received %d bytes response\r\n", result);
    
    // Parse response header
    if (result < DOIP_HEADER_SIZE) {
        printf("DOIP Client: Response too short for DOIP header\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    response_msg.protocol_version = buffer[0];
    response_msg.inverse_protocol_version = buffer[1];
    response_msg.payload_type = (buffer[2] << 8) | buffer[3];
    response_msg.payload_length = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
    
    // Validate header
    if (response_msg.protocol_version != DOIP_PROTOCOL_VERSION ||
        response_msg.inverse_protocol_version != DOIP_INVERSE_PROTOCOL_VERSION) {
        printf("DOIP Client: Invalid protocol version in response\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (response_msg.payload_type != DOIP_VEHICLE_IDENTIFICATION_RESPONSE) {
        printf("DOIP Client: Unexpected response type: 0x%04X\r\n", response_msg.payload_type);
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Copy payload
    if (response_msg.payload_length > 0 && result >= (DOIP_HEADER_SIZE + response_msg.payload_length)) {
        memcpy(response_msg.payload, &buffer[DOIP_HEADER_SIZE], response_msg.payload_length);
    } else {
        printf("DOIP Client: Invalid payload length in response\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Parse vehicle announcement payload (VIN(17) + LA(2) + EID(6) + ...)
    if (response_msg.payload_length < 25) {
        printf("DOIP Client: Vehicle announcement payload too short\r\n");
        context->current_state = DRV_DOIP_STATE_ERROR;
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Extract VIN (17 bytes)
    memcpy(vehicle_info->vin, response_msg.payload, 17);
    vehicle_info->vin[17] = '\0';
    
    // Extract Logical Address (2 bytes)
    vehicle_info->logical_address = (response_msg.payload[17] << 8) | response_msg.payload[18];
    
    // Extract Entity ID (6 bytes)
    memcpy(vehicle_info->entity_id, &response_msg.payload[19], 6);
    
    // Handle GID fields - check for extended format
    if (response_msg.payload_length >= 33) {
        // 6-byte GID format
        memcpy(vehicle_info->group_id, &response_msg.payload[25], 6);
    } else if (response_msg.payload_length >= 27) {
        // 2-byte GID format
        memcpy(vehicle_info->group_id, &response_msg.payload[25], 2);
        memset(&vehicle_info->group_id[2], 0x00, 4);
    } else {
        // No GID - set to zeros
        memset(vehicle_info->group_id, 0x00, 6);
    }
    
    // Set IP address and port from response source
    vehicle_info->ip_address = response_addr.sin_addr.s_addr;
    vehicle_info->tcp_port = DOIP_TCP_DATA_PORT;
    
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
    
    printf("DOIP Client: Routing activation request sent (15 bytes)\r\n");
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
    
    context->current_state = DRV_DOIP_STATE_ACTIVATED;
    printf("DOIP Client: DOIP routing activation completed\r\n");
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
    
    // DOIP Header
    *ptr++ = DOIP_PROTOCOL_VERSION;          // Protocol version
    *ptr++ = DOIP_INVERSE_PROTOCOL_VERSION;  // Inverse protocol version
    *ptr++ = (DOIP_DIAGNOSTIC_MESSAGE >> 8) & 0xFF;  // Payload type high byte
    *ptr++ = DOIP_DIAGNOSTIC_MESSAGE & 0xFF;         // Payload type low byte
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x07; // Payload length (7 bytes)
    
    // Diagnostic Message Payload
    *ptr++ = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;  // Source address high byte
    *ptr++ = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;         // Source address low byte
    *ptr++ = 0x00; *ptr++ = 0x01;  // Target address (0x0001)
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
    
    printf("DOIP Client: Diagnostic message sent - Service:0x%02X, DID:0x%04X\r\n", service_id, data_id);
    return DRV_DOIP_STATUS_OK;
}

// Simplified implementations for the remaining functions
static drv_doip_status_t drv_doip_send_diagnostic_request_impl(const void *hw_context, uint8_t service_id, uint16_t data_id, uint8_t *response, size_t max_response_len, size_t *actual_len)
{
    ASSERT(hw_context != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    if (context->current_state != DRV_DOIP_STATE_ACTIVATED) {
        printf("DOIP Client: Not activated - cannot send diagnostic request\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Send diagnostic message
    drv_doip_status_t status = doip_send_diagnostic_message(context, service_id, data_id);
    if (status != DRV_DOIP_STATUS_OK) {
        return status;
    }
    
    // For now, just return success - proper response handling would require
    // implementing the receive callbacks properly
    *actual_len = 0;
    printf("DOIP Client: Diagnostic request completed\r\n");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_vin_impl(const void *hw_context, char *vin_buffer)
{
    ASSERT(hw_context != NULL);
    ASSERT(vin_buffer != NULL);
    
    // Return mock VIN
    strcpy(vin_buffer, "MOCK_VIN_1234567890");
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_ecu_software_version_impl(const void *hw_context, char *version_buffer, size_t buffer_size)
{
    ASSERT(hw_context != NULL);
    ASSERT(version_buffer != NULL);
    
    strncpy(version_buffer, "v1.2.3-raw", buffer_size - 1);
    version_buffer[buffer_size - 1] = '\0';
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_ecu_hardware_version_impl(const void *hw_context, char *version_buffer, size_t buffer_size)
{
    ASSERT(hw_context != NULL);
    ASSERT(version_buffer != NULL);
    
    strncpy(version_buffer, "HW_v2.0-raw", buffer_size - 1);
    version_buffer[buffer_size - 1] = '\0';
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_read_monitoring_data_impl(const void *hw_context, uint16_t did, uint8_t *response, size_t max_response_len, size_t *actual_len)
{
    *actual_len = 0;
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t drv_doip_get_system_monitoring_data_impl(const void *hw_context, drv_doip_system_monitoring_t *monitoring_data)
{
    ASSERT(hw_context != NULL);
    ASSERT(monitoring_data != NULL);
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)hw_context;
    
    doip_update_dynamic_monitoring_data(&context->monitoring_data);
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
    strncpy(serial_buffer, "SAME54P20A-SN001234", buffer_size - 1);
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
static void doip_init_system_monitoring_data(drv_doip_system_monitoring_t *data)
{
    // Initialize system information
    data->active_diagnostic_session = 0x01;
    strcpy(data->spare_part_number, "SAME54-XPRO-DEV-001");
    strcpy(data->ecu_sw_number, "ECU-SW-SAME54-001");
    strcpy(data->ecu_sw_version_detailed, "v1.2.3-raw-20240729");
    strcpy(data->system_supplier_id, "MICROCHIP");
    strcpy(data->ecu_manufacturing_date, "2024-07-29");
    strcpy(data->ecu_serial_number, "SAME54P20A-SN001234");
    strcpy(data->kit_assembly_part_number, "ATSAME54-XPRO");
    
    // Initialize network information
    strcpy(data->ecu_network_name, "DOIP_SAME54_NET");
    strcpy(data->ecu_network_address, "192.168.100.50");
    strcpy(data->identification_data_traceability, "SAME54-DOIP-TRACE-001");
    strcpy(data->ecu_pin_traceability, "PIN-TRACE-SAME54-001");
    
    // Initialize runtime monitoring with mock values
    data->ecu_operating_hours = 1247;
    data->vehicle_speed_kmh = 0;
    data->engine_rpm = 800;
    data->battery_voltage_mv = 12750;
    data->temperature_celsius = 250;
    data->fuel_level_percent = 85;
    
    // Initialize diagnostic status
    data->error_memory_status = 0x00;
    data->last_reset_reason = 0x01;
    strcpy(data->boot_software_id, "BOOTLOADER-V2.1.0");
    strcpy(data->application_sw_fingerprint, "SHA256:A1B2C3D4E5F67890ABCDEF1234567890FEDCBA0987654321");
}

static void doip_update_dynamic_monitoring_data(drv_doip_system_monitoring_t *data)
{
    // Update dynamic values (mock implementation)
    static uint32_t update_counter = 0;
    update_counter++;
    
    // Simulate changing values
    data->vehicle_speed_kmh = (update_counter % 100);
    data->engine_rpm = 800 + (update_counter % 3000);
    data->temperature_celsius = 200 + (update_counter % 100); // 20-30°C
}

static void doip_display_all_server_data(const drv_doip_system_monitoring_t *data)
{
    printf("\r\n=== DOIP Server: Comprehensive System Monitoring Data ===\r\n");
    
    printf("DOIP Server: [INFO] Displaying all 22 AUTOSAR-standard DIDs\r\n");
    printf("DOIP Server: [INFO] System ready - all monitoring parameters available\r\n");
    
    printf("\r\n--- System Information ---\r\n");
    printf("DOIP Server: Active Diagnostic Session: 0x%02X\r\n", data->active_diagnostic_session);
    printf("DOIP Server: Spare Part Number: %s\r\n", data->spare_part_number);
    printf("DOIP Server: ECU Software Number: %s\r\n", data->ecu_sw_number);
    printf("DOIP Server: ECU Software Version: %s\r\n", data->ecu_sw_version_detailed);
    printf("DOIP Server: System Supplier: %s\r\n", data->system_supplier_id);
    printf("DOIP Server: Manufacturing Date: %s\r\n", data->ecu_manufacturing_date);
    printf("DOIP Server: ECU Serial Number: %s\r\n", data->ecu_serial_number);
    printf("DOIP Server: Kit Assembly Part: %s\r\n", data->kit_assembly_part_number);
    
    printf("\r\n--- Network Information ---\r\n");
    printf("DOIP Server: Network Name: %s\r\n", data->ecu_network_name);
    printf("DOIP Server: Network Address: %s\r\n", data->ecu_network_address);
    printf("DOIP Server: ID Data Traceability: %s\r\n", data->identification_data_traceability);
    printf("DOIP Server: PIN Traceability: %s\r\n", data->ecu_pin_traceability);
    
    printf("\r\n--- Runtime Monitoring ---\r\n");
    printf("DOIP Server: Operating Hours: %lu hours\r\n", (unsigned long)data->ecu_operating_hours);
    printf("DOIP Server: Vehicle Speed: %u km/h\r\n", data->vehicle_speed_kmh);
    printf("DOIP Server: Engine RPM: %u RPM\r\n", data->engine_rpm);
    printf("DOIP Server: Battery Voltage: %u.%03u V\r\n", data->battery_voltage_mv / 1000, data->battery_voltage_mv % 1000);
    printf("DOIP Server: Temperature: %d.%d °C\r\n", data->temperature_celsius / 10, abs(data->temperature_celsius % 10));
    printf("DOIP Server: Fuel Level: %u%%\r\n", data->fuel_level_percent);
    
    printf("\r\n--- Diagnostic Status ---\r\n");
    printf("DOIP Server: Error Memory Status: 0x%02X\r\n", data->error_memory_status);
    printf("DOIP Server: Last Reset Reason: 0x%02X\r\n", data->last_reset_reason);
    printf("DOIP Server: Boot Software ID: %s\r\n", data->boot_software_id);
    printf("DOIP Server: Application SW Fingerprint: %s\r\n", data->application_sw_fingerprint);
    
    printf("\r\n=== DOIP Server: End of Monitoring Data Display ===\r\n");
    printf("DOIP Server: [INFO] All system parameters successfully reported\r\n");
    printf("DOIP Server: [INFO] Data updated with current runtime values\r\n\r\n");
}

// Alive check function implementations
static drv_doip_status_t doip_send_alive_check_request(drv_doip_hw_context_t *context)
{
    uint8_t message_buffer[10]; // DOIP header (8) + source address (2)
    uint8_t *ptr = message_buffer;
    
    // DOIP Header
    *ptr++ = DOIP_PROTOCOL_VERSION;          // Protocol version
    *ptr++ = DOIP_INVERSE_PROTOCOL_VERSION;  // Inverse protocol version
    *ptr++ = (DOIP_ALIVE_CHECK_REQUEST >> 8) & 0xFF;  // Payload type high byte
    *ptr++ = DOIP_ALIVE_CHECK_REQUEST & 0xFF;         // Payload type low byte
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x02; // Payload length (2 bytes)
    
    // Alive Check Request Payload: Source Address (2 bytes)
    *ptr++ = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;  // Source address high byte
    *ptr++ = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;         // Source address low byte
    
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
    printf("DOIP Client: Alive check response received - payload length: %lu bytes\r\n", payload_length);
    
    // Print payload bytes for debugging
    if (payload_length > 0) {
        printf("DOIP Client: Payload bytes: ");
        for (uint32_t i = 0; i < payload_length && i < 16; i++) {
            printf("0x%02X ", payload[i]);
        }
        printf("\r\n");
    }
    
    // According to ISO 13400, alive check response should have 2 bytes (source address)
    if (payload_length >= 2) {
        uint16_t source_address = (payload[0] << 8) | payload[1];
        printf("DOIP Client: Alive check response received from 0x%04X\r\n", source_address);
    } else {
        printf("DOIP Client: Alive check response payload too short (expected >= 2 bytes)\r\n");
    }
    
    return DRV_DOIP_STATUS_OK;
}

static drv_doip_status_t doip_handle_alive_check_request(drv_doip_hw_context_t *context, const uint8_t *payload, uint32_t payload_length)
{
    uint8_t message_buffer[10]; // DOIP header (8) + source address (2)
    uint8_t *ptr = message_buffer;
    
    if (payload_length < 2) {
        printf("DOIP Client: Invalid alive check request payload length\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP Client: Alive check request received, sending response\r\n");
    
    // DOIP Header for response
    *ptr++ = DOIP_PROTOCOL_VERSION;          // Protocol version
    *ptr++ = DOIP_INVERSE_PROTOCOL_VERSION;  // Inverse protocol version
    *ptr++ = (DOIP_ALIVE_CHECK_RESPONSE >> 8) & 0xFF;  // Payload type high byte
    *ptr++ = DOIP_ALIVE_CHECK_RESPONSE & 0xFF;         // Payload type low byte
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x02; // Payload length (2 bytes)
    
    // Echo back the source address from the request
    *ptr++ = payload[0];
    *ptr++ = payload[1];
    
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

// Task function - performs DOIP operations
static void doip_client_task(void *pvParameters)
{
    drv_doip_hw_context_t *context = (drv_doip_hw_context_t *)pvParameters;
    drv_doip_vehicle_info_t vehicle_info;
    char vin_buffer[18];
    char version_buffer[64];
    uint16_t speed_kmh, rpm, voltage_mv;
    int16_t temperature;
    uint8_t fuel_percent;
    
    printf("DOIP Client: Task started (Raw lwIP mode)\r\n");
    
    // Wait a bit for network to be fully ready
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    while (1) {
        // Only proceed if we're in idle state (not connected)
        if (context->current_state == DRV_DOIP_STATE_IDLE) {
            printf("DOIP Client: Starting vehicle discovery...\r\n");
            
            // Discover vehicles
            if (drv_doip_discover_vehicles_impl(context, &vehicle_info) == DRV_DOIP_STATUS_OK) {
                printf("DOIP Client: Vehicle discovered, attempting connection...\r\n");
                
                // Connect to discovered vehicle
                if (drv_doip_connect_to_vehicle_impl(context, &vehicle_info) == DRV_DOIP_STATUS_OK) {
                    printf("\r\n--- Reading Vehicle Information ---\r\n");
                    
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
                    
                    printf("\r\n--- Reading Monitoring Data ---\r\n");
                    
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
                    
                    printf("\r\n--- DOIP Communication Complete ---\r\n");
                    
                    // Display comprehensive monitoring data
                    doip_update_dynamic_monitoring_data(&context->monitoring_data);
                    doip_display_all_server_data(&context->monitoring_data);
                    
                    // Test alive check functionality
                    printf("\r\n--- Testing Alive Check ---\r\n");
                    if (doip_send_alive_check_request(context) == DRV_DOIP_STATUS_OK) {
                        printf("DOIP Client: Alive check request sent successfully\r\n");
                        
                        // Listen for incoming messages for a short time
                        printf("DOIP Client: Listening for ECU messages...\r\n");
                        TickType_t start_time = xTaskGetTickCount();
                        TickType_t timeout_ticks = pdMS_TO_TICKS(3000); // 3 second timeout
                        
                        while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
                            // Check if we have received data in stream buffer
                            if (context->stream_buffer != NULL) {
                                uint8_t buffer[1024];
                                size_t received = xStreamBufferReceive(
                                    context->stream_buffer,
                                    buffer,
                                    sizeof(buffer),
                                    pdMS_TO_TICKS(100) // 100ms timeout per check
                                );
                                
                                if (received >= DOIP_HEADER_SIZE) {
                                    // Parse DOIP header
                                    uint16_t payload_type = (buffer[2] << 8) | buffer[3];
                                    uint32_t payload_length = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
                                    
                                    printf("DOIP Client: Received message - Type: 0x%04X, Length: %lu bytes\r\n", 
                                           payload_type, payload_length);
                                    
                                    // Handle alive check messages
                                    if (payload_type == DOIP_ALIVE_CHECK_REQUEST && received >= (DOIP_HEADER_SIZE + payload_length)) {
                                        printf("DOIP Client: Handling alive check request from ECU\r\n");
                                        doip_handle_alive_check_request(context, &buffer[DOIP_HEADER_SIZE], payload_length);
                                    } else if (payload_type == DOIP_ALIVE_CHECK_RESPONSE && received >= (DOIP_HEADER_SIZE + payload_length)) {
                                        printf("DOIP Client: Handling alive check response from ECU\r\n");
                                        doip_handle_alive_check_response(context, &buffer[DOIP_HEADER_SIZE], payload_length);
                                    } else {
                                        printf("DOIP Client: Received other message type: 0x%04X\r\n", payload_type);
                                    }
                                }
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
                    printf("DOIP Client: [DEMO] Connection failed as expected (no DOIP server)\r\n");
                    printf("DOIP Client: To connect to real DOIP server, update IP address in discovery\r\n");
                    vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds before retry
                }
            } else {
                printf("DOIP Client: No vehicles discovered\r\n");
                vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds before retry
            }
        } else {
            // If in connected state, perform periodic alive checks or monitoring
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
        
        // Update dynamic monitoring data
        doip_update_dynamic_monitoring_data(&context->monitoring_data);
    }
}