/*
 * FreeRTOS Kernel V11.1.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/******************************************************************************/
/* Compiler and platform specific includes ***********************************/
/******************************************************************************/

#if defined(__GNUC__) || defined(__ICCARM__)
/* Important: put #includes here unless they are also meant for the assembler. */
#include <stdint.h>
void assert_triggered(const char *file, uint32_t line);
#endif

#if defined(__ICCARM__)
#include <cmsis_compiler.h>
#endif

#include <peripheral_clk_config.h>

/******************************************************************************/
/* Hardware description related definitions. **********************************/
/******************************************************************************/

/* configCPU_CLOCK_HZ must be set to the frequency of the clock that drives 
 * the peripheral used to generate the kernels periodic tick interrupt. */
#ifndef configCPU_CLOCK_HZ
#define configCPU_CLOCK_HZ    (CONF_CPU_FREQUENCY)
#endif

/******************************************************************************/
/* Scheduling behaviour related definitions. **********************************/
/******************************************************************************/

/* configTICK_RATE_HZ sets frequency of the tick interrupt in Hz */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                         ((TickType_t)1000)
#endif

/* Set configUSE_PREEMPTION to 1 to use pre-emptive scheduling */
#ifndef configUSE_PREEMPTION
#define configUSE_PREEMPTION                       1
#endif

/* Set configUSE_TIME_SLICING to 1 to have the scheduler switch between Ready
 * state tasks of equal priority on every tick interrupt */
#ifndef configUSE_TIME_SLICING
#define configUSE_TIME_SLICING                     1
#endif

/* Set configUSE_PORT_OPTIMISED_TASK_SELECTION for Cortex-M4F optimization */
#ifndef configUSE_PORT_OPTIMISED_TASK_SELECTION
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    0
#endif

/* Set configUSE_TICKLESS_IDLE to 1 to use the low power tickless mode */
#ifndef configUSE_TICKLESS_IDLE
#define configUSE_TICKLESS_IDLE                    0
#endif

/* configMAX_PRIORITIES Sets the number of available task priorities */
#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES                       5
#endif

/* configMINIMAL_STACK_SIZE defines the size of the stack used by the Idle task */
#ifndef configMINIMAL_STACK_SIZE
#define configMINIMAL_STACK_SIZE                   64
#endif

/* configMAX_TASK_NAME_LEN sets the maximum length of a task's name */
#ifndef configMAX_TASK_NAME_LEN
#define configMAX_TASK_NAME_LEN                    8
#endif

/* Configure tick type for 32-bit systems */
#ifndef configTICK_TYPE_WIDTH_IN_BITS
#define configTICK_TYPE_WIDTH_IN_BITS              TICK_TYPE_WIDTH_32_BITS
#endif

/* Set configIDLE_SHOULD_YIELD to 1 to have the Idle task yield */
#ifndef configIDLE_SHOULD_YIELD
#define configIDLE_SHOULD_YIELD                    1
#endif

/* configTASK_NOTIFICATION_ARRAY_ENTRIES sets the number of task notifications */
#ifndef configTASK_NOTIFICATION_ARRAY_ENTRIES
#define configTASK_NOTIFICATION_ARRAY_ENTRIES      1
#endif

/* configQUEUE_REGISTRY_SIZE for kernel aware debugger */
#ifndef configQUEUE_REGISTRY_SIZE
#define configQUEUE_REGISTRY_SIZE                  0
#endif

/* Set configENABLE_BACKWARD_COMPATIBILITY to 1 for V8.2.3 compatibility */
#ifndef configENABLE_BACKWARD_COMPATIBILITY
#define configENABLE_BACKWARD_COMPATIBILITY        1
#endif

/* configNUM_THREAD_LOCAL_STORAGE_POINTERS for thread local storage */
#ifndef configNUM_THREAD_LOCAL_STORAGE_POINTERS
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS    0
#endif

/* configUSE_MINI_LIST_ITEM optimization */
#ifndef configUSE_MINI_LIST_ITEM
#define configUSE_MINI_LIST_ITEM                   1
#endif

/* configSTACK_DEPTH_TYPE sets the type used for stack size parameters */
#ifndef configSTACK_DEPTH_TYPE
#define configSTACK_DEPTH_TYPE                     uint16_t
#endif

/* configMESSAGE_BUFFER_LENGTH_TYPE for message buffers */
#ifndef configMESSAGE_BUFFER_LENGTH_TYPE
#define configMESSAGE_BUFFER_LENGTH_TYPE           size_t
#endif

/******************************************************************************/
/* Software timer related definitions. ****************************************/
/******************************************************************************/

/* Set configUSE_TIMERS to 1 to include software timer functionality */
#ifndef configUSE_TIMERS
#define configUSE_TIMERS                1
#endif

/* configTIMER_TASK_PRIORITY sets the priority used by the timer task */
#ifndef configTIMER_TASK_PRIORITY
#define configTIMER_TASK_PRIORITY       2
#endif

