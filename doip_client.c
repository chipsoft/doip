/**
 * \file doip_client.c
 * \brief DOIP (Diagnostics over Internet Protocol) Client Implementation
 * 
 * Implements ISO 13400 DOIP protocol client for automotive diagnostics.
 */

#include "doip_client.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include "FreeRTOS.h"
#include "task.h"
#include "printf.h"
#include "eth_ipstack_main.h"
#include <string.h>
#include <stdlib.h>

/* Task configuration */
#define DOIP_CLIENT_TASK_PRIORITY    (tskIDLE_PRIORITY + 3)
#define DOIP_CLIENT_TASK_STACK_SIZE  (2048)

/* Global variables */
static TaskHandle_t doip_client_task_handle = NULL;
static doip_status_t doip_status = DOIP_STATUS_IDLE;
static doip_vehicle_info_t current_vehicle;
static int tcp_socket = -1;
static bool doip_client_initialized = false;

/* Function implementations */

bool doip_client_init(void)
{
    if (doip_client_initialized) {
        return true;
    }

    /* Initialize global variables */
    doip_status = DOIP_STATUS_IDLE;
    tcp_socket = -1;
    memset(&current_vehicle, 0, sizeof(current_vehicle));

    doip_client_initialized = true;
    printf("DOIP Client: Initialized\r\n");
    return true;
}

bool doip_client_start_task(void)
{
    if (!doip_client_initialized) {
        printf("DOIP Client: Not initialized\r\n");
        return false;
    }

    if (doip_client_task_handle != NULL) {
        printf("DOIP Client: Task already running\r\n");
        return true;
    }

    BaseType_t result = xTaskCreate(
        doip_client_task,
        "DOIP_Client",
        DOIP_CLIENT_TASK_STACK_SIZE,
        NULL,
        DOIP_CLIENT_TASK_PRIORITY,
        &doip_client_task_handle
    );

    if (result != pdPASS) {
        printf("DOIP Client: Failed to create task\r\n");
        return false;
    }

    printf("DOIP Client: Task started\r\n");
    return true;
}

void doip_create_header(doip_message_t *msg, uint16_t payload_type, uint32_t payload_length)
{
    msg->protocol_version = DOIP_PROTOCOL_VERSION;
    msg->inverse_protocol_version = DOIP_INVERSE_PROTOCOL_VERSION;
    msg->payload_type = payload_type;
    msg->payload_length = payload_length;
}

bool doip_parse_header(const uint8_t *data, size_t data_len, doip_message_t *msg)
{
    if (data_len < DOIP_HEADER_SIZE) {
        return false;
    }

    msg->protocol_version = data[0];
    msg->inverse_protocol_version = data[1];
    msg->payload_type = (data[2] << 8) | data[3];
    msg->payload_length = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];

    if (msg->protocol_version != DOIP_PROTOCOL_VERSION ||
        msg->inverse_protocol_version != DOIP_INVERSE_PROTOCOL_VERSION) {
        return false;
    }

    if (msg->payload_length > DOIP_MAX_PAYLOAD_SIZE) {
        return false;
    }

    /* Copy payload if available */
    if (data_len >= DOIP_HEADER_SIZE + msg->payload_length) {
        memcpy(msg->payload, &data[DOIP_HEADER_SIZE], msg->payload_length);
    } else if (data_len > DOIP_HEADER_SIZE && msg->payload_length > 0) {
        size_t copy_len = (data_len - DOIP_HEADER_SIZE);
        if (copy_len > msg->payload_length) {
            copy_len = msg->payload_length;
        }
        memcpy(msg->payload, &data[DOIP_HEADER_SIZE], copy_len);
    } else {
        msg->payload_length = 0;
    }

    return true;
}

