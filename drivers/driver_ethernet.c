#include "driver_ethernet.h"
#include "utils_assert.h"

drv_eth_status_t hw_eth_init(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->init != NULL);
    
    if (handle->is_init) {
        return DRV_ETH_STATUS_OK;
    }
    
    drv_eth_status_t res = handle->init(handle->hw_context);
    if (res == DRV_ETH_STATUS_OK) {
        handle->is_init = true;
    }
    return res;
}

drv_eth_status_t hw_eth_deinit(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_OK;
    }
    
    drv_eth_status_t res = handle->deinit(handle->hw_context);
    if (res == DRV_ETH_STATUS_OK) {
        handle->is_init = false;
        handle->is_enabled = false;
    }
    return res;
}

drv_eth_status_t hw_eth_enable(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->enable != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    if (handle->is_enabled) {
        return DRV_ETH_STATUS_OK;
    }
    
    drv_eth_status_t res = handle->enable(handle->hw_context);
    if (res == DRV_ETH_STATUS_OK) {
        handle->is_enabled = true;
    }
    return res;
}

drv_eth_status_t hw_eth_disable(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->disable != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_ETH_STATUS_OK;
    }
    
    drv_eth_status_t res = handle->disable(handle->hw_context);
    if (res == DRV_ETH_STATUS_OK) {
        handle->is_enabled = false;
    }
    return res;
}

drv_eth_status_t hw_eth_phy_init(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->phy_init != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->phy_init(handle->hw_context);
}

drv_eth_status_t hw_eth_phy_reset(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->phy_reset != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->phy_reset(handle->hw_context);
}

drv_eth_status_t hw_eth_get_link_status(drv_eth_t *handle, bool *link_up)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_link_status != NULL);
    ASSERT(link_up != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->get_link_status(handle->hw_context, link_up);
}

drv_eth_status_t hw_eth_restart_autoneg(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->restart_autoneg != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->restart_autoneg(handle->hw_context);
}

drv_eth_status_t hw_eth_read_phy_reg(drv_eth_t *handle, uint16_t reg, uint16_t *value)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_phy_reg != NULL);
    ASSERT(value != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->read_phy_reg(handle->hw_context, reg, value);
}

drv_eth_status_t hw_eth_write_phy_reg(drv_eth_t *handle, uint16_t reg, uint16_t value)
{
    ASSERT(handle != NULL);
    ASSERT(handle->write_phy_reg != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->write_phy_reg(handle->hw_context, reg, value);
}

drv_eth_status_t hw_eth_register_callback(drv_eth_t *handle, drv_eth_cb_type_t type, drv_eth_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(handle->register_callback != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->register_callback(handle->hw_context, type, callback);
}

drv_eth_status_t hw_eth_write(drv_eth_t *handle, const uint8_t *data, uint32_t length)
{
    ASSERT(handle != NULL);
    ASSERT(handle->write != NULL);
    ASSERT(data != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->write(handle->hw_context, data, length);
}

drv_eth_tcpip_init_done_fn hw_eth_get_tcpip_init_done_fn(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_tcpip_init_done_fn != NULL);
    
    if (!handle->is_init) {
        return NULL;
    }
    
    return handle->get_tcpip_init_done_fn(handle->hw_context);
}

drv_eth_status_t hw_eth_start_link_monitor(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->start_link_monitor != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->start_link_monitor(handle->hw_context);
}

drv_eth_status_t hw_eth_stop_link_monitor(drv_eth_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->stop_link_monitor != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_STATUS_ERROR;
    }
    
    return handle->stop_link_monitor(handle->hw_context);
}