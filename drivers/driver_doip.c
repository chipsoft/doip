#include "driver_doip.h"
#include "utils_assert.h"

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