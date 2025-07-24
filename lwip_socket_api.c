/**
 * \file
 *
 * \brief LwIP socket api example
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

#include "lwip/sys.h"
#include "lwip/api.h"
#include "lwip/tcpip.h"
#include "webserver_tasks.h"
#include "FreeRTOS.h"
#include "sockets.h"
#include "lwip_socket_api.h"
#include "string.h"

typedef u32_t socklen_t;
char          buffer[1024];

/* This is the data for the socket web page. */
const static char socket_webpage[] = "<html> \
<head><title>Basic webpage</title></head> \
<body> \
Welcome to your basic webpage using Socket API. \
</body> \
</html>";
const static char http_html_hdr[]  = "HTTP/1.0 200 OK\r\nContent-type: text/html\r\n\r\n";

void basic_socket()
{
	/* Create application tasks */
	task_led_create();

	/* Create task for Ethernet */
	if (xTaskCreate(socket_basic_ethernet,
	                "Ethernet_basic",
	                TASK_ETHERNETBASIC_STACK_SIZE,
	                NULL,
	                (TASK_ETHERNETBASIC_STACK_PRIORITY - 1),
	                &xCreatedEthernetBasicTask)
	    != pdPASS) {
		while (1)
			;
	}

	/* Start FreeRTOS scheduler */
	vTaskStartScheduler();
}

void socket_basic_ethernet(void *p)
{

	struct sockaddr_in address;
	int                s_create, new_socket;
	int                addrlen = sizeof(address);
	int                opt     = 1;
	int                socket_check;

	sys_sem_t sem;
	err_t     err_sem;
	err_sem = sys_sem_new(&sem, 0); /* Create a new semaphore. */
	tcpip_init(tcpip_init_done, &sem);
	sys_sem_wait(&sem); /* Block until the lwIP stack is initialized. */
	sys_sem_free(&sem); /* Free the semaphore. */

	print_ipaddress();

	/*Create a socket*/
	s_create = socket(AF_INET, 1, 0);

	setsockopt(s_create, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

	address.sin_family      = AF_INET;
	address.sin_addr.s_addr = htonl(IPADDR_ANY);
	address.sin_port        = htons(HTTP_PORT);
	/* bind the connection to port */
	socket_check = bind(s_create, (struct sockaddr *)&address, sizeof(address));
	if (socket_check < 0) {
		LWIP_DEBUGF(LWIP_DBG_ON, ("Bind error=%d\n", socket_check));
		goto socket_close;
	}
	/* tell the connection to listen for incoming connection requests */
	listen(s_create, 3);

	for (;;) {
		new_socket = accept(s_create, (struct sockaddr *)&address, (socklen_t *)&addrlen);

		if (new_socket <= 0) {
			LWIP_DEBUGF(LWIP_DBG_ON, ("Connection error=%d\n", new_socket));
			goto socket_close;
		}
		socket_check = read(new_socket, buffer, 1024);
		if (socket_check <= 0) {
			LWIP_DEBUGF(LWIP_DBG_ON, ("Read error=%d\n", socket_check));
			goto socket_close;
		}
		/* Check if the request was an HTTP "GET /\r\n". */
		if (!strncmp(buffer, "GET", 3)) {
			socket_check = write(new_socket, http_html_hdr, strlen(http_html_hdr));
			if (socket_check <= 0) {
				LWIP_DEBUGF(LWIP_DBG_ON, ("Write error=%d\n", socket_check));
				goto socket_close;
			}

			/*Send the actual webpage*/
			socket_check = write(new_socket, socket_webpage, strlen(socket_webpage));
			if (socket_check <= 0) {
				LWIP_DEBUGF(LWIP_DBG_ON, ("Write error=%d\n", socket_check));
				goto socket_close;
			}
		}
	/*Close connection*/
	socket_close:
		close(new_socket);
	}
}
