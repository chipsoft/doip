#include "driver_phy.h"
#include "utils_assert.h"

drv_phy_status_t hw_phy_init(drv_phy_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->init != NULL);
    
    if (handle->is_init) {
        return DRV_PHY_STATUS_OK;
    }
    
    drv_phy_status_t result = handle->init(handle->hw_context);
    if (result == DRV_PHY_STATUS_OK) {
        handle->is_init = true;
    }
    
    return result;
}

drv_phy_status_t hw_phy_deinit(drv_phy_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_OK;
    }
    
    drv_phy_status_t result = handle->deinit(handle->hw_context);
    if (result == DRV_PHY_STATUS_OK) {
        handle->is_init = false;
        handle->is_enabled = false;
    }
    
    return result;
}

drv_phy_status_t hw_phy_enable(drv_phy_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->enable != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    if (handle->is_enabled) {
        return DRV_PHY_STATUS_OK;
    }
    
    drv_phy_status_t result = handle->enable(handle->hw_context);
    if (result == DRV_PHY_STATUS_OK) {
        handle->is_enabled = true;
    }
    
    return result;
}

drv_phy_status_t hw_phy_disable(drv_phy_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->disable != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_PHY_STATUS_OK;
    }
    
    drv_phy_status_t result = handle->disable(handle->hw_context);
    if (result == DRV_PHY_STATUS_OK) {
        handle->is_enabled = false;
    }
    
    return result;
}

drv_phy_status_t hw_phy_reset(drv_phy_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->reset != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->reset(handle->hw_context);
}

drv_phy_status_t hw_phy_get_link_status(drv_phy_t *handle, bool *link_up)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_link_status != NULL);
    ASSERT(link_up != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->get_link_status(handle->hw_context, link_up);
}

drv_phy_status_t hw_phy_restart_autoneg(drv_phy_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->restart_autoneg != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->restart_autoneg(handle->hw_context);
}

drv_phy_status_t hw_phy_read_reg(drv_phy_t *handle, uint16_t reg, uint16_t *value)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_reg != NULL);
    ASSERT(value != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->read_reg(handle->hw_context, reg, value);
}

drv_phy_status_t hw_phy_write_reg(drv_phy_t *handle, uint16_t reg, uint16_t value)
{
    ASSERT(handle != NULL);
    ASSERT(handle->write_reg != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->write_reg(handle->hw_context, reg, value);
}

drv_phy_status_t hw_phy_set_powerdown(drv_phy_t *handle, bool state)
{
    ASSERT(handle != NULL);
    ASSERT(handle->set_powerdown != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->set_powerdown(handle->hw_context, state);
}

drv_phy_status_t hw_phy_set_isolate(drv_phy_t *handle, bool state)
{
    ASSERT(handle != NULL);
    ASSERT(handle->set_isolate != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->set_isolate(handle->hw_context, state);
}

drv_phy_status_t hw_phy_set_loopback(drv_phy_t *handle, bool state)
{
    ASSERT(handle != NULL);
    ASSERT(handle->set_loopback != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->set_loopback(handle->hw_context, state);
}

drv_phy_status_t hw_phy_set_reg_bit(drv_phy_t *handle, uint16_t reg, uint16_t mask)
{
    ASSERT(handle != NULL);
    ASSERT(handle->set_reg_bit != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->set_reg_bit(handle->hw_context, reg, mask);
}

drv_phy_status_t hw_phy_clear_reg_bit(drv_phy_t *handle, uint16_t reg, uint16_t mask)
{
    ASSERT(handle != NULL);
    ASSERT(handle->clear_reg_bit != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->clear_reg_bit(handle->hw_context, reg, mask);
}

drv_phy_status_t hw_phy_register_callback(drv_phy_t *handle, 
                                         drv_phy_cb_type_t type, 
                                         drv_phy_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(handle->register_callback != NULL);
    
    if (!handle->is_init) {
        return DRV_PHY_STATUS_ERROR;
    }
    
    return handle->register_callback(handle->hw_context, type, callback);
}