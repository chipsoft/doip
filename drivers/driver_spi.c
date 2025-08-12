#include "driver_spi.h"
#include "utils_assert.h"

drv_spi_status_t hw_spi_init(drv_spi_t *handle, const drv_spi_config_t *config)
{
    ASSERT(handle != NULL);
    ASSERT(config != NULL);
    ASSERT(handle->init != NULL);
    
    if (handle->is_init) {
        return DRV_SPI_STATUS_OK;
    }
    
    drv_spi_status_t result = handle->init(handle->hw_context, config);
    if (result == DRV_SPI_STATUS_OK) {
        handle->is_init = true;
    }
    
    return result;
}

drv_spi_status_t hw_spi_deinit(drv_spi_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    
    if (!handle->is_init) {
        return DRV_SPI_STATUS_OK;
    }
    
    drv_spi_status_t result = handle->deinit(handle->hw_context);
    if (result == DRV_SPI_STATUS_OK) {
        handle->is_init = false;
        handle->is_enabled = false;
    }
    
    return result;
}

drv_spi_status_t hw_spi_enable(drv_spi_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->enable != NULL);
    
    if (!handle->is_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    if (handle->is_enabled) {
        return DRV_SPI_STATUS_OK;
    }
    
    drv_spi_status_t result = handle->enable(handle->hw_context);
    if (result == DRV_SPI_STATUS_OK) {
        handle->is_enabled = true;
    }
    
    return result;
}

drv_spi_status_t hw_spi_disable(drv_spi_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->disable != NULL);
    
    if (!handle->is_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    if (!handle->is_enabled) {
        return DRV_SPI_STATUS_OK;
    }
    
    drv_spi_status_t result = handle->disable(handle->hw_context);
    if (result == DRV_SPI_STATUS_OK) {
        handle->is_enabled = false;
    }
    
    return result;
}

drv_spi_status_t hw_spi_transfer(drv_spi_t *handle, const uint8_t *tx_data, 
                                uint8_t *rx_data, uint32_t length)
{
    ASSERT(handle != NULL);
    ASSERT(handle->transfer != NULL);
    ASSERT(tx_data != NULL || rx_data != NULL);
    ASSERT(length > 0);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    return handle->transfer(handle->hw_context, tx_data, rx_data, length);
}

drv_spi_status_t hw_spi_transfer_async(drv_spi_t *handle, const uint8_t *tx_data, 
                                      uint8_t *rx_data, uint32_t length)
{
    ASSERT(handle != NULL);
    ASSERT(handle->transfer_async != NULL);
    ASSERT(tx_data != NULL || rx_data != NULL);
    ASSERT(length > 0);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    return handle->transfer_async(handle->hw_context, tx_data, rx_data, length);
}

drv_spi_status_t hw_spi_set_baudrate(drv_spi_t *handle, uint32_t baudrate)
{
    ASSERT(handle != NULL);
    ASSERT(handle->set_baudrate != NULL);
    ASSERT(baudrate > 0);
    
    if (!handle->is_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    return handle->set_baudrate(handle->hw_context, baudrate);
}

drv_spi_status_t hw_spi_register_callback(drv_spi_t *handle, 
                                         drv_spi_cb_type_t type, 
                                         drv_spi_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(handle->register_callback != NULL);
    
    if (!handle->is_init) {
        return DRV_SPI_STATUS_ERROR;
    }
    
    return handle->register_callback(handle->hw_context, type, callback);
}