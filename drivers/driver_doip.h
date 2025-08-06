/**
 * @file driver_doip.h
 * @brief Universal DoIP (Diagnostics over Internet Protocol) Driver Interface
 * 
 * @details This header provides a hardware-agnostic DoIP client driver implementation
 * following ISO 13400 standard for automotive diagnostic communication. The driver
 * supports vehicle discovery, diagnostic messaging, alive check protocols, and 
 * multi-ECU environments.
 * 
 * @section features Key Features
 * - ISO 13400 compliant DoIP protocol implementation
 * - Vehicle discovery via UDP broadcast
 * - TCP diagnostic communication with UDS support
 * - Multi-ECU discovery and caching
 * - Raw packet handling with fragmentation support
 * - Comprehensive callback system for event-driven operation
 * - Memory-safe utility functions with bounds checking
 * - Universal driver pattern for hardware abstraction
 * 
 * @section architecture Driver Architecture
 * The driver follows a three-layer architecture:
 * 1. Universal Driver Interface (this file) - Hardware-agnostic API
 * 2. Universal Driver Implementation (driver_doip.c) - Common logic and validation
 * 3. BSP Driver Implementation (hw/platform/bsp_doip.c) - Hardware-specific code
 * 
 * @section usage Basic Usage Example
 * Basic usage involves: init, discover, connect, send requests
 * See function documentation below for detailed usage
 * 
 * @author  SAME54 DoIP Project
 * @date    2024
 * @version 1.0
 * 
 * @copyright MIT License - Free to use, modify, and distribute
 */

#ifndef _DRIVER_DOIP_H_
#define _DRIVER_DOIP_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"

/**
 * @defgroup doip_constants DoIP Protocol Constants
 * @brief Standard DoIP protocol constants as defined in ISO 13400
 * @{
 */
#define DOIP_UDP_DISCOVERY_PORT         13400  /**< UDP port for vehicle discovery */
#define DOIP_TCP_DATA_PORT             13400  /**< TCP port for diagnostic data */
#define DOIP_PROTOCOL_VERSION          0x02   /**< DoIP protocol version (ISO 13400) */
#define DOIP_INVERSE_PROTOCOL_VERSION  0xFD   /**< Inverse protocol version (~0x02) */
#define DOIP_HEADER_SIZE               8      /**< Size of DoIP header in bytes */
#define DOIP_CLIENT_SOURCE_ADDRESS     0x0E80 /**< Default client logical address */
#define DOIP_DISCOVERY_TIMEOUT_MS      5000   /**< Vehicle discovery timeout */
#define DOIP_TCP_TIMEOUT_MS           10000   /**< TCP connection timeout */
#define DOIP_MAX_PAYLOAD_SIZE         1024    /**< Maximum payload size in bytes */
/** @} */

/**
 * @defgroup doip_payload_types DoIP Payload Types
 * @brief Message payload type constants from ISO 13400
 * @{
 */
#define DOIP_VEHICLE_IDENTIFICATION_REQUEST     0x0001  /**< Vehicle ID request */
#define DOIP_VEHICLE_IDENTIFICATION_RESPONSE    0x0004  /**< Vehicle ID response */
#define DOIP_ROUTING_ACTIVATION_REQUEST         0x0005  /**< Routing activation request */
#define DOIP_ROUTING_ACTIVATION_RESPONSE        0x0006  /**< Routing activation response */
#define DOIP_ALIVE_CHECK_REQUEST                0x0007  /**< Alive check request */
#define DOIP_ALIVE_CHECK_RESPONSE               0x0008  /**< Alive check response */
#define DOIP_DIAGNOSTIC_MESSAGE                 0x8001  /**< Diagnostic message */
#define DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK    0x8002  /**< Diagnostic positive ACK */
#define DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK    0x8003  /**< Diagnostic negative ACK */
/** @} */

/**
 * @defgroup uds_services UDS Service Identifiers
 * @brief Unified Diagnostic Services (UDS) service IDs
 * @{
 */
#define UDS_READ_DATA_BY_IDENTIFIER     0x22  /**< Read Data by Identifier service */
#define UDS_POSITIVE_RESPONSE_MASK      0x40  /**< Positive response bit mask */
/** @} */

/**
 * @defgroup did_basic Basic Data Identifiers (DIDs)
 * @brief Standard vehicle identification DIDs
 * @{
 */
