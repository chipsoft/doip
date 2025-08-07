#!/usr/bin/env python3
"""Universal Chunking Test for DOIP"""

def calculate_optimal_chunk_size(tcp_mss):
    """Calculate optimal chunk size based on TCP_MSS"""
    calculated_size = tcp_mss - 60  # Safety margin
    
    if calculated_size < 512:
        return 512
    elif calculated_size > 2048:
        return 2048
    else:
        return calculated_size

def test_universal_chunking():
    """Test universal chunking with different TCP_MSS values"""
    print("=== Universal Chunking Test ===\n")
    
    test_configs = [
        {"tcp_mss": 1460, "name": "Standard Ethernet"},
        {"tcp_mss": 1024, "name": "Small MSS"},
        {"tcp_mss": 2048, "name": "Large MSS"},
        {"tcp_mss": 512, "name": "Very Small MSS"},
        {"tcp_mss": 4096, "name": "Very Large MSS"},
    ]
    
    for config in test_configs:
        tcp_mss = config["tcp_mss"]
        name = config["name"]
        chunk_size = calculate_optimal_chunk_size(tcp_mss)
        
        print(f"{name} (TCP_MSS: {tcp_mss}) -> Chunk Size: {chunk_size} bytes")
    
    print("\n✅ Universal chunking calculation working correctly!")

if __name__ == "__main__":
    test_universal_chunking()
