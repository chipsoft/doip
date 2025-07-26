#include "driver_led.h"
#include "utils_assert.h"

drv_led_status_t hw_led_init(drv_led_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->init != NULL);
    if (handle->is_init)
    {
        return DRV_LED_STATUS_OK;
    }
    drv_led_status_t res = handle->init(handle->hw_context);
    if (res == DRV_LED_STATUS_OK)
    {
        handle->is_init = true;
    }
    return res;
}

drv_led_status_t hw_led_deinit(drv_led_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->deinit != NULL);
    if (!handle->is_init)
    {
        return DRV_LED_STATUS_OK;
    }
    return handle->deinit(handle->hw_context);
}

drv_led_status_t hw_led_on(drv_led_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->on != NULL);
    if (!handle->is_init)
    {
        return DRV_LED_STATUS_ERROR;
    }
    return handle->on(handle->hw_context);
}

drv_led_status_t hw_led_off(drv_led_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->off != NULL);
    if (!handle->is_init)
    {
        return DRV_LED_STATUS_ERROR;
    }
    return handle->off(handle->hw_context);
}

drv_led_status_t hw_led_toggle(drv_led_t *handle)
{
    ASSERT(handle != NULL);
    ASSERT(handle->toggle != NULL);
    if (!handle->is_init)
    {
        return DRV_LED_STATUS_ERROR;
    }
    return handle->toggle(handle->hw_context);
}

// #include "CryptoLib_typedef_pb.h"
// #include "CryptoLib_mapping_pb.h"
// #include "CryptoLib_cf_pb.h"
// #include "CryptoLib_Headers_pb.h"

// void CRYPTOGRAPHY_0_example(void)
// {
//     PPUKCL_PARAM pvPUKCLParam;
//     PUKCL_PARAM PUKCLParam;

//     // Init
//     memset(&PUKCLParam, 0, sizeof(PUKCL_PARAM));

//     pvPUKCLParam = &PUKCLParam;
//     vPUKCL_Process(SelfTest, pvPUKCLParam);

//     while (PUKCL(u2Status) != PUKCL_OK)
//         ;

//     while (pvPUKCLParam->P.PUKCL_SelfTest.u4Version != PUKCL_VERSION)
//         ;

//     while (pvPUKCLParam->P.PUKCL_SelfTest.u4CheckNum1 != 0x6E70DDD2)
//         ;

//     while (pvPUKCLParam->P.PUKCL_SelfTest.u4CheckNum2 != 0x25C8D64F)
//         ;
// }

// /**
//  * \brief PUKCC initialization function
//  *
//  * Enables PUKCC peripheral, clocks and initializes PUKCC driver
//  */
// void CRYPTOGRAPHY_0_init(void)
// {
//     hri_mclk_set_AHBMASK_PUKCC_bit(MCLK);
// }
