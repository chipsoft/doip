# Active Context - DOIP Large Message TCP Flow Control Issue

## Current Issue
**Problem**: Large DOIP messages (3073+ bytes) fail with "tcp_write failed for payload - err=-1"

**Root Cause**: TCP send buffer size limitation in lwIP configuration
- Current TCP send buffer: `TCP_SND_BUF = 2 * TCP_MSS = 2 * 1460 = 2920 bytes`
- Large messages: 3073+ bytes exceed the send buffer capacity
- Error -1 in lwIP typically indicates `ERR_MEM` (memory allocation failure)

## Error Pattern Analysis
```
✅ 2049 bytes: SUCCESS (fits in TCP send buffer)
❌ 3073 bytes: FAILS - "tcp_write failed for payload - err=-1"
❌ 4097 bytes: FAILS - "tcp_write failed for payload - err=-1"
```

## ✅ SOLUTION IMPLEMENTED: Universal Chunking Approach
The DOIP implementation now includes a **universal chunking solution** that dynamically calculates optimal chunk sizes based on lwIP parameters:

### **Universal Chunking Implementation Details**
- **Dynamic Chunk Size**: `TCP_MSS - 60` (safety margin)
- **Bounds Checking**: Minimum 512 bytes, Maximum 2048 bytes
- **Flow Control**: Checks TCP send buffer space before each chunk
- **Retry Logic**: Up to 3 retries with exponential backoff
- **Progress Tracking**: Detailed logging of chunk transmission progress

### **Key Features**
1. **Universal Compatibility**: Works with any lwIP TCP_MSS configuration
2. **Dynamic Calculation**: `doip_get_optimal_chunk_size()` function
3. **Safety Bounds**: Enforces minimum (512) and maximum (2048) chunk sizes
4. **TCP Buffer Monitoring**: Checks `tcp_sndbuf()` before each write
5. **Adaptive Chunking**: Splits large payloads into optimal chunks
6. **Error Recovery**: Retry logic with increasing delays
7. **Progress Logging**: Real-time feedback on transmission progress

### **Implementation Location**
- **File**: `hw/same54/drivers/bsp_doip_raw.c`
- **Function**: `doip_send_chunked_payload()`
- **Helper Functions**: `doip_get_optimal_chunk_size()`, `doip_print_tcp_buffer_info()`

### **Universal Chunk Size Calculation**
```c
#define DOIP_TCP_CHUNK_SIZE          (TCP_MSS - 60)  /**< Dynamic chunk size */
#define DOIP_CHUNK_SIZE_MIN          512     /**< Minimum chunk size */
#define DOIP_CHUNK_SIZE_MAX          2048    /**< Maximum chunk size */

static inline uint32_t doip_get_optimal_chunk_size(void)
{
    uint32_t calculated_size = TCP_MSS - 60;  // Safety margin
    
    // Ensure chunk size is within reasonable bounds
    if (calculated_size < DOIP_CHUNK_SIZE_MIN) {
        calculated_size = DOIP_CHUNK_SIZE_MIN;
    } else if (calculated_size > DOIP_CHUNK_SIZE_MAX) {
        calculated_size = DOIP_CHUNK_SIZE_MAX;
    }
    
    return calculated_size;
}
```

### **Example Chunk Sizes for Different Configurations**
| TCP_MSS | Calculated Chunk Size | Description |
|---------|----------------------|-------------|
| 1460 | 1400 | Standard Ethernet |
| 1024 | 964 | Small networks |
| 2048 | 1988 | Large networks |
| 512 | 512 | Minimum enforced |
| 4096 | 2048 | Maximum enforced |

## Expected Results with Universal Chunking
- **Any TCP_MSS configuration**: ✅ SUCCESS with dynamic chunking
- **8KB+ messages**: ✅ SUCCESS with universal chunking
- **Performance**: Maintained with proper flow control
- **Reliability**: Improved error handling and retry logic
- **Memory**: Better utilization of available TCP buffers
- **Portability**: Works across different lwIP configurations

## Testing Recommendations
1. **Test with different TCP_MSS values**: Verify dynamic calculation
2. **Test 8KB+ messages**: Verify universal chunking handles maximum size
3. **Monitor logs**: Look for chunking progress and TCP buffer info
4. **Performance test**: Measure transmission time for large messages
5. **Configuration test**: Test with various lwIP parameter combinations

## Files Modified
- ✅ `hw/same54/drivers/bsp_doip_raw.c`: Universal chunking implementation
- ✅ `pc/python/test_universal_chunking.py`: Universal chunking test
- ✅ Constants: Dynamic `DOIP_TCP_CHUNK_SIZE` based on `TCP_MSS`
- ✅ Helper functions: `doip_get_optimal_chunk_size()`, `doip_print_tcp_buffer_info()`
- ✅ Error handling: Retry logic with exponential backoff
- ✅ Progress logging: Real-time chunk transmission feedback

## Universal Solution Benefits
- **Portable**: Works with any lwIP configuration
- **Adaptive**: Automatically adjusts to TCP_MSS changes
- **Safe**: Enforces reasonable bounds for chunk sizes
- **Efficient**: Uses optimal chunk size for each configuration
- **Robust**: Includes comprehensive error handling and monitoring
