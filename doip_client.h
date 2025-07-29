/**
 * \file doip_client.h
 * \brief DOIP (Diagnostics over Internet Protocol) Client Implementation
 * 
 * Implements ISO 13400 DOIP protocol client for automotive diagnostics
 * communication with ECU emulators and real automotive devices.
 */

#ifndef DOIP_CLIENT_H
#define DOIP_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* DOIP Protocol Constants */
#define DOIP_UDP_DISCOVERY_PORT         13400
#define DOIP_TCP_DATA_PORT             13400
#define DOIP_PROTOCOL_VERSION          0x02
#define DOIP_INVERSE_PROTOCOL_VERSION  0xFD

/* DOIP Header Size */
#define DOIP_HEADER_SIZE               8

/* DOIP Payload Types (ISO 13400) */
#define DOIP_VEHICLE_IDENTIFICATION_REQUEST     0x0001
#define DOIP_VEHICLE_IDENTIFICATION_RESPONSE    0x0004
#define DOIP_ROUTING_ACTIVATION_REQUEST         0x0005
#define DOIP_ROUTING_ACTIVATION_RESPONSE        0x0006
#define DOIP_ALIVE_CHECK_REQUEST                0x0007
#define DOIP_ALIVE_CHECK_RESPONSE               0x0008
#define DOIP_DIAGNOSTIC_MESSAGE                 0x8001
#define DOIP_DIAGNOSTIC_MESSAGE_POSITIVE_ACK    0x8002
#define DOIP_DIAGNOSTIC_MESSAGE_NEGATIVE_ACK    0x8003

/* UDS Service IDs */
#define UDS_READ_DATA_BY_IDENTIFIER     0x22
#define UDS_POSITIVE_RESPONSE_MASK      0x40

/* Data Identifiers (DIDs) - AUTOSAR Standard */
#define DID_VIN                         0xF190
#define DID_ECU_SOFTWARE_VERSION        0xF1A0
#define DID_ECU_HARDWARE_VERSION        0xF1A1

/* System Information DIDs */
#define DID_ACTIVE_DIAGNOSTIC_SESSION   0xF186
#define DID_VEHICLE_MANUFACTURER_SPARE_PART_NUMBER    0xF187
#define DID_VEHICLE_MANUFACTURER_ECU_SW_NUMBER        0xF188
#define DID_VEHICLE_MANUFACTURER_ECU_SW_VERSION       0xF189
#define DID_SYSTEM_SUPPLIER_IDENTIFIER  0xF18A
#define DID_ECU_MANUFACTURING_DATE      0xF18B
#define DID_ECU_SERIAL_NUMBER           0xF18C
#define DID_VEHICLE_MANUFACTURER_KIT_ASSEMBLY_PART_NUMBER  0xF192

/* Network/Communication DIDs */
#define DID_VEHICLE_MANUFACTURER_ECU_NETWORK_NAME     0xF1A2
#define DID_VEHICLE_MANUFACTURER_ECU_NETWORK_ADDRESS  0xF1A3
#define DID_VEHICLE_IDENTIFICATION_DATA_TRACEABILITY  0xF1A4
#define DID_VEHICLE_MANUFACTURER_ECU_PIN_TRACEABILITY 0xF1A5

/* Runtime Monitoring DIDs */
#define DID_ECU_OPERATING_HOURS         0xF1A6
#define DID_VEHICLE_SPEED_INFORMATION   0xF1A7
#define DID_ENGINE_RPM_INFORMATION      0xF1A8
#define DID_BATTERY_VOLTAGE_INFORMATION 0xF1A9
#define DID_TEMPERATURE_SENSOR_DATA     0xF1AA
#define DID_FUEL_LEVEL_INFORMATION      0xF1AB

/* Diagnostic Status DIDs */
#define DID_ERROR_MEMORY_STATUS         0xF1AC
#define DID_LAST_RESET_REASON           0xF1AD
#define DID_BOOT_SOFTWARE_IDENTIFICATION 0xF1AE
#define DID_APPLICATION_SOFTWARE_FINGERPRINT 0xF1AF

