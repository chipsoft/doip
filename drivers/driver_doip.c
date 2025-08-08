/**
 * @file driver_doip.c
 * @brief Universal DoIP Driver Implementation
 * 
 * @details This file implements the universal DoIP driver API functions
 * providing hardware-agnostic DoIP protocol handling with comprehensive
 * error checking, memory safety, and ISO 13400 compliance.
 * 
 * @section implementation Implementation Notes
 * - All API functions include parameter validation using ASSERT macros
 * - Memory safety is enforced with bounds checking in utility functions
 * - Error conditions are logged with detailed context for debugging
 * - State management prevents invalid operations
 * 
 * @author  SAME54 DoIP Project
 * @date    2024
 * @version 1.0
 */

#include "driver_doip.h"
#include "utils_assert.h"
#include "printf.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

//-----------------------------------------------------------------------------
// Universal API Function Implementations
//-----------------------------------------------------------------------------

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
                                                 const uint8_t *request_payload, size_t request_payload_len,
                                                 uint8_t *response_buffer, size_t max_response_len, size_t *actual_len)
{
    ASSERT(handle != NULL);
    ASSERT(handle->send_diagnostic_request != NULL);
    ASSERT(response_buffer != NULL);
    ASSERT(actual_len != NULL);
    ASSERT(max_response_len > 0);
    // request_payload can be NULL for simple DID requests
    ASSERT(request_payload != NULL || request_payload_len == 0);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Calculate total request size for logging
    size_t total_request_size = 3 + request_payload_len; // service_id(1) + data_id(2) + payload
    printf("DOIP: Unified diagnostic request - service=0x%02X, data_id=0x%04X, payload_len=%zu, total_size=%zu\r\n",
           service_id, data_id, request_payload_len, total_request_size);
    
    return handle->send_diagnostic_request(handle->hw_context, service_id, data_id,
                                          request_payload, request_payload_len,
                                          response_buffer, max_response_len, actual_len);
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

uint32_t hw_doip_get_last_source_ip(drv_doip_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_last_source_ip != NULL);
    
    if (!handle->is_init) {
        return 0;
    }
    
    return handle->get_last_source_ip(handle->hw_context);
}

// Large message functions removed - functionality moved to unified functions above

//-----------------------------------------------------------------------------
// Raw DOIP Messaging API Implementations
//-----------------------------------------------------------------------------

