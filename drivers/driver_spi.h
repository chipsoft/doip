#ifndef _DRIVER_SPI_H_
#define _DRIVER_SPI_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DRV_SPI_STATUS_OK = 0,
    DRV_SPI_STATUS_ERROR = 1,
    DRV_SPI_STATUS_BUSY = 2,
    DRV_SPI_STATUS_TIMEOUT = 3,
    DRV_SPI_STATUS_INVALID_PARAM = 4,
} drv_spi_status_t;

typedef enum {
    DRV_SPI_CB_TRANSFER_COMPLETE = 0,
    DRV_SPI_CB_ERROR = 1,
} drv_spi_cb_type_t;

typedef void (*drv_spi_callback_t)(void);

typedef struct {
    uint32_t baudrate;
    uint8_t clock_polarity;    // 0 or 1
    uint8_t clock_phase;       // 0 or 1
    uint8_t bits_per_transfer; // Typically 8
    uint8_t cs_delay_before;   // CS setup time
    uint8_t cs_delay_after;    // CS hold time
} drv_spi_config_t;

typedef struct {
    bool is_init;
    bool is_enabled;
    const void *hw_context;
    
    drv_spi_status_t (*init)(const void *hw_context, const drv_spi_config_t *config);
    drv_spi_status_t (*deinit)(const void *hw_context);
    drv_spi_status_t (*enable)(const void *hw_context);
    drv_spi_status_t (*disable)(const void *hw_context);
    
    drv_spi_status_t (*transfer)(const void *hw_context, const uint8_t *tx_data, 
                                uint8_t *rx_data, uint32_t length);
    drv_spi_status_t (*transfer_async)(const void *hw_context, const uint8_t *tx_data, 
                                      uint8_t *rx_data, uint32_t length);
    
    drv_spi_status_t (*set_baudrate)(const void *hw_context, uint32_t baudrate);
    drv_spi_status_t (*register_callback)(const void *hw_context, 
                                         drv_spi_cb_type_t type, 
                                         drv_spi_callback_t callback);
} drv_spi_t;

#ifdef __cplusplus
extern "C" {
#endif

drv_spi_status_t hw_spi_init(drv_spi_t *handle, const drv_spi_config_t *config);
drv_spi_status_t hw_spi_deinit(drv_spi_t *handle);
drv_spi_status_t hw_spi_enable(drv_spi_t *handle);
drv_spi_status_t hw_spi_disable(drv_spi_t *handle);
drv_spi_status_t hw_spi_transfer(drv_spi_t *handle, const uint8_t *tx_data, 
                                uint8_t *rx_data, uint32_t length);
drv_spi_status_t hw_spi_transfer_async(drv_spi_t *handle, const uint8_t *tx_data, 
                                      uint8_t *rx_data, uint32_t length);
drv_spi_status_t hw_spi_set_baudrate(drv_spi_t *handle, uint32_t baudrate);
drv_spi_status_t hw_spi_register_callback(drv_spi_t *handle, 
                                         drv_spi_cb_type_t type, 
                                         drv_spi_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_SPI_H_