/* DOIP Client Configuration */
#define DOIP_CLIENT_SOURCE_ADDRESS      0x0E80  /* Tester address */
#define DOIP_DISCOVERY_TIMEOUT_MS       5000    /* UDP discovery timeout */
#define DOIP_TCP_TIMEOUT_MS            10000    /* TCP operation timeout */
#define DOIP_MAX_PAYLOAD_SIZE          1024     /* Maximum payload size */
#define DOIP_ALIVE_CHECK_INTERVAL_MS   5000     /* Alive check interval (5 seconds) */
#define DOIP_ALIVE_CHECK_TIMEOUT_MS    3000     /* Alive check response timeout */

/* DOIP Message Structure */
typedef struct {
    uint8_t  protocol_version;
    uint8_t  inverse_protocol_version;
    uint16_t payload_type;
    uint32_t payload_length;
    uint8_t  payload[DOIP_MAX_PAYLOAD_SIZE];
} doip_message_t;

/* Vehicle Information Structure */
typedef struct {
    char     vin[18];           /* Vehicle Identification Number (17 chars + null) */
    uint16_t logical_address;   /* ECU logical address */
    uint8_t  entity_id[6];      /* Entity identifier */
    uint8_t  group_id[2];       /* Group identifier */
    uint32_t ip_address;        /* ECU IP address */
    uint16_t tcp_port;          /* TCP data port */
} doip_vehicle_info_t;

/* System Monitoring Data Structure */
typedef struct {
    /* System Information */
    uint8_t  active_diagnostic_session;
    char     spare_part_number[32];
    char     ecu_sw_number[32];
    char     ecu_sw_version_detailed[32];
    char     system_supplier_id[16];
    char     ecu_manufacturing_date[16];
    char     ecu_serial_number[32];
    char     kit_assembly_part_number[32];
    
    /* Network Information */
    char     ecu_network_name[32];
    char     ecu_network_address[16];
    char     identification_data_traceability[64];
    char     ecu_pin_traceability[32];
    
    /* Runtime Monitoring */
    uint32_t ecu_operating_hours;
    uint16_t vehicle_speed_kmh;         /* km/h */
    uint16_t engine_rpm;                /* RPM */
    uint16_t battery_voltage_mv;        /* millivolts */
    int16_t  temperature_celsius;       /* Celsius * 10 */
    uint8_t  fuel_level_percent;        /* Percentage */
    
    /* Diagnostic Status */
    uint8_t  error_memory_status;
    uint8_t  last_reset_reason;
    char     boot_software_id[32];
    char     application_sw_fingerprint[64];
} doip_system_monitoring_t;

/* DOIP Client Status */
typedef enum {
    DOIP_STATUS_IDLE,
    DOIP_STATUS_DISCOVERING,
    DOIP_STATUS_DISCOVERED,
    DOIP_STATUS_CONNECTING,
    DOIP_STATUS_CONNECTED,
    DOIP_STATUS_ACTIVATED,
    DOIP_STATUS_ERROR
} doip_status_t;

/* Function Prototypes */

/**
 * \brief Initialize DOIP client
 * \return true if initialization successful, false otherwise
 */
bool doip_client_init(void);

/**
 * \brief Start DOIP client task
 * \return true if task started successfully, false otherwise
 */
bool doip_client_start_task(void);

/**
 * \brief Discover DOIP vehicles on network
 * \param[out] vehicle_info Pointer to store discovered vehicle information
 * \return true if vehicle discovered, false otherwise
 */
bool doip_discover_vehicles(doip_vehicle_info_t *vehicle_info);

/**
 * \brief Connect to DOIP vehicle
 * \param[in] vehicle_info Vehicle information from discovery
 * \return true if connection successful, false otherwise
 */
bool doip_connect_to_vehicle(const doip_vehicle_info_t *vehicle_info);

/**
 * \brief Send diagnostic request and receive response
 * \param[in] service_id UDS service identifier
 * \param[in] data_id Data identifier for read services
 * \param[out] response Buffer to store response data
 * \param[in] max_response_len Maximum response buffer size
 * \return Number of bytes received, -1 on error
 */
int doip_send_diagnostic_request(uint8_t service_id, uint16_t data_id, 
                                uint8_t *response, size_t max_response_len);

/**
 * \brief Read VIN from connected ECU
 * \param[out] vin_buffer Buffer to store VIN (minimum 18 bytes)
 * \return true if VIN read successfully, false otherwise
 */
bool doip_read_vin(char *vin_buffer);

