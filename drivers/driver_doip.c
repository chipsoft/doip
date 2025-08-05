#include "driver_doip.h"
#include "utils_assert.h"
#include "printf.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

drv_doip_status_t hw_doip_init(drv_doip_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->init != NULL);
    
    if (handle->is_init) {
        return DRV_DOIP_STATUS_OK;
    }
    
    drv_doip_status_t result = handle->init(handle->hw_context);
    if (result == DRV_DOIP_STATUS_OK) {
        handle->is_init = true;
        handle->current_state = DRV_DOIP_STATE_IDLE;
    }
    
    return result;
}

drv_doip_status_t hw_doip_deinit(drv_doip_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_OK;
    }
    
    drv_doip_status_t result = handle->deinit(handle->hw_context);
    if (result == DRV_DOIP_STATUS_OK) {
        handle->is_init = false;
        handle->current_state = DRV_DOIP_STATE_IDLE;
    }
    
    return result;
}


drv_doip_status_t hw_doip_discover_vehicles(drv_doip_t *handle, drv_doip_vehicle_info_t *vehicle_info)
{
    ASSERT(handle != NULL);
    ASSERT(handle->discover_vehicles != NULL);
    ASSERT(vehicle_info != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->discover_vehicles(handle->hw_context, vehicle_info);
}

drv_doip_status_t hw_doip_connect_to_vehicle(drv_doip_t *handle, const drv_doip_vehicle_info_t *vehicle_info)
{
    ASSERT(handle != NULL);
    ASSERT(handle->connect_to_vehicle != NULL);
    ASSERT(vehicle_info != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->connect_to_vehicle(handle->hw_context, vehicle_info);
}

drv_doip_status_t hw_doip_disconnect(drv_doip_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->disconnect != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_OK;
    }
    
    return handle->disconnect(handle->hw_context);
}

drv_doip_status_t hw_doip_send_diagnostic_request(drv_doip_t *handle, uint8_t service_id, uint16_t data_id, 
                                                 uint8_t *response, size_t max_response_len, size_t *actual_len)
{
    ASSERT(handle != NULL);
    ASSERT(handle->send_diagnostic_request != NULL);
    ASSERT(response != NULL);
    ASSERT(actual_len != NULL);
    ASSERT(max_response_len > 0);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->send_diagnostic_request(handle->hw_context, service_id, data_id, 
                                          response, max_response_len, actual_len);
}



drv_doip_state_t hw_doip_get_status(drv_doip_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_status != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATE_ERROR;
    }
    
    return handle->get_status(handle->hw_context);
}

