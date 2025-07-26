#ifndef _DRIVER_LED_H_
#define _DRIVER_LED_H_

#include <stdbool.h>

typedef enum
{
    DRV_LED_STATUS_OK = 0,    ///< Success
    DRV_LED_STATUS_ERROR = 1, ///< Generic error
} drv_led_status_t;

typedef struct
{
    bool is_init;
    const void *hw_context;
    drv_led_status_t (*init)(const void *hw_context);
    drv_led_status_t (*deinit)(const void *hw_context);
    drv_led_status_t (*on)(const void *hw_context);
    drv_led_status_t (*off)(const void *hw_context);
    drv_led_status_t (*toggle)(const void *hw_context);
} drv_led_t;

#ifdef __cplusplus
extern "C"
{
#endif

    drv_led_status_t hw_led_init(drv_led_t *handle);
    drv_led_status_t hw_led_deinit(drv_led_t *handle);
    drv_led_status_t hw_led_on(drv_led_t *handle);
    drv_led_status_t hw_led_off(drv_led_t *handle);
    drv_led_status_t hw_led_toggle(drv_led_t *handle);

#ifdef __cplusplus
}
#endif

#endif // _DRIVER_LED_H_
