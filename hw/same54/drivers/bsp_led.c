#include "bsp_led.h"
#include "hal_gpio.h"
#include "utils_assert.h"
#include "atmel_start_pins.h"

typedef struct
{
    uint8_t pin_led;
    bool state;
} drv_led_hw_context_t;

static drv_led_hw_context_t drv_led_hw_context_yellow = {
    .pin_led = LED_0, // Yellow LED on PC18
    .state = false,
};

static drv_led_status_t drv_led_init(const void *hw_context);
static drv_led_status_t drv_led_deinit(const void *hw_context);
static drv_led_status_t drv_led_on(const void *hw_context);
static drv_led_status_t drv_led_off(const void *hw_context);
static drv_led_status_t drv_led_toggle(const void *hw_context);

drv_led_t led_yellow = {
    .is_init = false,
    .hw_context = &drv_led_hw_context_yellow,
    .init = drv_led_init,
    .deinit = drv_led_deinit,
    .on = drv_led_on,
    .off = drv_led_off,
    .toggle = drv_led_toggle,
};

static drv_led_status_t drv_led_init(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_set_pin_direction(context->pin_led, GPIO_DIRECTION_OUT);
    gpio_set_pin_function(context->pin_led, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_pull_mode(context->pin_led, GPIO_PULL_OFF);
    gpio_set_pin_level(context->pin_led, context->state);
    return DRV_LED_STATUS_OK;
}

static drv_led_status_t drv_led_deinit(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_set_pin_direction(context->pin_led, GPIO_DIRECTION_OFF);
    gpio_set_pin_function(context->pin_led, GPIO_PIN_FUNCTION_OFF);
    return DRV_LED_STATUS_OK;
}

static drv_led_status_t drv_led_on(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_set_pin_level(context->pin_led, true);
    return DRV_LED_STATUS_OK;
}

static drv_led_status_t drv_led_off(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_set_pin_level(context->pin_led, false);
    return DRV_LED_STATUS_OK;
}

static drv_led_status_t drv_led_toggle(const void *hw_context)
{
    ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_toggle_pin_level(context->pin_led);
    return DRV_LED_STATUS_OK;
}