#ifndef _KSZ8851SNL_CONFIG_H_
#define _KSZ8851SNL_CONFIG_H_

#include <hal_gpio.h>

#define KSZ8851SNL_RESET_PIN    GPIO(GPIO_PORTA, 6)   // PA6 - Reset pin
#define KSZ8851SNL_CE_PIN       GPIO(GPIO_PORTA, 27)  // PA27 - Chip Enable pin  
#define KSZ8851SNL_INT_PIN      GPIO(GPIO_PORTB, 7)   // PB7 - Interrupt pin
#define KSZ8851SNL_CS_PIN       GPIO(GPIO_PORTB, 28)  // PB28 - SPI Chip Select
#define KSZ8851SNL_SCK_PIN      GPIO(GPIO_PORTB, 26)  // PB26 - SPI Clock
#define KSZ8851SNL_MOSI_PIN     GPIO(GPIO_PORTB, 27)  // PB27 - SPI MOSI
#define KSZ8851SNL_MISO_PIN     GPIO(GPIO_PORTB, 29)  // PB29 - SPI MISO

// SPI Configuration
#define KSZ8851SNL_SPI_CLOCK_SPEED      5000000UL     // 5 MHz (reduced from 25MHz - working implementations use 5-10MHz max)
#define KSZ8851SNL_SPI_CLOCK_POLARITY   0             // Clock idle low
#define KSZ8851SNL_SPI_CLOCK_PHASE      1             // Sample on rising edge
#define KSZ8851SNL_SPI_BITS_PER_TRANSFER 8            // 8-bit transfers

// Timing Configuration
#define KSZ8851SNL_RESET_DELAY_MS       10            // Reset delay in milliseconds
#define KSZ8851SNL_POWERUP_DELAY_MS     100           // Power-up delay in milliseconds
#define KSZ8851SNL_SPI_CS_SETUP_NS      50            // CS setup time in nanoseconds
#define KSZ8851SNL_SPI_CS_HOLD_NS       50            // CS hold time in nanoseconds

// Register Definitions (from existing KSZ8851SNL register definitions)
#define KSZ8851SNL_REG_CHIP_ID          0xC0          // CIDER - Chip ID register
#define KSZ8851SNL_REG_MAC_ADDR_0       0x10          // MARL - MAC address register 0 (low)
#define KSZ8851SNL_REG_MAC_ADDR_2       0x12          // MARM - MAC address register 2 (middle)
#define KSZ8851SNL_REG_MAC_ADDR_4       0x14          // MARH - MAC address register 4 (high)

// Expected Chip ID Values (matching KSZ8851SNL register definitions)
#define KSZ8851SNL_CHIP_ID_EXPECTED     0x8870        // KS8851-16/32MQL chip ID (matches CHIP_ID_8851_16)
#define KSZ8851SNL_CHIP_ID_MASK         0xFFF0        // Family ID and chip ID mask (exclude revision bits)
#define KSZ8851SNL_FAMILY_ID            0x8870        // Family ID for validation  
#define KSZ8851SNL_REVISION_MASK        0x000F        // Revision mask (lower 4 bits)

// Default MAC Address
#define KSZ8851SNL_DEFAULT_MAC_ADDR     {0x00, 0x04, 0x25, 0x1C, 0xA0, 0x02}

#endif // _KSZ8851SNL_CONFIG_H_