#define DID_VIN                         0xF190  /**< Vehicle Identification Number */
#define DID_ECU_SOFTWARE_VERSION        0xF1A0  /**< ECU software version */
#define DID_ECU_HARDWARE_VERSION        0xF1A1  /**< ECU hardware version */
/** @} */

/**
 * @defgroup did_system System Information DIDs
 * @brief Data identifiers for ECU system information
 * @{
 */
#define DID_ACTIVE_DIAGNOSTIC_SESSION           0xF186  /**< Active diagnostic session */
#define DID_VEHICLE_MANUFACTURER_SPARE_PART_NUMBER      0xF187  /**< Spare part number */
#define DID_VEHICLE_MANUFACTURER_ECU_SW_NUMBER          0xF188  /**< ECU software number */
#define DID_VEHICLE_MANUFACTURER_ECU_SW_VERSION         0xF189  /**< ECU software version */
#define DID_SYSTEM_SUPPLIER_IDENTIFIER                  0xF18A  /**< System supplier ID */
#define DID_ECU_MANUFACTURING_DATE                      0xF18B  /**< ECU manufacturing date */
#define DID_ECU_SERIAL_NUMBER                           0xF18C  /**< ECU serial number */
#define DID_VEHICLE_MANUFACTURER_KIT_ASSEMBLY_PART_NUMBER   0xF192  /**< Kit assembly part number */
/** @} */

/**
 * @defgroup did_network Network/Communication DIDs
 * @brief Data identifiers for network and communication parameters
 * @{
 */
#define DID_VEHICLE_MANUFACTURER_ECU_NETWORK_NAME       0xF1A2  /**< ECU network name */
#define DID_VEHICLE_MANUFACTURER_ECU_NETWORK_ADDRESS    0xF1A3  /**< ECU network address */
#define DID_VEHICLE_IDENTIFICATION_DATA_TRACEABILITY    0xF1A4  /**< Vehicle ID traceability */
#define DID_VEHICLE_MANUFACTURER_ECU_PIN_TRACEABILITY   0xF1A5  /**< ECU PIN traceability */
/** @} */

/**
 * @defgroup did_runtime Runtime Monitoring DIDs
 * @brief Data identifiers for real-time vehicle/ECU monitoring
 * @{
 */
#define DID_ECU_OPERATING_HOURS                         0xF1A6  /**< ECU operating hours */
#define DID_VEHICLE_SPEED_INFORMATION                   0xF1A7  /**< Vehicle speed (km/h) */
#define DID_ENGINE_RPM_INFORMATION                      0xF1A8  /**< Engine RPM */
#define DID_BATTERY_VOLTAGE_INFORMATION                 0xF1A9  /**< Battery voltage (mV) */
#define DID_TEMPERATURE_SENSOR_DATA                     0xF1AA  /**< Temperature sensors */
#define DID_FUEL_LEVEL_INFORMATION                      0xF1AB  /**< Fuel level (%) */
/** @} */

/**
 * @defgroup did_diagnostics Diagnostic Status DIDs
 * @brief Data identifiers for diagnostic and error status
 * @{
 */
#define DID_ERROR_MEMORY_STATUS                         0xF1AC  /**< Error memory status */
#define DID_LAST_RESET_REASON                           0xF1AD  /**< Last reset reason */
#define DID_BOOT_SOFTWARE_IDENTIFICATION                0xF1AE  /**< Boot software ID */
#define DID_APPLICATION_SOFTWARE_FINGERPRINT            0xF1AF  /**< App software fingerprint */
/** @} */

/**
 * @defgroup doip_config DoIP Task Configuration
 * @brief FreeRTOS task configuration parameters
 * @{
 */
#define DOIP_CLIENT_TASK_PRIORITY       (tskIDLE_PRIORITY + 3)  /**< DoIP client task priority */
#define DOIP_CLIENT_TASK_STACK_SIZE     (2048)                  /**< Task stack size in bytes */
/** @} */

/**
 * @defgroup doip_structures Data Structures
 * @brief Core data structures for DoIP driver operation
 * @{
 */

/**
 * @brief DoIP message structure
 * @details Represents a complete DoIP message with header and payload
 */
typedef struct {
    uint8_t  protocol_version;         /**< DoIP protocol version (0x02) */
    uint8_t  inverse_protocol_version; /**< Inverse protocol version (0xFD) */
    uint16_t payload_type;             /**< Message payload type identifier */
    uint32_t payload_length;           /**< Length of payload data in bytes */
    uint8_t  payload[DOIP_MAX_PAYLOAD_SIZE]; /**< Message payload data */
} doip_message_t;

