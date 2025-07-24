/**
 * \file
 *
 * \brief SEGGER RTT integration with printf library
 *
 * This file provides the bridge between the embedded printf library and
 * SEGGER RTT for real-time debugging output.
 */

#include "SEGGER_RTT.h"
#include "printf.h"

/**
 * \brief Initialize SEGGER RTT system
 * 
 * This function must be called before using any printf functions.
 * It initializes the RTT buffers for communication with the debugger.
 */
void rtt_printf_init(void)
{
    SEGGER_RTT_Init();
}

/**
 * \brief Output a character via SEGGER RTT
 * 
 * This function is called by the printf library to output each character.
 * It sends the character to RTT buffer 0 (default terminal buffer).
 * 
 * \param character Character to output
 */
void _putchar(char character)
{
    SEGGER_RTT_PutChar(0, character);
}