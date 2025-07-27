/**
 * webserver_tasks.h
 *
 * Copyright (c) 2019 Microchip Technology Inc. and its subsidiaries.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Subject to your compliance with these terms, you may use Microchip
 * software and any derivatives exclusively with Microchip products.
 * It is your responsibility to comply with third party license terms applicable
 * to your use of third party software (including open source software) that
 * may accompany Microchip software.
 *
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE,
 * INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
 * AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP BE
 * LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL
 * LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
 * SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE
 * POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT
 * ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY
 * RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
 * THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 * \asf_license_stop
 *
 */

#ifndef WEBSERVER_TASKS_H_
#define WEBSERVER_TASKS_H_

#include "lwip/sys.h"
#include "hpl_gmac_config.h"
#include "lwip_macif_config.h"
#include "arch/sys_arch.h"
#include "FreeRTOS.h"
#include "task.h"

#define TASK_LED_STACK_SIZE (512 / sizeof(portSTACK_TYPE))
#define TASK_LED_TASK_PRIORITY (tskIDLE_PRIORITY + 1)

#define TASK_ETHERNETBASIC_STACK_SIZE (1024 / sizeof(portSTACK_TYPE))
#define TASK_ETHERNETBASIC_STACK_PRIORITY (tskIDLE_PRIORITY + 2)

#define netifINTERFACE_TASK_STACK_SIZE 512
#define netifINTERFACE_TASK_PRIORITY (tskIDLE_PRIORITY + 2)

/** Number of buffer for RX */
#define GMAC_RX_BUFFERS 5

/** Number of buffer for TX */
#define GMAC_TX_BUFFERS 3

#define SYS_THREAD_MAX 8

#define BLINK_NORMAL 500

#include <hal_mac_async.h>

void task_led_create();

#endif /* WEBSERVER_TASKS_H_ */
