LwIP Stack MAC Interface
========================

This software component implement functionlly how an Ethernet MAC Driver
register into the LwIP Stack.

Commonly the LwIP Stack should work with one or more Ethernet MAC Interface.
Application can use one or more TCPIP Stack Interface to register one or more
Ethernet MAC driver into LwIP Stack.

Features
--------

* Register an Ethernet MAC Driver into the LwIP TCPIP Stack

Dependencies
------------

* An instance of the Ethernet MAC driver is used by this driver.
* A LwIP Stack is used by this driver

Limitations
-----------

N/A
