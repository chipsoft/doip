#include "bsp_led.h"
#include "hal_gpio.h"
#include "custom_assert.h"

typedef struct
{
    uint8_t pin_led;
    bool state;
} drv_led_hw_context_t;

static drv_led_hw_context_t drv_led_hw_context_red = {
    .pin_led = GPIO(GPIO_PORTA, 7), // Red LED
    .state = false,
};

static drv_led_hw_context_t drv_led_hw_context_yellow = {
    .pin_led = GPIO(GPIO_PORTB, 7), // Yellow LED
    .state = false,
};

static drv_led_hw_context_t drv_led_hw_context_green = {
    .pin_led = GPIO(GPIO_PORTB, 4), // Green LED
    .state = false,
};

static drv_led_hw_context_t drv_led_pwm_power = {
    .pin_led = GPIO(GPIO_PORTA, 16),
    .state = false,
};

static drv_led_hw_context_t drv_led_lline_to_gnd = {
    .pin_led = GPIO(GPIO_PORTB, 5), // Red LED
    .state = true,
};

static drv_led_hw_context_t drv_led_swcan_power = {
    .pin_led = GPIO(GPIO_PORTA, 27), // Red LED
    .state = false,
};

static drv_led_status_t drv_led_init(const void *hw_context);
static drv_led_status_t drv_led_deinit(const void *hw_context);
static drv_led_status_t drv_led_on(const void *hw_context);
static drv_led_status_t drv_led_off(const void *hw_context);
static drv_led_status_t drv_led_toggle(const void *hw_context);

drv_led_t led_red = {
    .is_init = false,
    .hw_context = &drv_led_hw_context_red,
    .init = drv_led_init,
    .deinit = drv_led_deinit,
    .on = drv_led_on,
    .off = drv_led_off,
    .toggle = drv_led_toggle,
};

drv_led_t led_yellow = {
    .is_init = false,
    .hw_context = &drv_led_hw_context_yellow,
    .init = drv_led_init,
    .deinit = drv_led_deinit,
    .on = drv_led_on,
    .off = drv_led_off,
    .toggle = drv_led_toggle,
};

drv_led_t led_green = {
    .is_init = false,
    .hw_context = &drv_led_hw_context_green,
    .init = drv_led_init,
    .deinit = drv_led_deinit,
    .on = drv_led_on,
    .off = drv_led_off,
    .toggle = drv_led_toggle,
};

drv_led_t pwm_power = {
    .is_init = false,
    .hw_context = &drv_led_pwm_power,
    .init = drv_led_init,
    .deinit = drv_led_deinit,
    .on = drv_led_on,
    .off = drv_led_off,
    .toggle = drv_led_toggle,
};

drv_led_t led_lline_to_gnd = {
    .is_init = false,
    .hw_context = &drv_led_lline_to_gnd,
    .init = drv_led_init,
    .deinit = drv_led_deinit,
    .on = drv_led_on,
    .off = drv_led_off,
    .toggle = drv_led_toggle,
};

drv_led_t led_swcan_power = {
    .is_init = false,
    .hw_context = &drv_led_swcan_power,
    .init = drv_led_init,
    .deinit = drv_led_deinit,
    .on = drv_led_on,
    .off = drv_led_off,
    .toggle = drv_led_toggle,
};

static drv_led_status_t drv_led_init(const void *hw_context)
{
    CUSTOM_ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_set_pin_direction(context->pin_led, GPIO_DIRECTION_OUT);
    gpio_set_pin_function(context->pin_led, GPIO_PIN_FUNCTION_OFF);
    gpio_set_pin_pull_mode(context->pin_led, GPIO_PULL_OFF);
    gpio_set_pin_level(context->pin_led, context->state);
    return DRV_LED_STATUS_OK;
}

static drv_led_status_t drv_led_deinit(const void *hw_context)
{
    CUSTOM_ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_set_pin_direction(context->pin_led, GPIO_DIRECTION_OFF);
    gpio_set_pin_function(context->pin_led, GPIO_PIN_FUNCTION_OFF);
    return DRV_LED_STATUS_OK;
}

static drv_led_status_t drv_led_on(const void *hw_context)
{
    CUSTOM_ASSERT(hw_context != NULL);
}

static drv_led_status_t drv_led_off(const void *hw_context)
{
    CUSTOM_ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_set_pin_level(context->pin_led, false);
    return DRV_LED_STATUS_OK;
}

static drv_led_status_t drv_led_toggle(const void *hw_context)
{
    CUSTOM_ASSERT(hw_context != NULL);
    const drv_led_hw_context_t *context = (const drv_led_hw_context_t *)hw_context;
    gpio_toggle_pin_level(context->pin_led);
    return DRV_LED_STATUS_OK;
}