bool doip_send_tcp_message(int socket, const doip_message_t *msg)
{
    uint8_t buffer[DOIP_HEADER_SIZE + DOIP_MAX_PAYLOAD_SIZE];
    
    /* Serialize message */
    buffer[0] = msg->protocol_version;
    buffer[1] = msg->inverse_protocol_version;
    buffer[2] = (msg->payload_type >> 8) & 0xFF;
    buffer[3] = msg->payload_type & 0xFF;
    buffer[4] = (msg->payload_length >> 24) & 0xFF;
    buffer[5] = (msg->payload_length >> 16) & 0xFF;
    buffer[6] = (msg->payload_length >> 8) & 0xFF;
    buffer[7] = msg->payload_length & 0xFF;
    memcpy(&buffer[8], msg->payload, msg->payload_length);
    
    /* Send message */
    int result = send(socket, buffer, DOIP_HEADER_SIZE + msg->payload_length, 0);
    return (result >= 0);
}

bool doip_receive_tcp_message(int socket, doip_message_t *msg, uint32_t timeout_ms)
{
    uint8_t buffer[DOIP_HEADER_SIZE + DOIP_MAX_PAYLOAD_SIZE];
    struct timeval timeout;
    
    /* Set socket timeout */
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    /* Receive header first */
    int result = recv(socket, buffer, DOIP_HEADER_SIZE, MSG_PEEK);
    if (result < DOIP_HEADER_SIZE) {
        return false;
    }
    
    /* Parse header to get payload length */
    if (!doip_parse_header(buffer, DOIP_HEADER_SIZE, msg)) {
        return false;
    }
    
    /* Receive full message */
    result = recv(socket, buffer, DOIP_HEADER_SIZE + msg->payload_length, 0);
    if (result < DOIP_HEADER_SIZE + msg->payload_length) {
        return false;
    }
    
    /* Parse complete message */
    return doip_parse_header(buffer, result, msg);
}