drv_doip_status_t hw_doip_send_raw_message(drv_doip_t *handle, uint16_t payload_type,
                                          const uint8_t *payload_data, uint32_t payload_length, 
                                          bool use_static_buffer)
{
    ASSERT(handle != NULL);
    ASSERT(handle->send_raw_message != NULL);
    ASSERT(payload_data != NULL || payload_length == 0);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Validate payload size against safe limits
    if (payload_length > DOIP_MAX_SAFE_PAYLOAD_SIZE) {
        printf("DOIP: Unified raw message payload too large: %u bytes (max safe %d)\r\n", 
               (unsigned int)payload_length, DOIP_MAX_SAFE_PAYLOAD_SIZE);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    printf("DOIP: Unified raw message - type=0x%04X, length=%u, static_buffer=%s\r\n", 
           payload_type, (unsigned int)payload_length, use_static_buffer ? "true" : "false");
    
    return handle->send_raw_message(handle->hw_context, payload_type, payload_data, payload_length, use_static_buffer);
}

drv_doip_status_t hw_doip_start_packet_listener(drv_doip_t *handle, const drv_doip_packet_listener_config_t *config)
{
    ASSERT(handle != NULL);
    ASSERT(config != NULL);
    
    if (!handle->is_init) {
        printf("DOIP: Driver not initialized for packet listener start\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (handle->start_packet_listener == NULL) {
        printf("DOIP: start_packet_listener function not implemented\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    // Validate configuration
    if (config->timeout_ms == 0 || config->timeout_ms > 60000) {
        printf("DOIP: Invalid timeout_ms: %u (must be 1-60000)\r\n", (unsigned int)config->timeout_ms);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (config->max_fragments == 0 || config->max_fragments > 100) {
        printf("DOIP: Invalid max_fragments: %u (must be 1-100)\r\n", (unsigned int)config->max_fragments);
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->start_packet_listener(handle->hw_context, config);
}

drv_doip_status_t hw_doip_stop_packet_listener(drv_doip_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->stop_packet_listener != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_OK;
    }
    
    return handle->stop_packet_listener(handle->hw_context);
}

drv_doip_status_t hw_doip_register_packet_callback(drv_doip_t *handle, drv_doip_packet_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(callback != NULL);
    
    if (!handle->is_init) {
        printf("DOIP: Driver not initialized for packet callback registration\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (handle->register_packet_callback == NULL) {
        printf("DOIP: register_packet_callback function not implemented\r\n");
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->register_packet_callback(handle->hw_context, callback);
}

//-----------------------------------------------------------------------------
// Common Utility Function Implementations
//-----------------------------------------------------------------------------

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
    
    // MEMORY SAFETY: Check if payload exceeds small message limit
    if (msg->payload_length > DOIP_SMALL_PAYLOAD_SIZE) {
        printf("DOIP: Small message payload too large: %u bytes (max %d) - use large message API\r\n", 
               (unsigned int)msg->payload_length, DOIP_SMALL_PAYLOAD_SIZE);
        printf("DOIP: HINT - Use doip_utils_parse_large_message() for messages > %d bytes\r\n", 
               DOIP_SMALL_PAYLOAD_SIZE);
        return false;
    }
    
    if (data_len < DOIP_HEADER_SIZE + msg->payload_length) {
        printf("DOIP: Incomplete message: %zu bytes (expected %u)\r\n", 
               data_len, (unsigned int)(DOIP_HEADER_SIZE + msg->payload_length));
        return false;
    }
    
    // MEMORY SAFETY: Copy payload with validated bounds (no truncation)
    if (msg->payload_length > 0) {
        // We already validated that payload_length <= DOIP_SMALL_PAYLOAD_SIZE
        // and that we have enough source data, so this copy is safe
        memcpy(msg->payload, &data[DOIP_HEADER_SIZE], msg->payload_length);
        printf("DOIP: Small message parsed successfully - type=0x%04X, length=%u\r\n",
               msg->payload_type, (unsigned int)msg->payload_length);
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
    
    // MEMORY SAFETY: Validate payload length before serialization
    if (msg->payload_length > DOIP_MAX_PAYLOAD_SIZE) {
        printf("DOIP: ERROR - Cannot serialize message with payload length %u > max %d\r\n",
               (unsigned int)msg->payload_length, DOIP_MAX_PAYLOAD_SIZE);
        return;
    }
    
    buffer[0] = msg->protocol_version;
    buffer[1] = msg->inverse_protocol_version;
    buffer[2] = (msg->payload_type >> 8) & 0xFF;
    buffer[3] = msg->payload_type & 0xFF;
    buffer[4] = (msg->payload_length >> 24) & 0xFF;
    buffer[5] = (msg->payload_length >> 16) & 0xFF;
    buffer[6] = (msg->payload_length >> 8) & 0xFF;
    buffer[7] = msg->payload_length & 0xFF;
    
    // MEMORY SAFETY: Only copy payload if length is valid and within bounds
    if (msg->payload_length > 0 && msg->payload_length <= DOIP_MAX_PAYLOAD_SIZE) {
        memcpy(&buffer[DOIP_HEADER_SIZE], msg->payload, msg->payload_length);
    }
}

bool doip_utils_serialize_message_safe(const doip_message_t *msg, uint8_t *buffer, size_t buffer_size, size_t *bytes_written)
{
    ASSERT(msg != NULL);
    ASSERT(buffer != NULL);
    ASSERT(bytes_written != NULL);
    
    *bytes_written = 0;
    
    // MEMORY SAFETY: Validate payload length
    if (msg->payload_length > DOIP_MAX_PAYLOAD_SIZE) {
        printf("DOIP: ERROR - Message payload length %u exceeds maximum %d\r\n",
               (unsigned int)msg->payload_length, DOIP_MAX_PAYLOAD_SIZE);
        return false;
    }
    
    // Calculate total required buffer size
    size_t required_size = DOIP_HEADER_SIZE + msg->payload_length;
    
    // MEMORY SAFETY: Validate destination buffer size
    if (buffer_size < required_size) {
        printf("DOIP: ERROR - Buffer too small: %zu bytes (need %zu)\r\n", 
               buffer_size, required_size);
        return false;
    }
    
    // Serialize header
    buffer[0] = msg->protocol_version;
    buffer[1] = msg->inverse_protocol_version;
    buffer[2] = (msg->payload_type >> 8) & 0xFF;
    buffer[3] = msg->payload_type & 0xFF;
    buffer[4] = (msg->payload_length >> 24) & 0xFF;
    buffer[5] = (msg->payload_length >> 16) & 0xFF;
    buffer[6] = (msg->payload_length >> 8) & 0xFF;
    buffer[7] = msg->payload_length & 0xFF;
    
    // MEMORY SAFETY: Copy payload with validated bounds
    if (msg->payload_length > 0) {
        memcpy(&buffer[DOIP_HEADER_SIZE], msg->payload, msg->payload_length);
    }
    
    *bytes_written = required_size;
    return true;
}





//-----------------------------------------------------------------------------
// Large Message Utility Implementations
//-----------------------------------------------------------------------------

doip_large_message_t *doip_utils_alloc_large_message(uint32_t payload_size)
{
    // Allocate structure
    doip_large_message_t *msg = (doip_large_message_t *)pvPortMalloc(sizeof(doip_large_message_t));
    if (msg == NULL) {
        printf("DOIP: Failed to allocate large message structure (%zu bytes)\r\n", 
               sizeof(doip_large_message_t));
        return NULL;
    }
    
    // Initialize structure
    memset(msg, 0, sizeof(doip_large_message_t));
    msg->payload_capacity = payload_size;
    msg->payload_allocated = false;
    
    // Allocate payload buffer if needed
    if (payload_size > 0) {
        msg->payload = (uint8_t *)pvPortMalloc(payload_size);
        if (msg->payload == NULL) {
            printf("DOIP: Failed to allocate payload buffer (%u bytes)\r\n", 
                   (unsigned int)payload_size);
            vPortFree(msg);
            return NULL;
        }
        msg->payload_allocated = true;
        memset(msg->payload, 0, payload_size);
    } else {
        msg->payload = NULL;
    }
    
    printf("DOIP: Allocated large message - struct=%zu bytes, payload=%u bytes\r\n",
           sizeof(doip_large_message_t), (unsigned int)payload_size);
    
    return msg;
}

// Static large message structure for memory-constrained systems
static doip_large_message_t static_large_message = {0};
static bool static_large_message_in_use = false;

/**
 * @brief Initialize static large message with external buffer
 * @param payload_data Pointer to payload data buffer
 * @param payload_size Size of payload data
 * @return Pointer to static large message structure, NULL on failure
 * @note This avoids heap allocation by using static structure
 */
doip_large_message_t *doip_utils_init_static_large_message(uint8_t *payload_data, uint32_t payload_size)
{
    if (static_large_message_in_use) {
        printf("DOIP: Static large message structure already in use\r\n");
        return NULL;
    }
    
    if (payload_data == NULL && payload_size > 0) {
        printf("DOIP: Invalid parameters for static large message\r\n");
        return NULL;
    }
    
    // Mark as in use
    static_large_message_in_use = true;
    
    // Initialize static structure
    memset(&static_large_message, 0, sizeof(doip_large_message_t));
    static_large_message.payload = payload_data;
    static_large_message.payload_length = payload_size;
    static_large_message.payload_capacity = payload_size;
    static_large_message.payload_allocated = false;  // We're using external buffer
    
    printf("DOIP: Initialized static large message - struct=%zu bytes, payload=%u bytes (external buffer)\r\n",
           sizeof(doip_large_message_t), (unsigned int)payload_size);
    
    return &static_large_message;
}

/**
 * @brief Release static large message structure
 * @param msg Pointer to large message structure (should be static one)
 */
void doip_utils_release_static_large_message(doip_large_message_t *msg)
{
    if (msg == &static_large_message) {
        static_large_message_in_use = false;
        memset(&static_large_message, 0, sizeof(doip_large_message_t));
        printf("DOIP: Released static large message structure\r\n");
    }
}

void doip_utils_free_large_message(doip_large_message_t *msg)
{
    if (msg == NULL) {
        return;
    }
    
    // Free payload buffer if allocated
    if (msg->payload_allocated && msg->payload != NULL) {
        printf("DOIP: Freeing payload buffer (%u bytes)\r\n", (unsigned int)msg->payload_capacity);
        vPortFree(msg->payload);
        msg->payload = NULL;
    }
    
    // Free structure
    printf("DOIP: Freeing large message structure\r\n");
    vPortFree(msg);
}

bool doip_utils_parse_large_message(const uint8_t *data, size_t data_len, doip_large_message_t *msg)
{
    ASSERT(data != NULL);
    ASSERT(msg != NULL);
    
    if (data_len < DOIP_HEADER_SIZE) {
        printf("DOIP: Large message header too short: %zu bytes (expected %d)\r\n", 
               data_len, DOIP_HEADER_SIZE);
        return false;
    }
    
    // Parse header
    msg->protocol_version = data[0];
    msg->inverse_protocol_version = data[1];
    msg->payload_type = (data[2] << 8) | data[3];
    msg->payload_length = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    
    if (!doip_utils_validate_protocol(msg->protocol_version, msg->inverse_protocol_version)) {
        return false;
    }
    
    // Validate against safe limits
    if (msg->payload_length > DOIP_MAX_SAFE_PAYLOAD_SIZE) {
        printf("DOIP: Large message payload too large: %u bytes (max safe %d)\r\n", 
               (unsigned int)msg->payload_length, DOIP_MAX_SAFE_PAYLOAD_SIZE);
        return false;
    }
    
    // Check if we have enough input data
    if (data_len < DOIP_HEADER_SIZE + msg->payload_length) {
        printf("DOIP: Incomplete large message: %zu bytes (expected %u)\r\n", 
               data_len, (unsigned int)(DOIP_HEADER_SIZE + msg->payload_length));
        return false;
    }
    
    // Allocate or reallocate payload buffer if needed
    if (msg->payload_length > msg->payload_capacity || msg->payload == NULL) {
        if (msg->payload_allocated && msg->payload != NULL) {
            vPortFree(msg->payload);
        }
        
        msg->payload = (uint8_t *)pvPortMalloc(msg->payload_length);
        if (msg->payload == NULL) {
            printf("DOIP: Failed to allocate payload buffer for large message (%u bytes)\r\n",
                   (unsigned int)msg->payload_length);
            return false;
        }
        msg->payload_capacity = msg->payload_length;
        msg->payload_allocated = true;
    }
    
    // Copy payload data
    if (msg->payload_length > 0) {
        memcpy(msg->payload, &data[DOIP_HEADER_SIZE], msg->payload_length);
        printf("DOIP: Large message parsed successfully - type=0x%04X, length=%u\r\n",
               msg->payload_type, (unsigned int)msg->payload_length);
    }
    
    return true;
}

bool doip_utils_serialize_large_message(const doip_large_message_t *msg, uint8_t *buffer, size_t buffer_size, size_t *bytes_written)
{
    ASSERT(msg != NULL);
    ASSERT(buffer != NULL);
    ASSERT(bytes_written != NULL);
    
    *bytes_written = 0;
    
    // Calculate total required buffer size
    size_t required_size = DOIP_HEADER_SIZE + msg->payload_length;
    
    // Validate destination buffer size
    if (buffer_size < required_size) {
        printf("DOIP: Buffer too small for large message: %zu bytes (need %zu)\r\n", 
               buffer_size, required_size);
        return false;
    }
    
    // Serialize header
    buffer[0] = msg->protocol_version;
    buffer[1] = msg->inverse_protocol_version;
    buffer[2] = (msg->payload_type >> 8) & 0xFF;
    buffer[3] = msg->payload_type & 0xFF;
    buffer[4] = (msg->payload_length >> 24) & 0xFF;
    buffer[5] = (msg->payload_length >> 16) & 0xFF;
    buffer[6] = (msg->payload_length >> 8) & 0xFF;
    buffer[7] = msg->payload_length & 0xFF;
    
    // Copy payload with validated bounds
    if (msg->payload_length > 0 && msg->payload != NULL) {
        memcpy(&buffer[DOIP_HEADER_SIZE], msg->payload, msg->payload_length);
    }
    
    *bytes_written = required_size;
    printf("DOIP: Large message serialized - %zu bytes written\r\n", required_size);
    return true;
}

//-----------------------------------------------------------------------------
// Alive Check and Multi-ECU Utility Implementations
//-----------------------------------------------------------------------------

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

// Multi-ECU discovery utility implementations

uint8_t doip_utils_parse_multi_ecu_discovery_response(const uint8_t *buffer, size_t buffer_len, 
                                                     uint32_t source_ip, doip_multi_ecu_cache_t *cache)
{
    ASSERT(buffer != NULL);
    ASSERT(cache != NULL);
    
    printf("DOIP Utils: Parsing multi-ECU discovery response (%zu bytes) - current cache has %d ECUs\r\n", 
           buffer_len, cache->count);
    
    // Don't reset cache->count - we want to accumulate ECUs from multiple responses
    size_t buffer_offset = 0;
    doip_message_t response_msg;
    uint8_t initial_count = cache->count;
    
    while (buffer_offset < buffer_len && cache->count < DOIP_MAX_DISCOVERED_ECUS) {
        size_t remaining = buffer_len - buffer_offset;
        
        if (remaining < DOIP_HEADER_SIZE) {
            printf("DOIP Utils: Insufficient remaining data for DOIP header\r\n");
            break;
        }
        
        // Parse response header using utility
        if (!doip_utils_parse_header(buffer + buffer_offset, remaining, &response_msg)) {
            printf("DOIP Utils: Invalid discovery response header at offset %zu\r\n", buffer_offset);
            // Try to find next DOIP header by looking for protocol version pattern
            buffer_offset++;
            continue;
        }
        
        printf("DOIP Utils: Found DOIP message - Type: 0x%04X, Length: %lu\r\n", 
               response_msg.payload_type, response_msg.payload_length);
        
        // Check if this is a vehicle identification response
        if (response_msg.payload_type == DOIP_VEHICLE_IDENTIFICATION_RESPONSE) {
            drv_doip_vehicle_info_t temp_vehicle;
            if (doip_utils_extract_vehicle_info(&response_msg, source_ip, &temp_vehicle)) {
                // Check for duplicates based on logical address
                bool duplicate = false;
                for (uint8_t i = 0; i < cache->count; i++) {
                    if (cache->vehicles[i].logical_address == temp_vehicle.logical_address) {
                        printf("DOIP Utils: Duplicate ECU 0x%04X ignored\r\n", temp_vehicle.logical_address);
                        duplicate = true;
                        break;
                    }
                }
                
                if (!duplicate) {
                    memcpy(&cache->vehicles[cache->count], &temp_vehicle, sizeof(drv_doip_vehicle_info_t));
                    printf("DOIP Utils: Cached vehicle %d - VIN=%s, LA=0x%04X\r\n",
                           cache->count + 1, cache->vehicles[cache->count].vin, cache->vehicles[cache->count].logical_address);
                    cache->count++;
                }
            }
        }
        
        // Move to next message
        buffer_offset += DOIP_HEADER_SIZE + response_msg.payload_length;
    }
    
    uint8_t newly_added = cache->count - initial_count;
    printf("DOIP Utils: Parsed and cached %d new vehicles from discovery response (total: %d)\r\n", 
           newly_added, cache->count);
    // Only reset current_index when starting fresh discovery, not on each response
    return newly_added;
}

bool doip_utils_extract_vehicle_info(const doip_message_t *response_msg, uint32_t source_ip, 
                                     drv_doip_vehicle_info_t *vehicle_info)
{
    ASSERT(response_msg != NULL);
    ASSERT(vehicle_info != NULL);
    
    // Check minimum payload size: VIN(17) + Logical Address(2) + EID(6)
    if (response_msg->payload_length < 25) {
        printf("DOIP Utils: Vehicle announcement payload too short (%lu bytes)\r\n", response_msg->payload_length);
        return false;
    }
    
    // Parse VIN (17 bytes)
    memcpy(vehicle_info->vin, response_msg->payload, 17);
    vehicle_info->vin[17] = '\0';
    
    // Parse Logical Address (2 bytes)
    vehicle_info->logical_address = (response_msg->payload[17] << 8) | response_msg->payload[18];
    
    // Parse Entity ID (6 bytes)  
    memcpy(vehicle_info->entity_id, &response_msg->payload[19], 6);
    
    // Parse Group ID (first 2 bytes if available)
    if (response_msg->payload_length >= 31) { // VIN(17) + LA(2) + EID(6) + GID(6)
        memcpy(vehicle_info->group_id, &response_msg->payload[25], 2);
    } else {
        memset(vehicle_info->group_id, 0x00, 2);
    }
    
    // Set IP and port from discovery
    vehicle_info->ip_address = source_ip;
    vehicle_info->tcp_port = DOIP_TCP_DATA_PORT;
    
    return true;
}

bool doip_utils_handle_negative_ack(uint16_t payload_type, const uint8_t *payload, 
                                    uint32_t payload_length, size_t *actual_len)
{
    ASSERT(actual_len != NULL);
    
    if (payload_type == DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK) {
        printf("DOIP Utils: Received negative ACK response (ECU does not support this request)\r\n");
        if (payload_length >= 1 && payload != NULL) {
            printf("DOIP Utils: NACK code: 0x%02X (Request out of range)\r\n", payload[0]);
        }
        *actual_len = 0;
        // Return true to indicate this was handled as a negative ACK (not an error)
        return true;
    }
    
    return false; // Not a negative ACK
}