/**
 * @brief DoIP driver status codes
 * @details Return codes for all DoIP driver operations
 */
typedef enum {
    DRV_DOIP_STATUS_OK = 0,          /**< Operation completed successfully */
    DRV_DOIP_STATUS_ERROR = 1,       /**< General error occurred */
    DRV_DOIP_STATUS_BUSY = 2,        /**< Driver is busy with another operation */
    DRV_DOIP_STATUS_TIMEOUT = 3,     /**< Operation timed out */
    DRV_DOIP_STATUS_NO_VEHICLE = 4,  /**< No vehicle found during discovery */
    DRV_DOIP_STATUS_DISCONNECTED = 5,/**< Connection lost or disconnected */
} drv_doip_status_t;

/**
 * @brief DoIP client state machine states
 * @details Tracks the current state of the DoIP client connection
 */
typedef enum {
    DRV_DOIP_STATE_IDLE = 0,        /**< Driver idle, not connected */
    DRV_DOIP_STATE_DISCOVERING = 1, /**< Searching for vehicles */
    DRV_DOIP_STATE_DISCOVERED = 2,  /**< Vehicle(s) discovered */
    DRV_DOIP_STATE_CONNECTING = 3,  /**< Establishing TCP connection */
    DRV_DOIP_STATE_CONNECTED = 4,   /**< TCP connected, routing pending */
    DRV_DOIP_STATE_ACTIVATED = 5,   /**< Routing activated, ready for diagnostics */
    DRV_DOIP_STATE_ERROR = 6,       /**< Error state, driver unusable */
} drv_doip_state_t;

/**
 * @brief DoIP callback event types
 * @details Event types that trigger registered callbacks
 */
typedef enum {
    DRV_DOIP_CB_VEHICLE_DISCOVERED = 0,    /**< Vehicle discovered during search */
    DRV_DOIP_CB_CONNECTION_ESTABLISHED = 1,/**< TCP connection established */
    DRV_DOIP_CB_CONNECTION_LOST = 2,       /**< Connection lost unexpectedly */
    DRV_DOIP_CB_DIAGNOSTIC_RESPONSE = 3,   /**< Diagnostic response received */
    DRV_DOIP_CB_ERROR = 4,                 /**< Error occurred */
    DRV_DOIP_CB_RAW_PACKET_RECEIVED = 5,   /**< Raw DoIP packet received */
    DRV_DOIP_CB_PACKET_FRAGMENT = 6,       /**< Packet fragment received */
} drv_doip_cb_type_t;

/**
 * @brief DoIP callback function type
 * @param type Event type that triggered the callback
 * @param data Pointer to event-specific data (can be NULL)
 * @param data_len Length of data in bytes
 */
typedef void (*drv_doip_callback_t)(drv_doip_cb_type_t type, const void *data, size_t data_len);

// Forward declaration for raw packet structure
struct drv_doip_raw_packet;

/**
 * @brief Raw packet callback function type
 * @param packet Pointer to received raw DoIP packet structure
 */
typedef void (*drv_doip_packet_callback_t)(const struct drv_doip_raw_packet *packet);

/**
 * @brief Vehicle information structure
 * @details Contains all discovered information about a DoIP-enabled vehicle/ECU
 */
typedef struct {
    char     vin[18];           /**< Vehicle Identification Number (17 chars + null) */
    uint16_t logical_address;   /**< ECU logical address for DoIP routing */
    uint8_t  entity_id[6];      /**< DoIP entity identifier (MAC-like) */
    uint8_t  group_id[2];       /**< DoIP group identifier */
    uint32_t ip_address;        /**< ECU IP address (network byte order) */
    uint16_t tcp_port;          /**< TCP port for diagnostic data */
} drv_doip_vehicle_info_t;

/**
 * @brief System monitoring data structure
 * @details Comprehensive ECU monitoring data collected via diagnostic requests
 * @note All string fields are null-terminated
 */