/**
 * \brief Read ECU software version
 * \param[out] version_buffer Buffer to store version string
 * \param[in] buffer_size Size of version buffer
 * \return true if version read successfully, false otherwise
 */
bool doip_read_ecu_software_version(char *version_buffer, size_t buffer_size);

/**
 * \brief Read ECU hardware version
 * \param[out] version_buffer Buffer to store version string
 * \param[in] buffer_size Size of version buffer
 * \return true if version read successfully, false otherwise
 */
bool doip_read_ecu_hardware_version(char *version_buffer, size_t buffer_size);

/**
 * \brief Read system monitoring data by DID
 * \param[in] did Data Identifier to read
 * \param[out] response Buffer to store response data
 * \param[in] max_response_len Maximum response buffer size
 * \return Number of bytes received, -1 on error
 */
int doip_read_monitoring_data(uint16_t did, uint8_t *response, size_t max_response_len);

/**
 * \brief Get current system monitoring data
 * \param[out] monitoring_data Pointer to store monitoring data
 * \return true if data retrieved successfully, false otherwise
 */
bool doip_get_system_monitoring_data(doip_system_monitoring_t *monitoring_data);

/**
 * \brief Read active diagnostic session
 * \param[out] session_buffer Buffer to store session info
 * \param[in] buffer_size Size of session buffer
 * \return true if session read successfully, false otherwise
 */
bool doip_read_active_diagnostic_session(uint8_t *session_buffer, size_t buffer_size);

/**
 * \brief Read ECU serial number
 * \param[out] serial_buffer Buffer to store serial number
 * \param[in] buffer_size Size of serial buffer
 * \return true if serial read successfully, false otherwise
 */
bool doip_read_ecu_serial_number(char *serial_buffer, size_t buffer_size);

/**
 * \brief Read vehicle speed information
 * \param[out] speed_kmh Pointer to store speed in km/h
 * \return true if speed read successfully, false otherwise
 */
bool doip_read_vehicle_speed(uint16_t *speed_kmh);

/**
 * \brief Read engine RPM information
 * \param[out] rpm Pointer to store RPM value
 * \return true if RPM read successfully, false otherwise
 */
bool doip_read_engine_rpm(uint16_t *rpm);

/**
 * \brief Read battery voltage information
 * \param[out] voltage_mv Pointer to store voltage in millivolts
 * \return true if voltage read successfully, false otherwise
 */
bool doip_read_battery_voltage(uint16_t *voltage_mv);

/**
 * \brief Read temperature sensor data
 * \param[out] temperature_celsius Pointer to store temperature in Celsius * 10
 * \return true if temperature read successfully, false otherwise
 */
bool doip_read_temperature_data(int16_t *temperature_celsius);

/**
 * \brief Read fuel level information
 * \param[out] fuel_percent Pointer to store fuel level percentage
 * \return true if fuel level read successfully, false otherwise
 */
bool doip_read_fuel_level(uint8_t *fuel_percent);

/**
 * \brief Get current DOIP client status
 * \return Current client status
 */
doip_status_t doip_get_status(void);

/**
 * \brief Disconnect from current DOIP vehicle
 */
void doip_disconnect(void);

/**
 * \brief DOIP client main task function
 * \param[in] pvParameters FreeRTOS task parameters
 */
void doip_client_task(void *pvParameters);

/* Internal helper functions */
void doip_create_header(doip_message_t *msg, uint16_t payload_type, uint32_t payload_length);
bool doip_parse_header(const uint8_t *data, size_t data_len, doip_message_t *msg);
bool doip_send_udp_message(const doip_message_t *msg, uint32_t dest_ip, uint16_t dest_port);
bool doip_send_tcp_message(int socket, const doip_message_t *msg);
bool doip_receive_tcp_message(int socket, doip_message_t *msg, uint32_t timeout_ms);

/* Alive Check Functions */
bool doip_send_alive_check_request(int socket);
bool doip_handle_alive_check_response(const doip_message_t *msg);
bool doip_handle_alive_check_request(int socket, const doip_message_t *msg);

/* Enhanced Diagnostic Functions */
bool doip_send_diagnostic_ack(int socket, uint8_t ack_type);
bool doip_handle_diagnostic_ack(const doip_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* DOIP_CLIENT_H */