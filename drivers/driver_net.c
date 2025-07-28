#include "driver_net.h"
#include "utils_assert.h"

drv_net_status_t hw_net_init(drv_net_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->init != NULL);
    
    if (handle->is_init) {
        return DRV_NET_STATUS_OK;
    }
    
    drv_net_status_t result = handle->init(handle->hw_context);
    if (result == DRV_NET_STATUS_OK) {
        handle->is_init = true;
        handle->is_started = false;
    }
    
    return result;
}

drv_net_status_t hw_net_deinit(drv_net_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    // Stop network if it's running
    if (handle->is_started) {
        hw_net_stop(handle);
    }
    
    drv_net_status_t result = handle->deinit(handle->hw_context);
    if (result == DRV_NET_STATUS_OK) {
        handle->is_init = false;
        handle->is_started = false;
    }
    
    return result;
}

drv_net_status_t hw_net_start(drv_net_t *handle, const drv_net_config_t *config)
{
    ASSERT(handle != NULL);
    ASSERT(handle->start != NULL);
    ASSERT(config != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    if (handle->is_started) {
        return DRV_NET_STATUS_OK;
    }
    
    drv_net_status_t result = handle->start(handle->hw_context, config);
    if (result == DRV_NET_STATUS_OK) {
        handle->is_started = true;
    }
    
    return result;
}

drv_net_status_t hw_net_stop(drv_net_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->stop != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    if (!handle->is_started) {
        return DRV_NET_STATUS_OK;
    }
    
    drv_net_status_t result = handle->stop(handle->hw_context);
    if (result == DRV_NET_STATUS_OK) {
        handle->is_started = false;
    }
    
    return result;
}

drv_net_status_t hw_net_get_status(drv_net_t *handle, drv_net_status_info_t *status)
{
    ASSERT(handle != NULL);
    ASSERT(handle->get_status != NULL);
    ASSERT(status != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    return handle->get_status(handle->hw_context, status);
}

drv_net_status_t hw_net_wait_for_link(drv_net_t *handle, uint32_t timeout_ms)
{
    ASSERT(handle != NULL);
    ASSERT(handle->wait_for_link != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    return handle->wait_for_link(handle->hw_context, timeout_ms);
}

drv_net_status_t hw_net_wait_for_ip(drv_net_t *handle, uint32_t timeout_ms)
{
    ASSERT(handle != NULL);
    ASSERT(handle->wait_for_ip != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    return handle->wait_for_ip(handle->hw_context, timeout_ms);
}

drv_net_status_t hw_net_print_network_info(drv_net_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->print_network_info != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    return handle->print_network_info(handle->hw_context);
}

drv_net_status_t hw_net_ping(drv_net_t *handle, const char *target_ip, uint32_t timeout_ms)
{
    ASSERT(handle != NULL);
    ASSERT(handle->ping != NULL);
    ASSERT(target_ip != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    return handle->ping(handle->hw_context, target_ip, timeout_ms);
}

drv_net_status_t hw_net_register_callback(drv_net_t *handle, 
                                         drv_net_cb_type_t type, 
                                         drv_net_callback_t callback)
{
    ASSERT(handle != NULL);
    ASSERT(handle->register_callback != NULL);
    
    if (!handle->is_init) {
        return DRV_NET_STATUS_NOT_INITIALIZED;
    }
    
    return handle->register_callback(handle->hw_context, type, callback);
}