bool doip_discover_vehicles(doip_vehicle_info_t *vehicle_info)
{
    int udp_socket = -1;
    struct sockaddr_in broadcast_addr;
    struct sockaddr_in response_addr;
    socklen_t addr_len = sizeof(response_addr);
    doip_message_t request_msg, response_msg;
    uint8_t buffer[1024];
    int result;
    struct timeval timeout;

    printf("DOIP Client: Starting vehicle discovery\r\n");
    doip_status = DOIP_STATUS_DISCOVERING;

    /* Create UDP socket */
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        printf("DOIP Client: Failed to create UDP socket (error: %d)\r\n", udp_socket);
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }
    
    printf("DOIP Client: UDP socket created successfully\r\n");

    /* Enable broadcast */
    int broadcast_enable = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    /* Set receive timeout */
    timeout.tv_sec = DOIP_DISCOVERY_TIMEOUT_MS / 1000;
    timeout.tv_usec = (DOIP_DISCOVERY_TIMEOUT_MS % 1000) * 1000;
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    /* Prepare broadcast address */
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(DOIP_UDP_DISCOVERY_PORT);
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;

    /* Create vehicle identification request */
    doip_create_header(&request_msg, DOIP_VEHICLE_IDENTIFICATION_REQUEST, 0);

    /* Serialize header */
    buffer[0] = request_msg.protocol_version;
    buffer[1] = request_msg.inverse_protocol_version;
    buffer[2] = (request_msg.payload_type >> 8) & 0xFF;
    buffer[3] = request_msg.payload_type & 0xFF;
    buffer[4] = (request_msg.payload_length >> 24) & 0xFF;
    buffer[5] = (request_msg.payload_length >> 16) & 0xFF;
    buffer[6] = (request_msg.payload_length >> 8) & 0xFF;
    buffer[7] = request_msg.payload_length & 0xFF;

    /* Send broadcast request */
    result = sendto(udp_socket, buffer, DOIP_HEADER_SIZE, 0,
                   (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    if (result < 0) {
        printf("DOIP Client: Failed to send discovery request (error: %d)\r\n", result);
        close(udp_socket);
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    printf("DOIP Client: Discovery request sent\r\n");

    /* Wait for response */
    result = recvfrom(udp_socket, buffer, sizeof(buffer), 0,
                     (struct sockaddr*)&response_addr, &addr_len);
    
    close(udp_socket);

    if (result < 0) {
        printf("DOIP Client: No discovery response received\r\n");
        doip_status = DOIP_STATUS_IDLE;
        return false;
    }

    /* Parse response */
    if (!doip_parse_header(buffer, result, &response_msg)) {
        printf("DOIP Client: Invalid discovery response header\r\n");
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    if (response_msg.payload_type != DOIP_VEHICLE_IDENTIFICATION_RESPONSE) {
        printf("DOIP Client: Unexpected response type: 0x%04X\r\n", response_msg.payload_type);
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    /* Parse vehicle announcement payload */
    if (response_msg.payload_length < 28) {  /* VIN(17) + LA(2) + EID(6) + GID(2) + FAR(1) */
        printf("DOIP Client: Invalid vehicle announcement payload length\r\n");
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    /* Extract vehicle information */
    memcpy(vehicle_info->vin, response_msg.payload, 17);
    vehicle_info->vin[17] = '\0';
    
    vehicle_info->logical_address = (response_msg.payload[17] << 8) | response_msg.payload[18];
    memcpy(vehicle_info->entity_id, &response_msg.payload[19], 6);
    memcpy(vehicle_info->group_id, &response_msg.payload[25], 2);
    
    vehicle_info->ip_address = response_addr.sin_addr.s_addr;
    vehicle_info->tcp_port = DOIP_TCP_DATA_PORT;

    /* Store current vehicle info */
    memcpy(&current_vehicle, vehicle_info, sizeof(current_vehicle));
    doip_status = DOIP_STATUS_DISCOVERED;

    printf("DOIP Client: Vehicle discovered\r\n");
    printf("  VIN: %s\r\n", vehicle_info->vin);
    printf("  Logical Address: 0x%04X\r\n", vehicle_info->logical_address);
    
    struct in_addr addr_copy = response_addr.sin_addr;
    printf("  IP Address: %s\r\n", inet_ntoa(addr_copy));

    return true;
}

bool doip_connect_to_vehicle(const doip_vehicle_info_t *vehicle_info)
{
    struct sockaddr_in server_addr;
    doip_message_t request_msg, response_msg;
    uint8_t buffer[1024];
    int result;
    struct timeval timeout;

    printf("DOIP Client: Connecting to vehicle\r\n");
    doip_status = DOIP_STATUS_CONNECTING;

    /* Create TCP socket */
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0) {
        printf("DOIP Client: Failed to create TCP socket\r\n");
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    /* Set socket timeout */
    timeout.tv_sec = DOIP_TCP_TIMEOUT_MS / 1000;
    timeout.tv_usec = (DOIP_TCP_TIMEOUT_MS % 1000) * 1000;
    setsockopt(tcp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(tcp_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    /* Connect to vehicle */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(vehicle_info->tcp_port);
    server_addr.sin_addr.s_addr = vehicle_info->ip_address;

    result = connect(tcp_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0) {
        printf("DOIP Client: TCP connection failed\r\n");
        close(tcp_socket);
        tcp_socket = -1;
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    doip_status = DOIP_STATUS_CONNECTED;
    printf("DOIP Client: TCP connection established\r\n");

    /* Send routing activation request */
    doip_create_header(&request_msg, DOIP_ROUTING_ACTIVATION_REQUEST, 7);
    
    /* Routing activation payload: Source Address (2) + Activation Type (1) + Reserved (4) */
    request_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    request_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    request_msg.payload[2] = 0x00;  /* Default activation type */
    memset(&request_msg.payload[3], 0x00, 4);  /* Reserved */

    /* Serialize and send message */
    buffer[0] = request_msg.protocol_version;
    buffer[1] = request_msg.inverse_protocol_version;
    buffer[2] = (request_msg.payload_type >> 8) & 0xFF;
    buffer[3] = request_msg.payload_type & 0xFF;
    buffer[4] = (request_msg.payload_length >> 24) & 0xFF;
    buffer[5] = (request_msg.payload_length >> 16) & 0xFF;
    buffer[6] = (request_msg.payload_length >> 8) & 0xFF;
    buffer[7] = request_msg.payload_length & 0xFF;
    memcpy(&buffer[8], request_msg.payload, request_msg.payload_length);

    result = send(tcp_socket, buffer, DOIP_HEADER_SIZE + request_msg.payload_length, 0);
    if (result < 0) {
        printf("DOIP Client: Failed to send routing activation request\r\n");
        close(tcp_socket);
        tcp_socket = -1;
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    /* Receive routing activation response */
    result = recv(tcp_socket, buffer, sizeof(buffer), 0);
    if (result < 0) {
        printf("DOIP Client: Failed to receive routing activation response\r\n");
        close(tcp_socket);
        tcp_socket = -1;
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    if (!doip_parse_header(buffer, result, &response_msg)) {
        printf("DOIP Client: Invalid routing activation response\r\n");
        close(tcp_socket);
        tcp_socket = -1;
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    if (response_msg.payload_type != DOIP_ROUTING_ACTIVATION_RESPONSE) {
        printf("DOIP Client: Unexpected routing response type: 0x%04X\r\n", response_msg.payload_type);
        close(tcp_socket);
        tcp_socket = -1;
        doip_status = DOIP_STATUS_ERROR;
        return false;
    }

    /* Check response code */
    if (response_msg.payload_length >= 5) {
        uint8_t response_code = response_msg.payload[4];
        if (response_code == 0x10) {
            doip_status = DOIP_STATUS_ACTIVATED;
            printf("DOIP Client: Routing activation successful\r\n");
            return true;
        } else {
            printf("DOIP Client: Routing activation failed with code: 0x%02X\r\n", response_code);
        }
    }

    close(tcp_socket);
    tcp_socket = -1;
    doip_status = DOIP_STATUS_ERROR;
    return false;
}

int doip_send_diagnostic_request(uint8_t service_id, uint16_t data_id, 
                                uint8_t *response, size_t max_response_len)
{
    doip_message_t request_msg, response_msg;
    uint8_t buffer[1024];
    int result;

    if (tcp_socket < 0 || doip_status != DOIP_STATUS_ACTIVATED) {
        printf("DOIP Client: Not connected or activated\r\n");
        return -1;
    }

    /* Create diagnostic message */
    doip_create_header(&request_msg, DOIP_DIAGNOSTIC_MESSAGE, 7);
    
    /* Diagnostic payload: Source Address (2) + Target Address (2) + UDS Data (3) */
    request_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    request_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    request_msg.payload[2] = (current_vehicle.logical_address >> 8) & 0xFF;
    request_msg.payload[3] = current_vehicle.logical_address & 0xFF;
    request_msg.payload[4] = service_id;
    request_msg.payload[5] = (data_id >> 8) & 0xFF;
    request_msg.payload[6] = data_id & 0xFF;

    /* Serialize and send message */
    buffer[0] = request_msg.protocol_version;
    buffer[1] = request_msg.inverse_protocol_version;
    buffer[2] = (request_msg.payload_type >> 8) & 0xFF;
    buffer[3] = request_msg.payload_type & 0xFF;
    buffer[4] = (request_msg.payload_length >> 24) & 0xFF;
    buffer[5] = (request_msg.payload_length >> 16) & 0xFF;
    buffer[6] = (request_msg.payload_length >> 8) & 0xFF;
    buffer[7] = request_msg.payload_length & 0xFF;
    memcpy(&buffer[8], request_msg.payload, request_msg.payload_length);

    result = send(tcp_socket, buffer, DOIP_HEADER_SIZE + request_msg.payload_length, 0);
    if (result < 0) {
        printf("DOIP Client: Failed to send diagnostic request\r\n");
        return -1;
    }

    printf("DOIP Client: Sent diagnostic request - Service: 0x%02X, DID: 0x%04X\r\n", service_id, data_id);

    /* Receive response */
    result = recv(tcp_socket, buffer, sizeof(buffer), 0);
    if (result < 0) {
        printf("DOIP Client: Failed to receive diagnostic response\r\n");
        return -1;
    }

    if (!doip_parse_header(buffer, result, &response_msg)) {
        printf("DOIP Client: Invalid diagnostic response header\r\n");
        return -1;
    }

    if (response_msg.payload_type != DOIP_DIAGNOSTIC_MESSAGE) {
        printf("DOIP Client: Unexpected diagnostic response type: 0x%04X\r\n", response_msg.payload_type);
        return -1;
    }

    /* Extract UDS response (skip DOIP addressing) */
    if (response_msg.payload_length > 4) {
        size_t uds_data_len = response_msg.payload_length - 4;
        size_t copy_len = (uds_data_len < max_response_len) ? uds_data_len : max_response_len;
        
        memcpy(response, &response_msg.payload[4], copy_len);
        
        printf("DOIP Client: Received diagnostic response (%zu bytes)\r\n", uds_data_len);
        return (int)copy_len;
    }

    return 0;
}

bool doip_read_vin(char *vin_buffer)
{
    uint8_t response[32];
    int response_len;

    response_len = doip_send_diagnostic_request(UDS_READ_DATA_BY_IDENTIFIER, DID_VIN, response, sizeof(response));
    
    if (response_len > 3 && response[0] == (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK)) {
        /* Extract VIN from positive response */
        size_t vin_len = response_len - 3;  /* Skip service ID and DID */
        if (vin_len > 17) vin_len = 17;
        
        memcpy(vin_buffer, &response[3], vin_len);
        vin_buffer[vin_len] = '\0';
        
        printf("DOIP Client: VIN: %s\r\n", vin_buffer);
        return true;
    }

    printf("DOIP Client: Failed to read VIN\r\n");
    return false;
}

bool doip_read_ecu_software_version(char *version_buffer, size_t buffer_size)
{
    uint8_t response[64];
    int response_len;

    response_len = doip_send_diagnostic_request(UDS_READ_DATA_BY_IDENTIFIER, DID_ECU_SOFTWARE_VERSION, response, sizeof(response));
    
    if (response_len > 3 && response[0] == (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK)) {
        /* Extract version from positive response */
        size_t version_len = response_len - 3;  /* Skip service ID and DID */
        if (version_len >= buffer_size) version_len = buffer_size - 1;
        
        memcpy(version_buffer, &response[3], version_len);
        version_buffer[version_len] = '\0';
        
        printf("DOIP Client: ECU SW Version: %s\r\n", version_buffer);
        return true;
    }

    printf("DOIP Client: Failed to read ECU software version\r\n");
    return false;
}

bool doip_read_ecu_hardware_version(char *version_buffer, size_t buffer_size)
{
    uint8_t response[64];
    int response_len;

    response_len = doip_send_diagnostic_request(UDS_READ_DATA_BY_IDENTIFIER, DID_ECU_HARDWARE_VERSION, response, sizeof(response));
    
    if (response_len > 3 && response[0] == (UDS_READ_DATA_BY_IDENTIFIER + UDS_POSITIVE_RESPONSE_MASK)) {
        /* Extract version from positive response */
        size_t version_len = response_len - 3;  /* Skip service ID and DID */
        if (version_len >= buffer_size) version_len = buffer_size - 1;
        
        memcpy(version_buffer, &response[3], version_len);
        version_buffer[version_len] = '\0';
        
        printf("DOIP Client: ECU HW Version: %s\r\n", version_buffer);
        return true;
    }

    printf("DOIP Client: Failed to read ECU hardware version\r\n");
    return false;
}

/* Alive Check Functions */

bool doip_send_alive_check_request(int socket)
{
    doip_message_t request_msg;
    uint8_t buffer[DOIP_HEADER_SIZE + 2];  /* Header + Source Address */
    
    /* Create alive check request */
    doip_create_header(&request_msg, DOIP_ALIVE_CHECK_REQUEST, 2);
    
    /* Add source address to payload */
    request_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    request_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    
    /* Serialize message */
    buffer[0] = request_msg.protocol_version;
    buffer[1] = request_msg.inverse_protocol_version;
    buffer[2] = (request_msg.payload_type >> 8) & 0xFF;
    buffer[3] = request_msg.payload_type & 0xFF;
    buffer[4] = (request_msg.payload_length >> 24) & 0xFF;
    buffer[5] = (request_msg.payload_length >> 16) & 0xFF;
    buffer[6] = (request_msg.payload_length >> 8) & 0xFF;
    buffer[7] = request_msg.payload_length & 0xFF;
    memcpy(&buffer[8], request_msg.payload, request_msg.payload_length);
    
    /* Send request */
    int result = send(socket, buffer, DOIP_HEADER_SIZE + request_msg.payload_length, 0);
    if (result < 0) {
        printf("DOIP Client: Failed to send alive check request\r\n");
        return false;
    }
    
    printf("DOIP Client: Alive check request sent\r\n");
    return true;
}

bool doip_handle_alive_check_response(const doip_message_t *msg)
{
    if (msg->payload_length < 2) {
        printf("DOIP Client: Invalid alive check response payload length\r\n");
        return false;
    }
    
    uint16_t source_address = (msg->payload[0] << 8) | msg->payload[1];
    printf("DOIP Client: Alive check response received from 0x%04X\r\n", source_address);
    return true;
}

bool doip_handle_alive_check_request(int socket, const doip_message_t *msg)
{
    doip_message_t response_msg;
    uint8_t buffer[DOIP_HEADER_SIZE + 2];  /* Header + Source Address */
    
    if (msg->payload_length < 2) {
        printf("DOIP Client: Invalid alive check request payload length\r\n");
        return false;
    }
    
    /* Create alive check response */
    doip_create_header(&response_msg, DOIP_ALIVE_CHECK_RESPONSE, 2);
    
    /* Echo back the source address */
    response_msg.payload[0] = msg->payload[0];
    response_msg.payload[1] = msg->payload[1];
    
    /* Serialize message */
    buffer[0] = response_msg.protocol_version;
    buffer[1] = response_msg.inverse_protocol_version;
    buffer[2] = (response_msg.payload_type >> 8) & 0xFF;
    buffer[3] = response_msg.payload_type & 0xFF;
    buffer[4] = (response_msg.payload_length >> 24) & 0xFF;
    buffer[5] = (response_msg.payload_length >> 16) & 0xFF;
    buffer[6] = (response_msg.payload_length >> 8) & 0xFF;
    buffer[7] = response_msg.payload_length & 0xFF;
    memcpy(&buffer[8], response_msg.payload, response_msg.payload_length);
    
    /* Send response */
    int result = send(socket, buffer, DOIP_HEADER_SIZE + response_msg.payload_length, 0);
    if (result < 0) {
        printf("DOIP Client: Failed to send alive check response\r\n");
        return false;
    }
    
    printf("DOIP Client: Alive check response sent\r\n");
    return true;
}

/* Enhanced Diagnostic Functions */

bool doip_send_diagnostic_ack(int socket, uint8_t ack_type)
{
    doip_message_t ack_msg;
    uint8_t buffer[DOIP_HEADER_SIZE + 5];  /* Header + SA + TA + ACK Type */
    
    /* Create diagnostic ACK */
    doip_create_header(&ack_msg, 
                      ack_type == 0x00 ? DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK : DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK, 
                      5);
    
    /* Payload: Source Address + Target Address + ACK Type */
    ack_msg.payload[0] = (DOIP_CLIENT_SOURCE_ADDRESS >> 8) & 0xFF;
    ack_msg.payload[1] = DOIP_CLIENT_SOURCE_ADDRESS & 0xFF;
    ack_msg.payload[2] = (current_vehicle.logical_address >> 8) & 0xFF;
    ack_msg.payload[3] = current_vehicle.logical_address & 0xFF;
    ack_msg.payload[4] = ack_type;
    
    /* Serialize message */
    buffer[0] = ack_msg.protocol_version;
    buffer[1] = ack_msg.inverse_protocol_version;
    buffer[2] = (ack_msg.payload_type >> 8) & 0xFF;
    buffer[3] = ack_msg.payload_type & 0xFF;
    buffer[4] = (ack_msg.payload_length >> 24) & 0xFF;
    buffer[5] = (ack_msg.payload_length >> 16) & 0xFF;
    buffer[6] = (ack_msg.payload_length >> 8) & 0xFF;
    buffer[7] = ack_msg.payload_length & 0xFF;
    memcpy(&buffer[8], ack_msg.payload, ack_msg.payload_length);
    
    /* Send ACK */
    int result = send(socket, buffer, DOIP_HEADER_SIZE + ack_msg.payload_length, 0);
    if (result < 0) {
        printf("DOIP Client: Failed to send diagnostic ACK\r\n");
        return false;
    }
    
    printf("DOIP Client: Diagnostic ACK sent (type 0x%02X)\r\n", ack_type);
    return true;
}

bool doip_handle_diagnostic_ack(const doip_message_t *msg)
{
    if (msg->payload_length < 5) {
        printf("DOIP Client: Invalid diagnostic ACK payload length\r\n");
        return false;
    }
    
    uint16_t source_address = (msg->payload[0] << 8) | msg->payload[1];
    uint16_t target_address = (msg->payload[2] << 8) | msg->payload[3];
    uint8_t ack_type = msg->payload[4];
    
    printf("DOIP Client: Diagnostic ACK received: SA=0x%04X, TA=0x%04X, Type=0x%02X\r\n", 
           source_address, target_address, ack_type);
    
    return (ack_type == 0x00);  /* Return true for positive ACK */
}

doip_status_t doip_get_status(void)
{
    return doip_status;
}

void doip_disconnect(void)
{
    if (tcp_socket >= 0) {
        close(tcp_socket);
        tcp_socket = -1;
    }
    doip_status = DOIP_STATUS_IDLE;
    printf("DOIP Client: Disconnected\r\n");
}

void doip_client_task(void *pvParameters)
{
    (void)pvParameters;
    
    doip_vehicle_info_t vehicle_info;
    char vin_buffer[32];
    char version_buffer[64];
    
    printf("DOIP Client: Task started\r\n");
    
    /* Wait for network to be ready */
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    while (1) {
        /* Check if network interface has valid IP address */
        if (TCPIP_STACK_INTERFACE_0_desc.ip_addr.addr == 0) {
            printf("DOIP Client: Waiting for network interface to get IP address...\r\n");
            vTaskDelay(pdMS_TO_TICKS(5000));  /* Wait 5 seconds before retrying */
            continue;
        }
        
        /* Check if link is stable before attempting network operations */
        bool link_up = netif_is_link_up(&TCPIP_STACK_INTERFACE_0_desc);
        bool netif_up = netif_is_up(&TCPIP_STACK_INTERFACE_0_desc);
        
        printf("DOIP Client: Link status check - Link UP: %s, Interface UP: %s\r\n", 
               link_up ? "YES" : "NO", netif_up ? "YES" : "NO");
        
        /* If interface is UP but link is not detected as UP, proceed anyway */
        /* This handles cases where PHY link detection is unreliable but connectivity works */
        if (!link_up && !netif_up) {
            printf("DOIP Client: Both link and interface are down, waiting...\r\n");
            vTaskDelay(pdMS_TO_TICKS(2000));  /* Wait 2 seconds before retrying */
            continue;
        } else if (!link_up && netif_up) {
            printf("DOIP Client: Interface UP but link detection unreliable, proceeding...\r\n");
        } else {
            printf("DOIP Client: Link and interface both UP, proceeding...\r\n");
        }
        
        /* Wait additional time for link to stabilize after coming up */
        printf("DOIP Client: Link detected, waiting for stabilization...\r\n");
        vTaskDelay(pdMS_TO_TICKS(3000));  /* Wait 3 seconds for link stability */
        
        /* Double-check interface is still functional after stabilization period */
        if (!netif_is_up(&TCPIP_STACK_INTERFACE_0_desc)) {
            printf("DOIP Client: Interface went down during stabilization, retrying...\r\n");
            continue;
        } else {
            printf("DOIP Client: Interface stable after stabilization period\r\n");
        }
        
        printf("\r\n=== DOIP Client Diagnostic Cycle ===\r\n");
        
        /* Display current network configuration */
        static char tmp_buff[16];
        printf("DOIP Client: Network ready\r\n");
        printf("  IP Address: %s\r\n", 
               ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.ip_addr), tmp_buff, 16));
        printf("  Starting vehicle discovery...\r\n");
        
        /* Discover vehicles */
        if (doip_discover_vehicles(&vehicle_info)) {
            /* Connect to discovered vehicle */
            if (doip_connect_to_vehicle(&vehicle_info)) {
                /* Perform diagnostic operations */
                printf("\r\n--- Reading Vehicle Information ---\r\n");
                
                /* Read VIN */
                if (doip_read_vin(vin_buffer)) {
                    printf("VIN: %s\r\n", vin_buffer);
                }
                
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                /* Read ECU software version */
                if (doip_read_ecu_software_version(version_buffer, sizeof(version_buffer))) {
                    printf("ECU Software Version: %s\r\n", version_buffer);
                }
                
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                /* Read ECU hardware version */
                if (doip_read_ecu_hardware_version(version_buffer, sizeof(version_buffer))) {
                    printf("ECU Hardware Version: %s\r\n", version_buffer);
                }
                
                printf("--- Diagnostic cycle completed ---\r\n");
                
                /* Enhanced: Send alive check request to ECU */
                printf("\r\n--- Testing Alive Check ---\r\n");
                if (doip_send_alive_check_request(tcp_socket)) {
                    printf("Alive check request sent successfully\r\n");
                }
                
                /* Enhanced: Listen for incoming messages (alive checks, ACKs) */
                printf("\r\n--- Listening for ECU Messages ---\r\n");
                doip_message_t incoming_msg;
                TickType_t start_time = xTaskGetTickCount();
                TickType_t timeout_ticks = pdMS_TO_TICKS(DOIP_ALIVE_CHECK_TIMEOUT_MS);
                
                while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
                    if (doip_receive_tcp_message(tcp_socket, &incoming_msg, 100)) {
                        switch (incoming_msg.payload_type) {
                            case DOIP_ALIVE_CHECK_REQUEST:
                                printf("Received alive check request from ECU\r\n");
                                doip_handle_alive_check_request(tcp_socket, &incoming_msg);
                                break;
                                
                            case DOIP_ALIVE_CHECK_RESPONSE:
                                printf("Received alive check response from ECU\r\n");
                                doip_handle_alive_check_response(&incoming_msg);
                                break;
                                
                            case DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK:
                            case DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK:
                                printf("Received diagnostic ACK from ECU\r\n");
                                doip_handle_diagnostic_ack(&incoming_msg);
                                break;
                                
                            default:
                                printf("Received unknown message type: 0x%04X\r\n", incoming_msg.payload_type);
                                break;
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));  /* Small delay to prevent busy waiting */
                }
                
                printf("--- Enhanced communication completed ---\r\n");
                
                /* Disconnect */
                doip_disconnect();
            }
        }
        
        /* Wait before next cycle */
        printf("DOIP Client: Waiting for next cycle...\r\n");
        vTaskDelay(pdMS_TO_TICKS(10000));  /* 10 second cycle */
    }
}