#ifndef _BSP_SPI_H_
#define _BSP_SPI_H_

#include "driver_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

extern drv_spi_t spi_4;

void drv_spi_cs_set_low(void);
void drv_spi_cs_set_high(void);

#ifdef __cplusplus
}
#endif

#endif // _BSP_SPI_H_