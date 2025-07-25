# DOIP Testing Setup Guide

This guide will help you set up direct communication between your MacBook Pro and the embedded device for DOIP testing.

## 🔧 MacBook Pro Network Configuration

### Step 1: Configure USB-C Ethernet Adapter

1. **Connect** your USB-C to Ethernet adapter to MacBook Pro
2. **Connect** Ethernet cable between adapter and embedded device
3. **Open** System Preferences → Network
4. **Select** your USB Ethernet adapter from the list
5. **Set** "Configure IPv4" to **"Manually"**
6. **Configure** the following settings:
   - **IP Address**: `192.168.100.1`
   - **Subnet Mask**: `255.255.255.0`
   - **Router**: Leave empty
   - **DNS Servers**: Leave empty
7. **Click** "Apply"

### Step 2: Verify Network Configuration

Open Terminal and verify the configuration:

```bash
# Check interface configuration
ifconfig

# Look for an interface with IP 192.168.100.1
# Should show something like:
# en7: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500
#      inet 192.168.100.1 netmask 0xffffff00 broadcast 192.168.100.255
```

## 🔌 Hardware Setup

1. **Power on** the embedded device
2. **Connect** Ethernet cable between:
   - MacBook Pro USB-C Ethernet adapter
   - Embedded device Ethernet port
3. **Wait** for link establishment (LED indicators on both sides should show connection)

## 📡 Network Configuration Summary

| Device | IP Address | Purpose |
|--------|------------|---------|
| **MacBook Pro** | `192.168.100.1` | DOIP ECU Emulator (Python script) |
| **Embedded Device** | `192.168.100.2` | DOIP Client (Firmware) |
| **Subnet** | `255.255.255.0` | `/24` network |
| **Network** | `192.168.100.0/24` | Direct point-to-point |

## 🐍 Python DOIP Emulator Setup

### Step 1: Install Dependencies (Optional)

For better interface detection, install netifaces:

```bash
pip3 install netifaces
```

### Step 2: Check Network Setup

Run the network setup helper:

```bash
cd pc/python
python3 setup_network.py
```

This will verify your network configuration and show if everything is ready.

### Step 3: Start DOIP ECU Emulator

```bash
cd pc/python
python3 doip_ecu_emulator.py
```

**Expected Output:**
```
Starting DOIP ECU Emulator
VIN: WBAVN31010AE12345
ECU Address: 0x1001
Logical Address: 0x0001
Press Ctrl+C to stop

Found DOIP interface en7: 192.168.100.1
UDP discovery server listening on 192.168.100.1:13400
TCP diagnostic server listening on 192.168.100.1:13400
```

## 🖥️ Embedded Device Setup

### Step 1: Flash Updated Firmware

1. **Build** the firmware with: `make clean && make all`
2. **Flash** `build/AtmelStart.hex` to your embedded device
3. **Power cycle** the device

### Step 2: Monitor Debug Output

Use SEGGER RTT Viewer or your preferred debugging tool to monitor the output.

**Expected Output:**
```
=== Network Events Logging Initialized ===
[LWIP] Stack initialization: SUCCESS
[INIT] Ethernet link detected
IP_ADDR    : 192.168.100.2
NET_MASK   : 255.255.255.0
GATEWAY_IP : 192.168.100.1

DOIP Client: Task started

=== DOIP Client Diagnostic Cycle ===
DOIP Client: Network ready
  IP Address: 192.168.100.2
  Starting vehicle discovery...
DOIP Client: Starting vehicle discovery
DOIP Client: UDP socket created successfully
DOIP Client: Discovery request sent
```

## 🔄 Testing DOIP Communication

When both systems are running correctly, you should see:

### On Python Emulator:
```
Received vehicle identification request from ('192.168.100.2', 54321)
Sending vehicle identification response: VIN=WBAVN31010AE12345, Logical Address=0x0001
TCP client connected from ('192.168.100.2', 54322)
Routing activation request from ('192.168.100.2', 54322), Source Address: 0x0E80
Routing activation successful for source 0x0E80
Diagnostic message: SA=0x0E80, TA=0x0001, Data=22f190
Sending positive response for DID 0xF190: WBAVN31010AE12345
```

### On Embedded Device (SEGGER RTT):
```
DOIP Client: Vehicle discovered
  VIN: WBAVN31010AE12345
  Logical Address: 0x0001
  IP Address: 192.168.100.1
DOIP Client: TCP connection established
DOIP Client: Routing activation successful

--- Reading Vehicle Information ---
VIN: WBAVN31010AE12345
ECU Software Version: SW_V1.2.3
ECU Hardware Version: HW_V2.0.1
--- Diagnostic cycle completed ---
```

## 🛠️ Troubleshooting

### Problem: No IP Address on MacBook

**Solution:** 
- Check USB-C adapter connection
- Verify manual IP configuration in Network preferences
- Try unplugging and reconnecting the adapter

### Problem: Embedded Device Shows "Waiting for IP address"

**Solution:**
- Check Ethernet cable connection
- Verify embedded device has link (LED indicators)
- Power cycle the embedded device

### Problem: Python Script Shows "No interface found"

**Solution:**
- Run `python3 setup_network.py` to check configuration
- Ensure MacBook IP is set to `192.168.100.1`
- Try running Python script with sudo (if permission issues)

### Problem: Discovery Requests Fail

**Solution:**
- Check both devices are on same network (`192.168.100.x`)
- Verify no firewall blocking ports 13400
- Ensure both devices can ping each other:
  ```bash
  # From MacBook:
  ping 192.168.100.2
  
  # From embedded device (if available):
  ping 192.168.100.1
  ```

### Problem: Firewall Blocking Connections

**Solution:**
- **macOS**: System Preferences → Security & Privacy → Firewall → Allow Python through firewall
- **Alternative**: Temporarily disable firewall for testing

## 📊 Protocol Flow Verification

1. **UDP Discovery**: Embedded device broadcasts vehicle identification request
2. **UDP Response**: Python emulator responds with vehicle information
3. **TCP Connection**: Embedded device connects to emulator
4. **Routing Activation**: Establishes diagnostic session
5. **UDS Communication**: Read VIN, software version, hardware version
6. **Cycle Repeat**: Every 10 seconds

This setup provides a complete DOIP testing environment for automotive diagnostic development!