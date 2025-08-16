/**
 * \file
 *
 * \brief Simple KSZ8851SNL Test Application - No LwIP
 *
 * This is a minimal test application that directly tests KSZ8851SNL functionality
 * without any LwIP dependencies to isolate and verify hardware operation.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "printf.h"
#include "bsp_led.h"
#include "ksz_minimal_test.h"
#include <peripheral_clk_config.h>
#include <utils.h>
#include <hal_init.h>
#include <hal_gpio.h>

/* RTT printf integration */
extern void rtt_printf_init(void);

/**
 * Simple LED blink task to show system is alive
 */
static void led_blink_task(void *pvParameters)
{
    (void)pvParameters;
    
    printf("[LED] LED blink task started\r\n");
    
    while (1) {
        hw_led_toggle(&led_yellow);
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second blink
    }
}

/**
 * Stack overflow hook function for FreeRTOS
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("\r\n*** CRITICAL ERROR: Stack overflow detected! ***\r\n");
    printf("Task: %s (handle: %p)\r\n", pcTaskName ? pcTaskName : "Unknown", (void*)xTask);
    printf("System halted for safety.\r\n");
    
    /* Disable interrupts and halt system */
    taskDISABLE_INTERRUPTS();
    for (;;);
}

/**
 * Heap protection canary generation for FreeRTOS heap_4
 */
uint32_t vApplicationGetRandomHeapCanary(void)
{
    static uint32_t seed = 0x55AA55AA;
    
    /* Linear congruential generator for simple randomness */
    seed = (seed * 1664525UL + 1013904223UL);
    
    /* XOR with system tick for additional entropy */
    return seed ^ xTaskGetTickCount();
}

/**
 * Main application entry point
 */
int main(void)
{
    /* Initialize system and peripherals */
    init_mcu();
    
    /* Initialize LED using universal driver */
    hw_led_init(&led_yellow);
    
    /* Initialize SEGGER RTT for debug output */
    rtt_printf_init();
    
    printf("\r\n=============================================\r\n");
    printf("KSZ8851SNL MINIMAL Hardware Test\r\n");
    printf("No LwIP, No ECU, No DOIP - Just KSZ hardware\r\n");
    printf("Register verification + simple packets\r\n");
    printf("=============================================\r\n");
    
    /* Create LED blink task to show system is alive */
    if (xTaskCreate(led_blink_task,
                    "LED_BLINK",
                    256,  // Stack size
                    NULL,
                    (tskIDLE_PRIORITY + 1),  // Low priority
                    NULL) != pdPASS) {
        printf("Failed to create LED blink task\r\n");
        while (1);
    }
    
    /* Start the minimal KSZ test */
    ksz_minimal_test_start();
    
    /* Start FreeRTOS scheduler */
    printf("Starting FreeRTOS scheduler...\r\n");
    vTaskStartScheduler();
    
    /* Should never reach here */
    return 0;
}