typedef struct {
    /** @name System Information
     * Basic ECU identification and configuration data
     * @{
     */
    uint8_t  active_diagnostic_session;     /**< Current diagnostic session type */
    char     spare_part_number[32];         /**< Manufacturer spare part number */
    char     ecu_sw_number[32];             /**< ECU software part number */
    char     ecu_sw_version_detailed[32];   /**< Detailed software version string */
    char     system_supplier_id[16];        /**< System supplier identifier */
    char     ecu_manufacturing_date[16];    /**< ECU manufacturing date */
    char     ecu_serial_number[32];         /**< ECU serial number */
    char     kit_assembly_part_number[32];  /**< Kit assembly part number */
    /** @} */
    
    /** @name Network Information
     * Network and communication configuration
     * @{
     */
    char     ecu_network_name[32];              /**< ECU network name/identifier */
    char     ecu_network_address[16];           /**< ECU network address string */
    char     identification_data_traceability[64]; /**< Vehicle ID traceability data */
    char     ecu_pin_traceability[32];          /**< ECU PIN traceability information */
    /** @} */
    
    /** @name Runtime Monitoring
     * Real-time vehicle and ECU operational data
     * @{
     */
    uint32_t ecu_operating_hours;       /**< Total ECU operating time in hours */
    uint16_t vehicle_speed_kmh;         /**< Current vehicle speed in km/h */
    uint16_t engine_rpm;                /**< Current engine RPM */
    uint16_t battery_voltage_mv;        /**< Battery voltage in millivolts */
    int16_t  temperature_celsius;       /**< ECU temperature in Celsius * 10 */
    uint8_t  fuel_level_percent;        /**< Fuel level as percentage (0-100) */
    /** @} */
    
    /** @name Diagnostic Status
     * Error and diagnostic status information
     * @{
     */
    uint8_t  error_memory_status;           /**< Error memory status flags */
    uint8_t  last_reset_reason;             /**< Last ECU reset reason code */
    char     boot_software_id[32];          /**< Boot software identification */
    char     application_sw_fingerprint[64]; /**< Application software fingerprint */
    /** @} */
} drv_doip_system_monitoring_t;

// Raw DOIP packet structure for universal packet handling
typedef struct drv_doip_raw_packet {
    // DOIP Header Information
    uint8_t  protocol_version;
    uint8_t  inverse_protocol_version;
    uint16_t payload_type;
    uint32_t payload_length;
    
    // Payload data
    uint8_t  payload[DOIP_MAX_PAYLOAD_SIZE];
    size_t   actual_payload_length;
    
    // Source information
    uint32_t source_ip_address;
    uint16_t source_port;
    
    // Fragment information (for large packets)
    bool     is_fragmented;
    uint16_t fragment_index;
    uint16_t total_fragments;
    uint32_t total_message_length;
    
    // Timing information
    uint32_t timestamp_ms;
} drv_doip_raw_packet_t;

// Raw packet listener configuration
typedef struct {
    bool     is_enabled;
    uint32_t timeout_ms;
    uint32_t max_fragments;
    drv_doip_packet_callback_t packet_callback;
} drv_doip_packet_listener_config_t;

