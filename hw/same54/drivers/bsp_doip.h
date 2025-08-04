#ifndef _BSP_DOIP_H_
#define _BSP_DOIP_H_

#include "driver_doip.h"

#ifdef __cplusplus
extern "C" {
#endif

// Compile-time configuration for DOIP implementation
// Set DOIP_USE_RAW_LWIP to 1 for raw lwIP implementation
// Set DOIP_USE_RAW_LWIP to 0 for socket-based implementation
#ifndef DOIP_USE_RAW_LWIP
#define DOIP_USE_RAW_LWIP 0  // Default to socket implementation
#endif

// Global driver instance (defined in the selected BSP implementation)
extern drv_doip_t doip_0;

#ifdef __cplusplus
}
#endif

#endif // _BSP_DOIP_H_