#ifndef _DRIVER_DOIP_H_
#define _DRIVER_DOIP_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Status enumeration
typedef enum {
    DRV_DOIP_STATUS_OK = 0,
    DRV_DOIP_STATUS_ERROR = 1,
    DRV_DOIP_STATUS_BUSY = 2,
    DRV_DOIP_STATUS_TIMEOUT = 3,
    DRV_DOIP_STATUS_NO_VEHICLE = 4,
    DRV_DOIP_STATUS_DISCONNECTED = 5,
} drv_doip_status_t;

// DOIP client states
typedef enum {
    DRV_DOIP_STATE_IDLE = 0,
    DRV_DOIP_STATE_DISCOVERING = 1,
    DRV_DOIP_STATE_DISCOVERED = 2,
    DRV_DOIP_STATE_CONNECTING = 3,
    DRV_DOIP_STATE_CONNECTED = 4,
    DRV_DOIP_STATE_ACTIVATED = 5,
    DRV_DOIP_STATE_ERROR = 6,
} drv_doip_state_t;

// Callback types
typedef enum {
    DRV_DOIP_CB_VEHICLE_DISCOVERED = 0,
    DRV_DOIP_CB_CONNECTION_ESTABLISHED = 1,
    DRV_DOIP_CB_CONNECTION_LOST = 2,
    DRV_DOIP_CB_DIAGNOSTIC_RESPONSE = 3,
    DRV_DOIP_CB_ERROR = 4,
} drv_doip_cb_type_t;

typedef void (*drv_doip_callback_t)(drv_doip_cb_type_t type, const void *data, size_t data_len);

// Vehicle information structure
typedef struct {
    char     vin[18];           // Vehicle Identification Number (17 chars + null)
    uint16_t logical_address;   // ECU logical address
    uint8_t  entity_id[6];      // Entity identifier
    uint8_t  group_id[2];       // Group identifier
    uint32_t ip_address;        // ECU IP address
    uint16_t tcp_port;          // TCP data port
} drv_doip_vehicle_info_t;

// System monitoring data structure
typedef struct {
    // System Information
    uint8_t  active_diagnostic_session;
    char     spare_part_number[32];
    char     ecu_sw_number[32];
    char     ecu_sw_version_detailed[32];
    char     system_supplier_id[16];
    char     ecu_manufacturing_date[16];
    char     ecu_serial_number[32];
    char     kit_assembly_part_number[32];
    
    // Network Information
    char     ecu_network_name[32];
    char     ecu_network_address[16];
    char     identification_data_traceability[64];
    char     ecu_pin_traceability[32];
    
    // Runtime Monitoring
    uint32_t ecu_operating_hours;
    uint16_t vehicle_speed_kmh;         // km/h
    uint16_t engine_rpm;                // RPM
    uint16_t battery_voltage_mv;        // millivolts
    int16_t  temperature_celsius;       // Celsius * 10
    uint8_t  fuel_level_percent;        // Percentage
    
    // Diagnostic Status
    uint8_t  error_memory_status;
    uint8_t  last_reset_reason;
    char     boot_software_id[32];
    char     application_sw_fingerprint[64];
} drv_doip_system_monitoring_t;

