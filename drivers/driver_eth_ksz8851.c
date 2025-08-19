#include "driver_eth_ksz8851.h"
#include "utils_assert.h"

drv_eth_ksz8851_status_t hw_eth_ksz8851_init(drv_eth_ksz8851_t *handle, const drv_eth_ksz8851_config_t *config)
{
    ASSERT(handle != NULL);
    ASSERT(config != NULL);
    ASSERT(handle->init != NULL);
    
    if (handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_OK;
    }
    
    drv_eth_ksz8851_status_t result = handle->init(handle->hw_context, config);
    if (result == DRV_ETH_KSZ8851_STATUS_OK) {
        handle->is_init = true;
    }
    
    return result;
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_deinit(drv_eth_ksz8851_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_OK;
    }
    
    drv_eth_ksz8851_status_t result = handle->deinit(handle->hw_context);
    if (result == DRV_ETH_KSZ8851_STATUS_OK) {
        handle->is_init = false;
        handle->is_enabled = false;
    }
    
    return result;
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_enable(drv_eth_ksz8851_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->enable != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    if (handle->is_enabled) {
        return DRV_ETH_KSZ8851_STATUS_OK;
    }
    
    drv_eth_ksz8851_status_t result = handle->enable(handle->hw_context);
    if (result == DRV_ETH_KSZ8851_STATUS_OK) {
        handle->is_enabled = true;
    }
    
    return result;
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_disable(drv_eth_ksz8851_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->disable != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    if (!handle->is_enabled) {
        return DRV_ETH_KSZ8851_STATUS_OK;
    }
    
    drv_eth_ksz8851_status_t result = handle->disable(handle->hw_context);
    if (result == DRV_ETH_KSZ8851_STATUS_OK) {
        handle->is_enabled = false;
    }
    
    return result;
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_read_reg(drv_eth_ksz8851_t *handle, uint16_t reg, uint16_t *value)
{
    ASSERT(handle != NULL);
    ASSERT(handle->read_reg != NULL);
    ASSERT(value != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->read_reg(handle->hw_context, reg, value);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_write_reg(drv_eth_ksz8851_t *handle, uint16_t reg, uint16_t value)
{
    ASSERT(handle != NULL);
    ASSERT(handle->write_reg != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->write_reg(handle->hw_context, reg, value);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_set_bits(drv_eth_ksz8851_t *handle, uint16_t reg, uint16_t mask)
{
    ASSERT(handle != NULL);
    ASSERT(handle->set_bits != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->set_bits(handle->hw_context, reg, mask);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_clear_bits(drv_eth_ksz8851_t *handle, uint16_t reg, uint16_t mask)
{
    ASSERT(handle != NULL);
    ASSERT(handle->clear_bits != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->clear_bits(handle->hw_context, reg, mask);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_send_packet(drv_eth_ksz8851_t *handle, const uint8_t *data, uint16_t length)
{
    ASSERT(handle != NULL);
    ASSERT(handle->send_packet != NULL);
    ASSERT(data != NULL);
    ASSERT(length > 0);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->send_packet(handle->hw_context, data, length);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_receive_packet(drv_eth_ksz8851_t *handle, uint8_t *data, uint16_t max_length, uint16_t *actual_length)
{
    ASSERT(handle != NULL);
    ASSERT(handle->receive_packet != NULL);
    ASSERT(data != NULL);
    ASSERT(actual_length != NULL);
    ASSERT(max_length > 0);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->receive_packet(handle->hw_context, data, max_length, actual_length);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_get_rx_status(drv_eth_ksz8851_t *handle, drv_eth_ksz8851_rx_status_t *rx_status)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_rx_status != NULL);
    ASSERT(rx_status != NULL);
    
    if (!handle->is_init || !handle->is_enabled) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->get_rx_status(handle->hw_context, rx_status);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_get_link_status(drv_eth_ksz8851_t *handle, bool *link_up)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_link_status != NULL);
    ASSERT(link_up != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->get_link_status(handle->hw_context, link_up);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_restart_autoneg(drv_eth_ksz8851_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->restart_autoneg != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->restart_autoneg(handle->hw_context);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_get_link_speed(drv_eth_ksz8851_t *handle, bool *speed_100mbps, bool *full_duplex)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_link_speed != NULL);
    ASSERT(speed_100mbps != NULL);
    ASSERT(full_duplex != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->get_link_speed(handle->hw_context, speed_100mbps, full_duplex);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_enable_irq(drv_eth_ksz8851_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->enable_irq != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->enable_irq(handle->hw_context);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_disable_irq(drv_eth_ksz8851_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->disable_irq != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->disable_irq(handle->hw_context);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_irq_handler(drv_eth_ksz8851_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->irq_handler != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->irq_handler(handle->hw_context);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_set_mac_addr(drv_eth_ksz8851_t *handle, const uint8_t mac_addr[6])
{
    ASSERT(handle != NULL);
    ASSERT(handle->set_mac_addr != NULL);
    ASSERT(mac_addr != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->set_mac_addr(handle->hw_context, mac_addr);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_get_mac_addr(drv_eth_ksz8851_t *handle, uint8_t mac_addr[6])
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_mac_addr != NULL);
    ASSERT(mac_addr != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->get_mac_addr(handle->hw_context, mac_addr);
}

drv_eth_ksz8851_status_t hw_eth_ksz8851_register_callback(drv_eth_ksz8851_t *handle, 
                                                           drv_eth_ksz8851_cb_type_t type, 
                                                           drv_eth_ksz8851_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(handle->register_callback != NULL);
    
    if (!handle->is_init) {
        return DRV_ETH_KSZ8851_STATUS_ERROR;
    }
    
    return handle->register_callback(handle->hw_context, type, callback);
}