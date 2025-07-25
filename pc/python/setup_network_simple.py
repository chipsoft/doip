#!/usr/bin/env python3
"""
Simple DOIP Network Setup Helper
Basic network configuration check without external dependencies
"""

import socket
import subprocess
import sys

def check_network_config():
    """Check if the network is properly configured for DOIP testing"""
    print("=== DOIP Network Configuration Check ===\n")
    
    # Test if we can bind to DOIP ports
    doip_ip = "192.168.100.1"
    
    try:
        # Test UDP port 13400
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.bind((doip_ip, 13400))
        udp_sock.close()
        print("✅ UDP port 13400 is available")
    except Exception as e:
        print(f"❌ UDP port 13400 is not available: {e}")
        print("   Make sure your MacBook IP is set to 192.168.100.1")
        return False
    
    try:
        # Test TCP port 13400
        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.bind((doip_ip, 13400))
        tcp_sock.close()
        print("✅ TCP port 13400 is available")
    except Exception as e:
        print(f"❌ TCP port 13400 is not available: {e}")
        return False
    
    print(f"\n📋 Network Configuration Summary:")
    print(f"   MacBook IP: {doip_ip}")
    print(f"   Embedded Device IP: 192.168.100.2")
    
    return True

def test_connection():
    """Test connection to embedded device"""
    target_ip = "192.168.100.2"
    print(f"\n🔌 Testing connection to embedded device ({target_ip})...")
    
    try:
        # Try to ping the device
        result = subprocess.run(['ping', '-c', '1', '-W', '2000', target_ip], 
                              capture_output=True, text=True)
        
        if result.returncode == 0:
            print(f"✅ Can reach embedded device at {target_ip}")
            return True
        else:
            print(f"⚠️  Cannot reach embedded device at {target_ip}")
            print("   Make sure the embedded device is powered on and connected")
            return False
    except Exception as e:
        print(f"⚠️  Ping test failed: {e}")
        return False

def show_network_interfaces():
    """Show network interface information using system commands"""
    print("\n🔍 Network interface information:")
    try:
        # Try to show interface info
        result = subprocess.run(['ifconfig'], capture_output=True, text=True)
        if result.returncode == 0:
            lines = result.stdout.split('\n')
            for line in lines:
                if '192.168.100.' in line:
                    print(f"   Found DOIP network: {line.strip()}")
        else:
            print("   Could not retrieve interface information")
    except Exception as e:
        print(f"   Error getting interface info: {e}")

def show_setup_instructions():
    """Show manual setup instructions"""
    print("\n🔧 Manual Network Setup Instructions:")
    print("   1. Go to System Preferences → Network")
    print("   2. Select your USB Ethernet adapter")
    print("   3. Set Configure IPv4 to 'Manually'")
    print("   4. Set IP Address: 192.168.100.1")
    print("   5. Set Subnet Mask: 255.255.255.0")
    print("   6. Leave Router and DNS empty")
    print("   7. Click Apply")

if __name__ == "__main__":
    print("Simple DOIP Network Setup Helper")
    print("=" * 40)
    
    # Check network configuration
    network_ok = check_network_config()
    
    if network_ok:
        # Test connection
        connection_ok = test_connection()
        
        if connection_ok:
            print(f"\n🚀 Ready to start DOIP emulator!")
            print(f"   Run: python3 doip_ecu_emulator.py")
        else:
            print(f"\n⚠️  Network configured but device not reachable")
            print(f"   Start the emulator anyway: python3 doip_ecu_emulator.py")
    else:
        print(f"\n❌ Network not ready for DOIP testing")
        show_network_interfaces()
        show_setup_instructions()
        sys.exit(1)