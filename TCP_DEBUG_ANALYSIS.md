# TCP Message Sending Debug Analysis

## Overview
We've added comprehensive debugging to identify why TCP messages are not being sent via the KSZ8851SNL chip. The debugging will show the exact failure point in the TCP message flow.

## What We've Added

### 1. Enhanced TCP Send Function Debugging (`bsp_doip_socket.c`)
- **Function**: `doip_send_tcp_message_socket()`
- **Debug Output**: 
  - Socket validation
  - Message structure details
  - Buffer allocation status
  - `send()` function call details
  - **Error Analysis**: Detailed errno analysis with human-readable error messages

### 2. Diagnostic Request Flow Debugging (`bsp_doip_socket.c`)
- **Function**: `drv_doip_send_diagnostic_request_impl()`
- **Debug Output**:
  - Function entry with all parameters
  - DOIP state validation
  - Message building process
  - Payload size calculations
  - TCP send attempt status

### 3. Connection Flow Debugging (`bsp_doip_socket.c`)
- **Function**: `drv_doip_connect_to_vehicle_impl()`
- **Debug Output**:
  - Connection parameters
  - Socket creation status
  - TCP connect() call details
  - Routing activation flow
  - Connection state transitions

### 4. Main Task Flow Debugging (`user_tasks.c`)
- **Function**: `doip_client_task()`
- **Debug Output**:
  - Task startup and initialization
  - Driver status checks
  - ECU discovery process
  - Connection attempts
  - Diagnostic request flow

## Expected Debug Output

### Successful Flow:
```
🔍 [DEBUG] DOIP Client Task started
🔍 [DEBUG] Initializing DOIP driver...
✅ [DEBUG] DOIP driver initialized successfully
🔍 [DEBUG] Current DOIP state: 0
🔍 [DEBUG] In IDLE state, starting ECU discovery...
🔍 [DEBUG] ECU discovery completed: 1 ECUs found
🔍 [DEBUG] Attempting to connect to primary ECU (0x1001)...
🔍 [DEBUG] drv_doip_connect_to_vehicle_impl() called
🔍 [DEBUG] Creating TCP socket...
✅ [DEBUG] TCP socket created successfully: 0
🔍 [DEBUG] Calling connect() function...
✅ [DEBUG] connect() successful
🔍 [DEBUG] Sending routing activation request...
✅ [DEBUG] Routing activation request sent successfully
✅ [DEBUG] Routing activation successful
✅ [DEBUG] Connection established successfully
🔍 [DEBUG] Reading VIN...
🔍 [DEBUG] drv_doip_send_diagnostic_request_impl() called
🔍 [DEBUG] Building small message...
🔍 [DEBUG] About to send diagnostic request...
🔍 [DEBUG] doip_send_tcp_message_socket() called
✅ [DEBUG] TCP message sent successfully (X bytes)
✅ [DEBUG] Diagnostic request sent successfully
```

### Failure Scenarios to Look For:

#### A. Connection State Issues:
```
❌ [DEBUG] Not connected or activated (state: X)
```
**Problem**: DOIP client not in correct state
**Solution**: Check connection flow and state management

#### B. TCP Socket Issues:
```
❌ [DEBUG] Invalid socket: -1
❌ [DEBUG] send() failed with error: ENOTCONN
```
**Problem**: Invalid or closed TCP socket
**Solution**: Check socket creation and connection flow

#### C. Network Interface Issues:
```
❌ [DEBUG] send() failed with error: ENOBUFS
❌ [DEBUG] send() failed with error: EMSGSIZE
```
**Problem**: Network buffer or message size issues
**Solution**: Check lwIP configuration and buffer sizes

#### D. Message Building Issues:
```
❌ [DEBUG] Message too large (X bytes, max: Y)
❌ [DEBUG] Additional payload too large for small message buffer
```
**Problem**: Message size exceeds buffer limits
**Solution**: Check buffer size configurations

## Testing Steps

### 1. Flash the Updated Code
```bash
# The code has been compiled successfully
# Flash to your SAME54 board
```

### 2. Monitor Serial Output
Look for the debug messages starting with:
- `🔍 [DEBUG]` - Information gathering
- `✅ [DEBUG]` - Success indicators  
- `❌ [DEBUG]` - Error indicators

### 3. Run the Python Test Script
```bash
cd pc/python
python3 test_tcp_debug.py [IP_ADDRESS] [PORT]
```

### 4. Key Things to Watch For

#### A. **DOIP State Transitions**:
- `IDLE` → `CONNECTING` → `ACTIVATED`
- Any unexpected state changes

#### B. **TCP Socket Lifecycle**:
- Socket creation (should return positive number)
- Connection establishment
- Socket validity before send operations

#### C. **Message Flow**:
- Message building success
- Buffer allocation success
- TCP send() function return values

#### D. **Error Codes**:
- `errno` values from failed operations
- Specific error messages (EBADF, ENOTCONN, ENOBUFS, etc.)

## Common Issues and Solutions

### 1. **"Not connected or activated"**
- **Cause**: DOIP client not in correct state
- **Check**: Connection flow, routing activation success

### 2. **"Invalid socket"**
- **Cause**: TCP socket creation failed or socket closed
- **Check**: Network initialization, socket creation, connection flow

### 3. **"send() failed with ENOTCONN"**
- **Cause**: TCP connection not established
- **Check**: Network interface, IP configuration, routing

### 4. **"send() failed with ENOBUFS"**
- **Cause**: Network buffer full
- **Check**: lwIP buffer configuration, network congestion

### 5. **"Message too large"**
- **Cause**: Message exceeds buffer limits
- **Check**: Buffer size configurations, message chunking

## Next Steps

1. **Flash the updated code** to your SAME54 board
2. **Monitor the serial output** for debug messages
3. **Identify the exact failure point** from the debug output
4. **Use the Python test script** to verify external connectivity
5. **Report the specific error messages** you see

The comprehensive debugging should now show exactly where the TCP sending is failing, allowing us to implement the correct fix.

