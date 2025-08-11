/*
 * Code generated from Atmel Start.
 *
 * This file will be overwritten when reconfiguring your Atmel Start project.
 * Please copy examples or other code you want to keep to a separate file
 * to avoid losing it when reconfiguring.
 */

#include "driver_init.h"
#include <peripheral_clk_config.h>
#include <utils.h>
#include <hal_init.h>

struct spi_m_sync_descriptor WINC_SPI;

struct usart_sync_descriptor EDBG_UART;

void WINC_IRQ_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, EIC_GCLK_ID, CONF_GCLK_EIC_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_mclk_set_APBAMASK_EIC_bit(MCLK);

	// Set pin direction to input
	gpio_set_pin_direction(CONF_WINC_EXT_INT_PIN, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(CONF_WINC_EXT_INT_PIN,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(CONF_WINC_EXT_INT_PIN, PINMUX_PB07A_EIC_EXTINT7);

	ext_irq_init();
}

void EDBG_UART_PORT_init(void)
{

	gpio_set_pin_function(EDBG_UART_TX, PINMUX_PB25D_SERCOM2_PAD0);

	gpio_set_pin_function(EDBG_UART_RX, PINMUX_PB24D_SERCOM2_PAD1);
}

void EDBG_UART_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM2_GCLK_ID_CORE, CONF_GCLK_SERCOM2_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM2_GCLK_ID_SLOW, CONF_GCLK_SERCOM2_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBBMASK_SERCOM2_bit(MCLK);
}

void EDBG_UART_init(void)
{
	EDBG_UART_CLOCK_init();
	usart_sync_init(&EDBG_UART, SERCOM2, (void *)NULL);
	EDBG_UART_PORT_init();
}

void WINC_SPI_PORT_init(void)
{

	gpio_set_pin_level(CONF_WINC_SPI_MOSI,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(CONF_WINC_SPI_MOSI, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CONF_WINC_SPI_MOSI, PINMUX_PB27D_SERCOM4_PAD0);

	gpio_set_pin_level(CONF_WINC_SPI_SCK,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(CONF_WINC_SPI_SCK, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CONF_WINC_SPI_SCK, PINMUX_PB26D_SERCOM4_PAD1);

	// Set pin direction to input
	gpio_set_pin_direction(CONF_WINC_SPI_MISO, GPIO_DIRECTION_IN);

	gpio_set_pin_pull_mode(CONF_WINC_SPI_MISO,
	                       // <y> Pull configuration
	                       // <id> pad_pull_config
	                       // <GPIO_PULL_OFF"> Off
	                       // <GPIO_PULL_UP"> Pull-up
	                       // <GPIO_PULL_DOWN"> Pull-down
	                       GPIO_PULL_OFF);

	gpio_set_pin_function(CONF_WINC_SPI_MISO, PINMUX_PB29D_SERCOM4_PAD3);
}

void WINC_SPI_CLOCK_init(void)
{
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_CORE, CONF_GCLK_SERCOM4_CORE_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));
	hri_gclk_write_PCHCTRL_reg(GCLK, SERCOM4_GCLK_ID_SLOW, CONF_GCLK_SERCOM4_SLOW_SRC | (1 << GCLK_PCHCTRL_CHEN_Pos));

	hri_mclk_set_APBDMASK_SERCOM4_bit(MCLK);
}

void WINC_SPI_init(void)
{
	WINC_SPI_CLOCK_init();
	spi_m_sync_init(&WINC_SPI, SERCOM4);
	WINC_SPI_PORT_init();
}

void delay_driver_init(void)
{
	delay_init(SysTick);
}

void system_init(void)
{
	init_mcu();

	// GPIO on PA06

	gpio_set_pin_level(RESET_PIN,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(RESET_PIN, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(RESET_PIN, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PA27

	gpio_set_pin_level(CE_PIN,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   false);

	// Set pin direction to output
	gpio_set_pin_direction(CE_PIN, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CE_PIN, GPIO_PIN_FUNCTION_OFF);

	// GPIO on PB28

	gpio_set_pin_level(CS_PIN,
	                   // <y> Initial level
	                   // <id> pad_initial_level
	                   // <false"> Low
	                   // <true"> High
	                   true);

	// Set pin direction to output
	gpio_set_pin_direction(CS_PIN, GPIO_DIRECTION_OUT);

	gpio_set_pin_function(CS_PIN, GPIO_PIN_FUNCTION_OFF);

	WINC_IRQ_init();

	EDBG_UART_init();

	WINC_SPI_init();

	delay_driver_init();
}