// Driver structure with function pointers
typedef struct {
    bool is_init;
    drv_doip_state_t current_state;
    const void *hw_context;
    
    // Core operations
    drv_doip_status_t (*init)(const void *hw_context);
    drv_doip_status_t (*deinit)(const void *hw_context);
    
    // Discovery and connection
    drv_doip_status_t (*discover_vehicles)(const void *hw_context, drv_doip_vehicle_info_t *vehicle_info);
    drv_doip_status_t (*connect_to_vehicle)(const void *hw_context, const drv_doip_vehicle_info_t *vehicle_info);
    drv_doip_status_t (*disconnect)(const void *hw_context);
    
    // Diagnostic communication
    drv_doip_status_t (*send_diagnostic_request)(const void *hw_context, uint8_t service_id, uint16_t data_id, 
                                                uint8_t *response, size_t max_response_len, size_t *actual_len);
    
    // Raw DOIP messaging
    drv_doip_status_t (*send_raw_message)(const void *hw_context, const drv_doip_raw_packet_t *packet);
    drv_doip_status_t (*start_packet_listener)(const void *hw_context, const drv_doip_packet_listener_config_t *config);
    drv_doip_status_t (*stop_packet_listener)(const void *hw_context);
    drv_doip_status_t (*register_packet_callback)(const void *hw_context, drv_doip_packet_callback_t callback);
    
    // Status and callback management
    drv_doip_state_t (*get_status)(const void *hw_context);
    drv_doip_status_t (*register_callback)(const void *hw_context, drv_doip_cb_type_t type, 
                                          drv_doip_callback_t callback);
} drv_doip_t;
/** @} */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup doip_api Public API Functions
 * @brief Universal DoIP driver API functions
 * @{
 */

/**
 * @brief Initialize the DoIP driver
 * @param handle Pointer to DoIP driver instance
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 * @note Must be called before any other driver operations
 */
drv_doip_status_t hw_doip_init(drv_doip_t *handle);

/**
 * @brief Deinitialize the DoIP driver
 * @param handle Pointer to DoIP driver instance
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 * @note Releases all resources and disconnects from vehicles
 */
drv_doip_status_t hw_doip_deinit(drv_doip_t *handle);

/**
 * @brief Discover DoIP-enabled vehicles on the network
 * @param handle Pointer to DoIP driver instance
 * @param vehicle_info Pointer to structure to store discovered vehicle information
 * @return DRV_DOIP_STATUS_OK if vehicle found, DRV_DOIP_STATUS_NO_VEHICLE if none found
 * @note Performs UDP broadcast discovery with configurable timeout
 */
drv_doip_status_t hw_doip_discover_vehicles(drv_doip_t *handle, drv_doip_vehicle_info_t *vehicle_info);

/**
 * @brief Connect to a discovered vehicle
 * @param handle Pointer to DoIP driver instance
 * @param vehicle_info Pointer to vehicle information from discovery
 * @return DRV_DOIP_STATUS_OK on successful connection, error code otherwise
 * @note Establishes TCP connection and performs routing activation
 */
drv_doip_status_t hw_doip_connect_to_vehicle(drv_doip_t *handle, const drv_doip_vehicle_info_t *vehicle_info);

/**
 * @brief Disconnect from the currently connected vehicle
 * @param handle Pointer to DoIP driver instance
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 * @note Gracefully closes TCP connection and cleans up resources
 */
drv_doip_status_t hw_doip_disconnect(drv_doip_t *handle);

/**
 * @brief Send a diagnostic request to the connected vehicle
 * @param handle Pointer to DoIP driver instance
 * @param service_id UDS service identifier (e.g., UDS_READ_DATA_BY_IDENTIFIER)
 * @param data_id Data identifier (DID) for the request
 * @param response Buffer to store the response data
 * @param max_response_len Maximum size of response buffer
 * @param actual_len Pointer to store actual response length
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 * @note Vehicle must be connected before calling this function
 */
drv_doip_status_t hw_doip_send_diagnostic_request(drv_doip_t *handle, uint8_t service_id, uint16_t data_id, 
                                                 uint8_t *response, size_t max_response_len, size_t *actual_len);

/**
 * @brief Send a raw DoIP message
 * @param handle Pointer to DoIP driver instance
 * @param packet Pointer to raw DoIP packet structure
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 * @note For advanced users requiring custom DoIP message handling
 */
drv_doip_status_t hw_doip_send_raw_message(drv_doip_t *handle, const drv_doip_raw_packet_t *packet);

/**
 * @brief Start raw packet listener for monitoring DoIP traffic
 * @param handle Pointer to DoIP driver instance
 * @param config Listener configuration (timeout, callback, etc.)
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 * @note Enables promiscuous monitoring of DoIP packets
 */
drv_doip_status_t hw_doip_start_packet_listener(drv_doip_t *handle, const drv_doip_packet_listener_config_t *config);

/**
 * @brief Stop the raw packet listener
 * @param handle Pointer to DoIP driver instance
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 */
drv_doip_status_t hw_doip_stop_packet_listener(drv_doip_t *handle);

/**
 * @brief Register a callback for raw packet events
 * @param handle Pointer to DoIP driver instance
 * @param callback Function to call when packets are received
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 */
drv_doip_status_t hw_doip_register_packet_callback(drv_doip_t *handle, drv_doip_packet_callback_t callback);

/**
 * @brief Get the current driver status/state
 * @param handle Pointer to DoIP driver instance
 * @return Current driver state (idle, connected, error, etc.)
 */
drv_doip_state_t hw_doip_get_status(drv_doip_t *handle);

/**
 * @brief Register a callback for driver events
 * @param handle Pointer to DoIP driver instance
 * @param type Type of event to register for
 * @param callback Function to call when event occurs
 * @return DRV_DOIP_STATUS_OK on success, error code otherwise
 * @note Multiple callbacks can be registered for different event types
 */
drv_doip_status_t hw_doip_register_callback(drv_doip_t *handle, drv_doip_cb_type_t type, 
                                           drv_doip_callback_t callback);
/** @} */

/**
 * @defgroup doip_multi_ecu Multi-ECU Support
 * @brief Structures and constants for managing multiple ECU discovery
 * @{
 */

/** Maximum number of ECUs that can be cached during discovery */
#define DOIP_MAX_DISCOVERED_ECUS 8

/**
 * @brief Multi-ECU discovery cache
 * @details Stores information about multiple discovered ECUs
 */
typedef struct {
    drv_doip_vehicle_info_t vehicles[DOIP_MAX_DISCOVERED_ECUS]; /**< Array of discovered vehicles */
    uint8_t count;          /**< Number of vehicles currently cached */
    uint8_t current_index;  /**< Index of currently selected vehicle */
} doip_multi_ecu_cache_t;
/** @} */

/**
 * @defgroup doip_utilities Utility Functions
 * @brief Helper functions for DoIP message handling and protocol operations
 * @{
 */

/**
 * @brief Create a DoIP message header
 * @param msg Pointer to message structure to populate
 * @param payload_type DoIP payload type identifier
 * @param payload_length Length of payload data in bytes
 */
void doip_utils_create_header(doip_message_t *msg, uint16_t payload_type, uint32_t payload_length);

/**
 * @brief Parse a DoIP message header from raw data
 * @param data Pointer to raw data buffer
 * @param data_len Length of data buffer
 * @param msg Pointer to message structure to populate
 * @return true if parsing successful, false on error
 * @note Includes memory safety bounds checking
 */
bool doip_utils_parse_header(const uint8_t *data, size_t data_len, doip_message_t *msg);

/**
 * @brief Validate DoIP protocol version fields
 * @param protocol_version Protocol version byte
 * @param inverse_protocol_version Inverse protocol version byte
 * @return true if valid, false otherwise
 */
bool doip_utils_validate_protocol(uint8_t protocol_version, uint8_t inverse_protocol_version);

/**
 * @brief Serialize a DoIP message to a buffer
 * @param msg Pointer to message structure
 * @param buffer Pointer to output buffer (must be large enough)
 * @note Assumes buffer is large enough - use safe version for bounds checking
 */
void doip_utils_serialize_message(const doip_message_t *msg, uint8_t *buffer);

/**
 * @brief Safely serialize a DoIP message with bounds checking
 * @param msg Pointer to message structure
 * @param buffer Pointer to output buffer
 * @param buffer_size Size of output buffer
 * @param bytes_written Pointer to store number of bytes written
 * @return true if serialization successful, false if buffer too small
 */
bool doip_utils_serialize_message_safe(const doip_message_t *msg, uint8_t *buffer, size_t buffer_size, size_t *bytes_written);

/**
 * @brief Parse multi-ECU discovery response
 * @param buffer Raw response data buffer
 * @param buffer_len Length of response data
 * @param source_ip IP address of responding ECU
 * @param cache Multi-ECU cache to populate
 * @return Number of new ECUs added to cache
 */
uint8_t doip_utils_parse_multi_ecu_discovery_response(const uint8_t *buffer, size_t buffer_len, 
                                                     uint32_t source_ip, doip_multi_ecu_cache_t *cache);

/**
 * @brief Extract vehicle information from response message
 * @param response_msg Parsed DoIP response message
 * @param source_ip IP address of source ECU
 * @param vehicle_info Pointer to structure to populate
 * @return true if extraction successful, false on error
 */
bool doip_utils_extract_vehicle_info(const doip_message_t *response_msg, uint32_t source_ip, 
                                     drv_doip_vehicle_info_t *vehicle_info);

/**
 * @brief Handle negative acknowledgment responses
 * @param payload_type Message payload type
 * @param payload Pointer to payload data
 * @param payload_length Length of payload
 * @param actual_len Pointer to store processed length
 * @return true if this was a negative ACK, false otherwise
 */
bool doip_utils_handle_negative_ack(uint16_t payload_type, const uint8_t *payload, 
                                    uint32_t payload_length, size_t *actual_len);

/**
 * @brief Create alive check request message
 * @param buffer Output buffer (must be at least 10 bytes)
 * @param source_address Client source address
 */
void doip_utils_create_alive_check_request(uint8_t *buffer, uint16_t source_address);

/**
 * @brief Create alive check response message
 * @param buffer Output buffer (must be at least 10 bytes)
 * @param request_payload Payload from original request
 */
void doip_utils_create_alive_check_response(uint8_t *buffer, const uint8_t *request_payload);

/**
 * @brief Handle alive check payload data
 * @param payload Pointer to alive check payload
 * @param payload_length Length of payload data
 * @param source_address Pointer to store extracted source address
 * @return true if payload valid, false otherwise
 */
bool doip_utils_handle_alive_check_payload(const uint8_t *payload, uint32_t payload_length, uint16_t *source_address);

/** @} */

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_DOIP_H_