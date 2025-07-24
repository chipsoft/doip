=======================
LwIP Socket API Example
=======================

The LwIP socket API example demonstrates the use of socket apis of LwIP stack to implement basic webserver
application.

The example sends "Socket API implementation" to the debug console by usart port, and
once the ethernet connection was established, the debug console will print
the static IP address and netmask address. The user can ping the board from
the host computer.

The use can also see the string message from the embedded web server by entering the IP address
of embedded web server in the web browser

Middleware
----------
* TCPIP Stack
* TCPIP Stack Interface
* Ethernet PHY
* STDIO Redirect
* Ethernet MAC
* FreeRToS

Running the demo:
-----------------
1. Press Download Pack and save the .atzip file
2. Import .atzip file into Atmel Studio 7, File->Import->Atmel Start Project.
3. Build and flash into supported evaluation board 

- NOTE: 
    If compilation results in error: "redefinition of 'struct timeval'", 
    perform below step to make compilation successful.

    Navigate to sockets.h file in $PROJECT_LOCAION$\lwip\lwip-1.4.0\src\include\lwip &
    change the MACRO LWIP_TIMEVAL_PRIVATE like below

    "#define LWIP_TIMEVAL_PRIVATE       1" to
    "#define LWIP_TIMEVAL_PRIVATE       0"


4. Open the serial port in Atmel Data Visualizer or any serial port monitor.
5. Press the Reset button.
6. You should see the following text:

        Socket API implementation
        Ethernet link up
        IP_ADDR    : 192.168.1.100
        NET_MASK   : 255.255.255.0
        GATEWAY_IP : 192.168.1.1
