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
        handle->is_task_running = false;
        handle->current_state = DRV_DOIP_STATE_IDLE;
    }
    
    return result;
}

drv_doip_status_t hw_doip_start_task(drv_doip_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->start_task != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    if (handle->is_task_running) {
        return DRV_DOIP_STATUS_OK;
    }
    
    drv_doip_status_t result = handle->start_task(handle->hw_context);
    if (result == DRV_DOIP_STATUS_OK) {
        handle->is_task_running = true;
    }
    
    return result;
}

drv_doip_status_t hw_doip_stop_task(drv_doip_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->stop_task != NULL);
    
    if (!handle->is_task_running) {
        return DRV_DOIP_STATUS_OK;
    }
    
    drv_doip_status_t result = handle->stop_task(handle->hw_context);
    if (result == DRV_DOIP_STATUS_OK) {
        handle->is_task_running = false;
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

drv_doip_status_t hw_doip_read_vin(drv_doip_t *handle, char *vin_buffer)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_vin != NULL);
    ASSERT(vin_buffer != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_vin(handle->hw_context, vin_buffer);
}

drv_doip_status_t hw_doip_read_ecu_software_version(drv_doip_t *handle, char *version_buffer, size_t buffer_size)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_ecu_software_version != NULL);
    ASSERT(version_buffer != NULL);
    ASSERT(buffer_size > 0);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_ecu_software_version(handle->hw_context, version_buffer, buffer_size);
}

drv_doip_status_t hw_doip_read_ecu_hardware_version(drv_doip_t *handle, char *version_buffer, size_t buffer_size)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_ecu_hardware_version != NULL);
    ASSERT(version_buffer != NULL);
    ASSERT(buffer_size > 0);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_ecu_hardware_version(handle->hw_context, version_buffer, buffer_size);
}

drv_doip_status_t hw_doip_read_monitoring_data(drv_doip_t *handle, uint16_t did, uint8_t *response, 
                                              size_t max_response_len, size_t *actual_len)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_monitoring_data != NULL);
    ASSERT(response != NULL);
    ASSERT(actual_len != NULL);
    ASSERT(max_response_len > 0);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_monitoring_data(handle->hw_context, did, response, max_response_len, actual_len);
}

drv_doip_status_t hw_doip_get_system_monitoring_data(drv_doip_t *handle, drv_doip_system_monitoring_t *monitoring_data)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_system_monitoring_data != NULL);
    ASSERT(monitoring_data != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->get_system_monitoring_data(handle->hw_context, monitoring_data);
}

drv_doip_status_t hw_doip_read_active_diagnostic_session(drv_doip_t *handle, uint8_t *session_buffer, size_t buffer_size)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_active_diagnostic_session != NULL);
    ASSERT(session_buffer != NULL);
    ASSERT(buffer_size > 0);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_active_diagnostic_session(handle->hw_context, session_buffer, buffer_size);
}

drv_doip_status_t hw_doip_read_ecu_serial_number(drv_doip_t *handle, char *serial_buffer, size_t buffer_size)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_ecu_serial_number != NULL);
    ASSERT(serial_buffer != NULL);
    ASSERT(buffer_size > 0);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_ecu_serial_number(handle->hw_context, serial_buffer, buffer_size);
}

drv_doip_status_t hw_doip_read_vehicle_speed(drv_doip_t *handle, uint16_t *speed_kmh)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_vehicle_speed != NULL);
    ASSERT(speed_kmh != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_vehicle_speed(handle->hw_context, speed_kmh);
}

drv_doip_status_t hw_doip_read_engine_rpm(drv_doip_t *handle, uint16_t *rpm)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_engine_rpm != NULL);
    ASSERT(rpm != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_engine_rpm(handle->hw_context, rpm);
}

drv_doip_status_t hw_doip_read_battery_voltage(drv_doip_t *handle, uint16_t *voltage_mv)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_battery_voltage != NULL);
    ASSERT(voltage_mv != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_battery_voltage(handle->hw_context, voltage_mv);
}

drv_doip_status_t hw_doip_read_temperature_data(drv_doip_t *handle, int16_t *temperature_celsius)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_temperature_data != NULL);
    ASSERT(temperature_celsius != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_temperature_data(handle->hw_context, temperature_celsius);
}

drv_doip_status_t hw_doip_read_fuel_level(drv_doip_t *handle, uint8_t *fuel_percent)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_fuel_level != NULL);
    ASSERT(fuel_percent != NULL);
    
    if (!handle->is_init) {
        return DRV_DOIP_STATUS_ERROR;
    }
    
    return handle->read_fuel_level(handle->hw_context, fuel_percent);
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

void doip_utils_init_monitoring_data(drv_doip_system_monitoring_t *data)
{
    ASSERT(data != NULL);
    
    data->active_diagnostic_session = 0x01;
    strcpy(data->spare_part_number, "SAME54-XPRO-DEV-001");
    strcpy(data->ecu_sw_number, "ECU-SW-SAME54-001");
    strcpy(data->ecu_sw_version_detailed, "v1.2.3-common-20240729");
    strcpy(data->system_supplier_id, "MICROCHIP");
    strcpy(data->ecu_manufacturing_date, "2024-07-29");
    strcpy(data->ecu_serial_number, "SAME54P20A-COMMON-001");
    strcpy(data->kit_assembly_part_number, "ATSAME54-XPRO");
    
    strcpy(data->ecu_network_name, "DOIP_SAME54_NET");
    strcpy(data->ecu_network_address, "192.168.100.50");
    strcpy(data->identification_data_traceability, "SAME54-DOIP-TRACE-001");
    strcpy(data->ecu_pin_traceability, "PIN-TRACE-SAME54-001");
    
    data->ecu_operating_hours = 1247;
    data->vehicle_speed_kmh = 0;
    data->engine_rpm = 800;
    data->battery_voltage_mv = 12750;
    data->temperature_celsius = 250;
    data->fuel_level_percent = 85;
    
    data->error_memory_status = 0x00;
    data->last_reset_reason = 0x01;
    strcpy(data->boot_software_id, "BOOTLOADER-V2.1.0");
    strcpy(data->application_sw_fingerprint, "SHA256:A1B2C3D4E5F67890ABCDEF1234567890FEDCBA0987654321");
}

void doip_utils_update_dynamic_data(drv_doip_system_monitoring_t *data)
{
    ASSERT(data != NULL);
    
    static uint32_t update_counter = 0;
    update_counter++;
    
    data->vehicle_speed_kmh = (update_counter % 100);
    data->engine_rpm = 800 + (update_counter % 3000);
    data->temperature_celsius = 200 + (update_counter % 100);
}

void doip_utils_display_server_data(const drv_doip_system_monitoring_t *data)
{
    ASSERT(data != NULL);
    
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