/* configTIMER_TASK_STACK_DEPTH sets the size of the stack allocated to the timer task */
#ifndef configTIMER_TASK_STACK_DEPTH
#define configTIMER_TASK_STACK_DEPTH    256
#endif

/* configTIMER_QUEUE_LENGTH sets the length of the timer command queue */
#ifndef configTIMER_QUEUE_LENGTH
#define configTIMER_QUEUE_LENGTH        2
#endif

/******************************************************************************/
/* Event Group related definitions. *******************************************/
/******************************************************************************/

/* Set configUSE_EVENT_GROUPS to 1 to include event group functionality */
#ifndef configUSE_EVENT_GROUPS
#define configUSE_EVENT_GROUPS    1
#endif

/******************************************************************************/
/* Stream Buffer related definitions. *****************************************/
/******************************************************************************/

/* Set configUSE_STREAM_BUFFERS to 1 to include stream buffer functionality */
#ifndef configUSE_STREAM_BUFFERS
#define configUSE_STREAM_BUFFERS    1
#endif

/******************************************************************************/
/* Memory allocation related definitions. *************************************/
/******************************************************************************/

/* Set configSUPPORT_STATIC_ALLOCATION to include static allocation */
#ifndef configSUPPORT_STATIC_ALLOCATION
#define configSUPPORT_STATIC_ALLOCATION              0
#endif

/* Set configSUPPORT_DYNAMIC_ALLOCATION to include dynamic allocation */
#ifndef configSUPPORT_DYNAMIC_ALLOCATION
#define configSUPPORT_DYNAMIC_ALLOCATION             1
#endif

/* configTOTAL_HEAP_SIZE sets the total size of the FreeRTOS heap - preserve 42KB */
#ifndef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE                        42000
#endif

/* Set configAPPLICATION_ALLOCATED_HEAP to 0 to have the linker allocate heap */
#ifndef configAPPLICATION_ALLOCATED_HEAP
#define configAPPLICATION_ALLOCATED_HEAP             0
#endif

/* Set configSTACK_ALLOCATION_FROM_SEPARATE_HEAP to 0 for standard heap */
#ifndef configSTACK_ALLOCATION_FROM_SEPARATE_HEAP
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP    0
#endif

/* Set configENABLE_HEAP_PROTECTOR to 0 for compatibility */
#ifndef configENABLE_HEAP_PROTECTOR
#define configENABLE_HEAP_PROTECTOR                  0
#endif

/******************************************************************************/
/* Interrupt nesting behaviour configuration. *********************************/
/******************************************************************************/

/* Interrupt priority configuration for SAME54 Cortex-M4F */
#define configPRIO_BITS 3

/* configKERNEL_INTERRUPT_PRIORITY sets the priority of the tick interrupt */
#ifndef configKERNEL_INTERRUPT_PRIORITY
#define configKERNEL_INTERRUPT_PRIORITY          (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#endif

/* configMAX_SYSCALL_INTERRUPT_PRIORITY sets the highest interrupt priority that can call FreeRTOS APIs */
#ifndef configMAX_SYSCALL_INTERRUPT_PRIORITY
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#endif

/* The lowest interrupt priority that can be used in a call to a "set priority" function */
#ifndef configLIBRARY_LOWEST_INTERRUPT_PRIORITY
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0x07
#endif

/* The highest interrupt priority that can be used by any interrupt service routine that makes calls to interrupt safe FreeRTOS API functions */
#ifndef configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 4
#endif

/******************************************************************************/
/* Hook and callback function related definitions. ****************************/
/******************************************************************************/

/* Set hook function usage - preserve original settings */
#ifndef configUSE_IDLE_HOOK
#define configUSE_IDLE_HOOK                   0
#endif

#ifndef configUSE_TICK_HOOK
#define configUSE_TICK_HOOK                   0
#endif

#ifndef configUSE_MALLOC_FAILED_HOOK
#define configUSE_MALLOC_FAILED_HOOK          0
#endif

#ifndef configUSE_DAEMON_TASK_STARTUP_HOOK
#define configUSE_DAEMON_TASK_STARTUP_HOOK    0
#endif

/* Set configUSE_SB_COMPLETED_CALLBACK for stream buffer callbacks */
#ifndef configUSE_SB_COMPLETED_CALLBACK
#define configUSE_SB_COMPLETED_CALLBACK       0
#endif

/* Set configCHECK_FOR_STACK_OVERFLOW to 0 - preserve original setting */
#ifndef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW        0
#endif

/******************************************************************************/
/* Run time and task stats gathering related definitions. *********************/
/******************************************************************************/

/* Set configGENERATE_RUN_TIME_STATS to collect processing time data */
#ifndef configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS           0
#endif

/* Set configUSE_TRACE_FACILITY for trace and visualisation functions */
#ifndef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY                1
#endif

/* Set configUSE_STATS_FORMATTING_FUNCTIONS for vTaskList() and vTaskGetRunTimeStats() */
#ifndef configUSE_STATS_FORMATTING_FUNCTIONS
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#endif

