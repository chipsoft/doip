#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Typedefs for the various structures */
typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;
typedef QueueHandle_t sys_mbox_t;
typedef TaskHandle_t sys_thread_t;

/* Since there is no SYS_ARCH_TIMEOUT, we use magic value 0xFFFFFFFF to indicate timeout */
#define SYS_ARCH_TIMEOUT 0xFFFFFFFF

/* Used to indicate invalid handle */
#define SYS_SEM_NULL     NULL
#define SYS_MBOX_NULL    NULL
#define SYS_MUTEX_NULL   NULL

#endif /* LWIP_ARCH_SYS_ARCH_H */