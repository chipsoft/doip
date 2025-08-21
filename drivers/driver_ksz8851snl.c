#include "driver_ksz8851snl.h"
#include "utils_assert.h"

drv_ksz8851snl_status_t hw_ksz8851snl_init(drv_ksz8851snl_t *handle, const drv_ksz8851snl_config_t *config)
{
    ASSERT(handle != NULL);
    ASSERT(config != NULL);
    ASSERT(handle->init != NULL);
    
    if (handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    drv_ksz8851snl_status_t result = handle->init(handle->hw_context, config);
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        handle->is_init = true;
    }
    
    return result;
}

drv_ksz8851snl_status_t hw_ksz8851snl_deinit(drv_ksz8851snl_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    
    if (!handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    drv_ksz8851snl_status_t result = handle->deinit(handle->hw_context);
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        handle->is_init = false;
        handle->is_enabled = false;
    }
    
    return result;
}

drv_ksz8851snl_status_t hw_ksz8851snl_enable(drv_ksz8851snl_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->enable != NULL);
    
    if (!handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    if (handle->is_enabled) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    drv_ksz8851snl_status_t result = handle->enable(handle->hw_context);
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        handle->is_enabled = true;
    }
    
    return result;
}

drv_ksz8851snl_status_t hw_ksz8851snl_disable(drv_ksz8851snl_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->disable != NULL);
    
    if (!handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    if (!handle->is_enabled) {
        return DRV_KSZ8851SNL_STATUS_OK;
    }
    
    drv_ksz8851snl_status_t result = handle->disable(handle->hw_context);
    if (result == DRV_KSZ8851SNL_STATUS_OK) {
        handle->is_enabled = false;
    }
    
    return result;
}

drv_ksz8851snl_status_t hw_ksz8851snl_get_chip_id(drv_ksz8851snl_t *handle, drv_ksz8851snl_id_info_t *id_info)
{
    ASSERT(handle != NULL);
    ASSERT(id_info != NULL);
    ASSERT(handle->get_chip_id != NULL);
    
    if (!handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    return handle->get_chip_id(handle->hw_context, id_info);
}

drv_ksz8851snl_status_t hw_ksz8851snl_get_status(drv_ksz8851snl_t *handle, drv_ksz8851snl_status_info_t *status_info)
{
    ASSERT(handle != NULL);
    ASSERT(status_info != NULL);
    ASSERT(handle->get_status != NULL);
    
    if (!handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    return handle->get_status(handle->hw_context, status_info);
}

drv_ksz8851snl_status_t hw_ksz8851snl_send_packet(drv_ksz8851snl_t *handle, const uint8_t *data, uint16_t length)
{
    ASSERT(handle != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);
    ASSERT(handle->send_packet != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    return handle->send_packet(handle->hw_context, data, length);
}

drv_ksz8851snl_status_t hw_ksz8851snl_receive_packet(drv_ksz8851snl_t *handle, uint8_t *data, uint16_t *length)
{
    ASSERT(handle != NULL);
    ASSERT(data != NULL);
    ASSERT(length != NULL);
    ASSERT(handle->receive_packet != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    return handle->receive_packet(handle->hw_context, data, length);
}

drv_ksz8851snl_status_t hw_ksz8851snl_check_rx_available(drv_ksz8851snl_t *handle, bool *rx_available)
{
    ASSERT(handle != NULL);
    ASSERT(rx_available != NULL);
    ASSERT(handle->check_rx_available != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    return handle->check_rx_available(handle->hw_context, rx_available);
}

drv_ksz8851snl_status_t hw_ksz8851snl_register_callback(drv_ksz8851snl_t *handle, 
                                                       drv_ksz8851snl_cb_type_t type, 
                                                       drv_ksz8851snl_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(handle->register_callback != NULL);
    
    if (!handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    return handle->register_callback(handle->hw_context, type, callback);
}

drv_ksz8851snl_status_t hw_ksz8851snl_set_mac_address(drv_ksz8851snl_t *handle, const uint8_t mac_addr[6])
{
    ASSERT(handle != NULL);
    ASSERT(mac_addr != NULL);
    ASSERT(handle->set_mac_address != NULL);
    
    if (!handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    // Validate MAC address
    if (!hw_ksz8851snl_is_mac_valid(mac_addr)) {
        return DRV_KSZ8851SNL_STATUS_INVALID_MAC;
    }
    
    return handle->set_mac_address(handle->hw_context, mac_addr);
}

drv_ksz8851snl_status_t hw_ksz8851snl_get_mac_address(drv_ksz8851snl_t *handle, uint8_t mac_addr[6])
{
    ASSERT(handle != NULL);
    ASSERT(mac_addr != NULL);
    ASSERT(handle->get_mac_address != NULL);
    
    if (!handle->is_init) {
        return DRV_KSZ8851SNL_STATUS_ERROR;
    }
    
    return handle->get_mac_address(handle->hw_context, mac_addr);
}

bool hw_ksz8851snl_is_mac_valid(const uint8_t mac_addr[6])
{
    ASSERT(mac_addr != NULL);
    
    // Check for all-zero MAC address
    if ((mac_addr[0] | mac_addr[1] | mac_addr[2] | mac_addr[3] | mac_addr[4] | mac_addr[5]) == 0) {
        return false;
    }
    
    // Check for multicast MAC address (first bit of first byte)
    if (mac_addr[0] & 0x01) {
        return false;
    }
    
    return true;
}