/**
 * \file
 *
 * \brief LwIP socket api application implementation
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

#include "lwip_socket_api.h"
#include "printf.h"
#include <peripheral_clk_config.h>
#include <utils.h>
#include <hal_init.h>
#include <hal_gpio.h>
#include <hal_usart_sync.h>
#include <hal_mac_async.h>
#include "atmel_start_pins.h"
#include "bsp_led.h"
#include "bsp_ethernet.h"
#include "eth_ipstack_main.h"

/* RTT printf integration */
extern void rtt_printf_init(void);

/* Peripheral descriptors */
struct usart_sync_descriptor TARGET_IO;
struct mac_async_descriptor COMMUNICATION_IO;

/* Peripheral initialization functions */
static void TARGET_IO_PORT_init(void)
{
	gpio_set_pin_function(PB25, PINMUX_PB25D_SERCOM2_PAD0);
	gpio_set_pin_function(PB24, PINMUX_PB24D_SERCOM2_PAD1);
}

static void TARGET_IO_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM2_GCLK_ID_CORE, CONF_GCLK_SERCOM2_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM2_GCLK_ID_SLOW, CONF_GCLK_SERCOM2_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_mclk_set_APBBMASK_SERCOM2_bit(MCLK);
}

static void TARGET_IO_init(void)
{
	TARGET_IO_CLOCK_init();
	usart_sync_init(&TARGET_IO, SERCOM2, (void *)NULL);
	TARGET_IO_PORT_init();
}

static void COMMUNICATION_IO_PORT_init(void)
{
	gpio_set_pin_function(PC11, PINMUX_PC11L_GMAC_GMDC);
	gpio_set_pin_function(PC12, PINMUX_PC12L_GMAC_GMDIO);
	gpio_set_pin_function(PA13, PINMUX_PA13L_GMAC_GRX0);
	gpio_set_pin_function(PA12, PINMUX_PA12L_GMAC_GRX1);
	gpio_set_pin_function(PC20, PINMUX_PC20L_GMAC_GRXDV);
	gpio_set_pin_function(PA15, PINMUX_PA15L_GMAC_GRXER);
	gpio_set_pin_function(PA18, PINMUX_PA18L_GMAC_GTX0);
	gpio_set_pin_function(PA19, PINMUX_PA19L_GMAC_GTX1);
	gpio_set_pin_function(PA14, PINMUX_PA14L_GMAC_GTXCK);
	gpio_set_pin_function(PA17, PINMUX_PA17L_GMAC_GTXEN);
}

static void COMMUNICATION_IO_CLOCK_init(void)
{
	hri_mclk_set_AHBMASK_GMAC_bit(MCLK);
	hri_mclk_set_APBCMASK_GMAC_bit(MCLK);
}

static void COMMUNICATION_IO_init(void)
{
	COMMUNICATION_IO_CLOCK_init();
	mac_async_init(&COMMUNICATION_IO, GMAC);
	COMMUNICATION_IO_PORT_init();
}

static void system_init(void)
{
	init_mcu();
	
	// Initialize LED using universal driver
	hw_led_init(&led_yellow);
	
	TARGET_IO_init();
	COMMUNICATION_IO_init();
	
	// Initialize Ethernet using universal driver
	hw_eth_init(&eth_communication);
}

/* define to avoid compilation warning */
// #define LWIP_TIMEVAL_PRIVATE 0

void print_ipaddress(void)
{
	static char tmp_buff[16];
	printf("IP_ADDR    : %s\r\n",
	       ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.ip_addr), tmp_buff, 16));
	printf("NET_MASK   : %s\r\n",
	       ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.netmask), tmp_buff, 16));
	printf("GATEWAY_IP : %s\r\n", ipaddr_ntoa_r((const ip_addr_t *)&(TCPIP_STACK_INTERFACE_0_desc.gw), tmp_buff, 16));
}

/*
 * NOTE:
 * If compilation results in error: "redefinition of 'struct timeval'",
 * perform below step to make compilation successful.
 *
 * Navigate to sockets.h file in $PROJECT_LOCAION$\lwip\lwip-1.4.0\src\include\lwip &
 * change the MACRO LWIP_TIMEVAL_PRIVATE like below
 *
 * #define LWIP_TIMEVAL_PRIVATE		1
 *				to
 * #define LWIP_TIMEVAL_PRIVATE		0
 *
 */

int main(void)
{
	/* Initialize system and peripherals */
	system_init();
	
	/* Initialize ethernet PHY - now handled by universal driver in system_init */
	/* ethernet_phys_init(); - REMOVED: handled by hw_eth_init() */

	/* Initialize SEGGER RTT for debug output */
	rtt_printf_init();

	/*Handles Socket API */
	printf("\r\nSocket API implementation\r\n");
	basic_socket();

	while (1)
		;

	return 0;
}