drv_doip_status_t hw_doip_register_callback(drv_doip_t *handle, drv_doip_cb_type_t type, 
                                           drv_doip_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(handle->register_callback != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->register_callback(handle->hw_context, type, callback);
}

// Common utility function implementations

void doip_utils_create_header(doip_message_t *msg, uint16_t payload_type, uint32_t payload_length)
{
    ASSERT(msg != NULL);
    
    msg->protocol_version = DOIP_PROTOCOL_VERSION;
    msg->inverse_protocol_version = DOIP_INVERSE_PROTOCOL_VERSION;
    msg->payload_type = payload_type;
    msg->payload_length = payload_length;
}

bool doip_utils_parse_header(const uint8_t *data, size_t data_len, doip_message_t *msg)
{
    ASSERT(data != NULL);
    ASSERT(msg != NULL);
    
    if (data_len < DOIP_HEADER_SIZE) {
        printf("DOIP: Header too short: %zu bytes (expected %d)\r\n", data_len, DOIP_HEADER_SIZE);
        return false;
    }
    
    msg->protocol_version = data[0];
    msg->inverse_protocol_version = data[1];
    msg->payload_type = (data[2] << 8) | data[3];
    msg->payload_length = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    
    if (!doip_utils_validate_protocol(msg->protocol_version, msg->inverse_protocol_version)) {
        return false;
    }
    
    if (msg->payload_length > DOIP_MAX_PAYLOAD_SIZE) {
        printf("DOIP: Payload too large: %lu bytes (max %d)\r\n", 
               msg->payload_length, DOIP_MAX_PAYLOAD_SIZE);
        return false;
    }
    
    if (data_len < DOIP_HEADER_SIZE + msg->payload_length) {
        printf("DOIP: Incomplete message: %zu bytes (expected %lu)\r\n", 
               data_len, DOIP_HEADER_SIZE + msg->payload_length);
        return false;
    }
    
    if (msg->payload_length > 0) {
        memcpy(msg->payload, &data[DOIP_HEADER_SIZE], msg->payload_length);
    }
    
    return true;
}

bool doip_utils_validate_protocol(uint8_t protocol_version, uint8_t inverse_protocol_version)
{
    if (protocol_version != DOIP_PROTOCOL_VERSION ||
        inverse_protocol_version != DOIP_INVERSE_PROTOCOL_VERSION) {
        printf("DOIP: Invalid protocol version: 0x%02X/0x%02X (expected 0x%02X/0x%02X)\r\n",
               protocol_version, inverse_protocol_version,
               DOIP_PROTOCOL_VERSION, DOIP_INVERSE_PROTOCOL_VERSION);
        return false;
    }
    return true;
}

void doip_utils_serialize_message(const doip_message_t *msg, uint8_t *buffer)
{
    ASSERT(msg != NULL);
    ASSERT(buffer != NULL);
    
    buffer[0] = msg->protocol_version;
    buffer[1] = msg->inverse_protocol_version;
    buffer[2] = (msg->payload_type >> 8) & 0xFF;
    buffer[3] = msg->payload_type & 0xFF;
    buffer[4] = (msg->payload_length >> 24) & 0xFF;
    buffer[5] = (msg->payload_length >> 16) & 0xFF;
    buffer[6] = (msg->payload_length >> 8) & 0xFF;
    buffer[7] = msg->payload_length & 0xFF;
    
    if (msg->payload_length > 0) {
        memcpy(&buffer[DOIP_HEADER_SIZE], msg->payload, msg->payload_length);
    }
}





void doip_utils_create_alive_check_request(uint8_t *buffer, uint16_t source_address)
{
    ASSERT(buffer != NULL);
    
    uint8_t *ptr = buffer;
    
    *ptr++ = DOIP_PROTOCOL_VERSION;
    *ptr++ = DOIP_INVERSE_PROTOCOL_VERSION;
    *ptr++ = (DOIP_ALIVE_CHECK_REQUEST >> 8) & 0xFF;
    *ptr++ = DOIP_ALIVE_CHECK_REQUEST & 0xFF;
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x02;
    
    *ptr++ = (source_address >> 8) & 0xFF;
    *ptr++ = source_address & 0xFF;
}

void doip_utils_create_alive_check_response(uint8_t *buffer, const uint8_t *request_payload)
{
    ASSERT(buffer != NULL);
    ASSERT(request_payload != NULL);
    
    uint8_t *ptr = buffer;
    
    *ptr++ = DOIP_PROTOCOL_VERSION;
    *ptr++ = DOIP_INVERSE_PROTOCOL_VERSION;
    *ptr++ = (DOIP_ALIVE_CHECK_RESPONSE >> 8) & 0xFF;
    *ptr++ = DOIP_ALIVE_CHECK_RESPONSE & 0xFF;
    *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x00; *ptr++ = 0x02;
    
    *ptr++ = request_payload[0];
    *ptr++ = request_payload[1];
}

bool doip_utils_handle_alive_check_payload(const uint8_t *payload, uint32_t payload_length, uint16_t *source_address)
{
    ASSERT(payload != NULL);
    ASSERT(source_address != NULL);
    
    printf("DOIP: Alive check payload received - length: %lu bytes\r\n", payload_length);
    
    if (payload_length > 0) {
        printf("DOIP: Payload bytes: ");
        for (uint32_t i = 0; i < payload_length && i < 16; i++) {
            printf("0x%02X ", payload[i]);
        }
        printf("\r\n");
    }
    
    if (payload_length >= 2) {
        *source_address = (payload[0] << 8) | payload[1];
        printf("DOIP: Source address: 0x%04X\r\n", *source_address);
        return true;
    } else {
        printf("DOIP: Payload too short (expected >= 2 bytes)\r\n");
        return false;
    }
}