/******************************************************************************/
/* Co-routine related definitions. ********************************************/
/******************************************************************************/

/* Set configUSE_CO_ROUTINES to include co-routine functionality */
#ifndef configUSE_CO_ROUTINES
#define configUSE_CO_ROUTINES              0
#endif

/* configMAX_CO_ROUTINE_PRIORITIES defines the number of co-routine priorities */
#ifndef configMAX_CO_ROUTINE_PRIORITIES
#define configMAX_CO_ROUTINE_PRIORITIES    2
#endif

/******************************************************************************/
/* Definitions that include or exclude functionality. *************************/
/******************************************************************************/

/* Set the following configUSE_* constants to 1 to include the named feature */
#ifndef configUSE_TASK_NOTIFICATIONS
#define configUSE_TASK_NOTIFICATIONS           1
#endif

#ifndef configUSE_MUTEXES
#define configUSE_MUTEXES                      1
#endif

#ifndef configUSE_RECURSIVE_MUTEXES
#define configUSE_RECURSIVE_MUTEXES            1
#endif

#ifndef configUSE_COUNTING_SEMAPHORES
#define configUSE_COUNTING_SEMAPHORES          1
#endif

#ifndef configUSE_QUEUE_SETS
#define configUSE_QUEUE_SETS                   1
#endif

#ifndef configUSE_APPLICATION_TASK_TAG
#define configUSE_APPLICATION_TASK_TAG         0
#endif

/* USE_POSIX_ERRNO enables per-task errno */
#ifndef configUSE_POSIX_ERRNO
#define configUSE_POSIX_ERRNO                  0
#endif

/******************************************************************************/
/* INCLUDE_* constants to include API functions ******************************/
/******************************************************************************/

/* Set the following INCLUDE_* constants to 1 to include the named API function */
#ifndef INCLUDE_vTaskPrioritySet
#define INCLUDE_vTaskPrioritySet               1
#endif

#ifndef INCLUDE_uxTaskPriorityGet
#define INCLUDE_uxTaskPriorityGet              1
#endif

#ifndef INCLUDE_vTaskDelete
#define INCLUDE_vTaskDelete                    1
#endif

#ifndef INCLUDE_vTaskSuspend
#define INCLUDE_vTaskSuspend                   1
#endif

#ifndef INCLUDE_vTaskDelayUntil
#define INCLUDE_vTaskDelayUntil                0
#endif

#ifndef INCLUDE_vTaskDelay
#define INCLUDE_vTaskDelay                     1
#endif

#ifndef INCLUDE_xTaskGetSchedulerState
#define INCLUDE_xTaskGetSchedulerState         0
#endif

#ifndef INCLUDE_xTaskGetCurrentTaskHandle
#define INCLUDE_xTaskGetCurrentTaskHandle      1
#endif

#ifndef INCLUDE_uxTaskGetStackHighWaterMark
#define INCLUDE_uxTaskGetStackHighWaterMark    0
#endif

#ifndef INCLUDE_xTaskGetIdleTaskHandle
#define INCLUDE_xTaskGetIdleTaskHandle         0
#endif

#ifndef INCLUDE_xTimerGetTimerDaemonTaskHandle
#define INCLUDE_xTimerGetTimerDaemonTaskHandle 0
#endif

#ifndef INCLUDE_pcTaskGetTaskName
#define INCLUDE_pcTaskGetTaskName              0
#endif

#ifndef INCLUDE_eTaskGetState
#define INCLUDE_eTaskGetState                  0
#endif

#ifndef INCLUDE_vTaskCleanUpResources
#define INCLUDE_vTaskCleanUpResources          0
#endif

#ifndef INCLUDE_xTimerPendFunctionCall
#define INCLUDE_xTimerPendFunctionCall         0
#endif

#ifndef INCLUDE_xResumeFromISR
#define INCLUDE_xResumeFromISR                 1
#endif

/******************************************************************************/
/* Debugging assistance. ******************************************************/
/******************************************************************************/

/* configASSERT() has the same semantics as the standard C assert() */
#define configASSERT(x)                                                        \
	if ((x) == 0) {                                                            \
		taskDISABLE_INTERRUPTS();                                              \
		for (;;)                                                               \
			;                                                                  \
	}

/******************************************************************************/
/* FreeRTOS port interrupt handlers mapped to CMSIS standard names. ***********/
/******************************************************************************/

/* Definitions that map the FreeRTOS port interrupt handlers to their CMSIS standard names */
#define vPortSVCHandler SVCall_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/******************************************************************************/
/* Run time stats configuration. **********************************************/
/******************************************************************************/

/* Used when configGENERATE_RUN_TIME_STATS is 1 */
#if configGENERATE_RUN_TIME_STATS
extern void     vConfigureTimerForRunTimeStats(void);
extern uint32_t vGetRunTimeCounterValue(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() vConfigureTimerForRunTimeStats()
#define portGET_RUN_TIME_COUNTER_VALUE() vGetRunTimeCounterValue()
#endif

#endif /* FREERTOS_CONFIG_H */