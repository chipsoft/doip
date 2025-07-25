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

/* Data Identifiers (DIDs) */
#define DID_VIN                         0xF190
#define DID_ECU_SOFTWARE_VERSION        0xF1A0
#define DID_ECU_HARDWARE_VERSION        0xF1A1

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