// Driver structure with function pointers
typedef struct {
    bool is_init;
    bool is_task_running;
    drv_doip_state_t current_state;
    const void *hw_context;
    
    // Core operations
    drv_doip_status_t (*init)(const void *hw_context);
    drv_doip_status_t (*deinit)(const void *hw_context);
    drv_doip_status_t (*start_task)(const void *hw_context);
    drv_doip_status_t (*stop_task)(const void *hw_context);
    
    // Discovery and connection
    drv_doip_status_t (*discover_vehicles)(const void *hw_context, drv_doip_vehicle_info_t *vehicle_info);
    drv_doip_status_t (*connect_to_vehicle)(const void *hw_context, const drv_doip_vehicle_info_t *vehicle_info);
    drv_doip_status_t (*disconnect)(const void *hw_context);
    
    // Diagnostic communication
    drv_doip_status_t (*send_diagnostic_request)(const void *hw_context, uint8_t service_id, uint16_t data_id, 
                                                uint8_t *response, size_t max_response_len, size_t *actual_len);
    
    // Data reading functions
    drv_doip_status_t (*read_vin)(const void *hw_context, char *vin_buffer);
    drv_doip_status_t (*read_ecu_software_version)(const void *hw_context, char *version_buffer, size_t buffer_size);
    drv_doip_status_t (*read_ecu_hardware_version)(const void *hw_context, char *version_buffer, size_t buffer_size);
    drv_doip_status_t (*read_monitoring_data)(const void *hw_context, uint16_t did, uint8_t *response, 
                                             size_t max_response_len, size_t *actual_len);
    drv_doip_status_t (*get_system_monitoring_data)(const void *hw_context, drv_doip_system_monitoring_t *monitoring_data);
    
    // Specific monitoring data readers
    drv_doip_status_t (*read_active_diagnostic_session)(const void *hw_context, uint8_t *session_buffer, size_t buffer_size);
    drv_doip_status_t (*read_ecu_serial_number)(const void *hw_context, char *serial_buffer, size_t buffer_size);
    drv_doip_status_t (*read_vehicle_speed)(const void *hw_context, uint16_t *speed_kmh);
    drv_doip_status_t (*read_engine_rpm)(const void *hw_context, uint16_t *rpm);
    drv_doip_status_t (*read_battery_voltage)(const void *hw_context, uint16_t *voltage_mv);
    drv_doip_status_t (*read_temperature_data)(const void *hw_context, int16_t *temperature_celsius);
    drv_doip_status_t (*read_fuel_level)(const void *hw_context, uint8_t *fuel_percent);
    
    // Status and callback management
    drv_doip_state_t (*get_status)(const void *hw_context);
    drv_doip_status_t (*register_callback)(const void *hw_context, drv_doip_cb_type_t type, 
                                          drv_doip_callback_t callback);
} drv_doip_t;

#ifdef __cplusplus
extern "C" {
#endif

// Universal API functions
drv_doip_status_t hw_doip_init(drv_doip_t *handle);
drv_doip_status_t hw_doip_deinit(drv_doip_t *handle);
drv_doip_status_t hw_doip_start_task(drv_doip_t *handle);
drv_doip_status_t hw_doip_stop_task(drv_doip_t *handle);

drv_doip_status_t hw_doip_discover_vehicles(drv_doip_t *handle, drv_doip_vehicle_info_t *vehicle_info);
drv_doip_status_t hw_doip_connect_to_vehicle(drv_doip_t *handle, const drv_doip_vehicle_info_t *vehicle_info);
drv_doip_status_t hw_doip_disconnect(drv_doip_t *handle);

drv_doip_status_t hw_doip_send_diagnostic_request(drv_doip_t *handle, uint8_t service_id, uint16_t data_id, 
                                                 uint8_t *response, size_t max_response_len, size_t *actual_len);

drv_doip_status_t hw_doip_read_vin(drv_doip_t *handle, char *vin_buffer);
drv_doip_status_t hw_doip_read_ecu_software_version(drv_doip_t *handle, char *version_buffer, size_t buffer_size);
drv_doip_status_t hw_doip_read_ecu_hardware_version(drv_doip_t *handle, char *version_buffer, size_t buffer_size);
drv_doip_status_t hw_doip_read_monitoring_data(drv_doip_t *handle, uint16_t did, uint8_t *response, 
                                              size_t max_response_len, size_t *actual_len);
drv_doip_status_t hw_doip_get_system_monitoring_data(drv_doip_t *handle, drv_doip_system_monitoring_t *monitoring_data);

drv_doip_status_t hw_doip_read_active_diagnostic_session(drv_doip_t *handle, uint8_t *session_buffer, size_t buffer_size);
drv_doip_status_t hw_doip_read_ecu_serial_number(drv_doip_t *handle, char *serial_buffer, size_t buffer_size);
drv_doip_status_t hw_doip_read_vehicle_speed(drv_doip_t *handle, uint16_t *speed_kmh);
drv_doip_status_t hw_doip_read_engine_rpm(drv_doip_t *handle, uint16_t *rpm);
drv_doip_status_t hw_doip_read_battery_voltage(drv_doip_t *handle, uint16_t *voltage_mv);
drv_doip_status_t hw_doip_read_temperature_data(drv_doip_t *handle, int16_t *temperature_celsius);
drv_doip_status_t hw_doip_read_fuel_level(drv_doip_t *handle, uint8_t *fuel_percent);

drv_doip_state_t hw_doip_get_status(drv_doip_t *handle);
drv_doip_status_t hw_doip_register_callback(drv_doip_t *handle, drv_doip_cb_type_t type, 
                                           drv_doip_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_DOIP_H_