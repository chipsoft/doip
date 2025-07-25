#!/usr/bin/env python3
"""
DOIP Network Setup Helper
Configures the Python DOIP emulator to bind to the correct network interface
for direct MacBook to embedded device communication.
"""

import socket
import netifaces
import sys

def get_network_interfaces():
    """Get all available network interfaces"""
    interfaces = netifaces.interfaces()
    print("Available network interfaces:")
    
    for i, iface in enumerate(interfaces):
        try:
            addrs = netifaces.ifaddresses(iface)
            if netifaces.AF_INET in addrs:
                ip_info = addrs[netifaces.AF_INET][0]
                ip = ip_info.get('addr', 'N/A')
                print(f"  {i+1}. {iface}: {ip}")
            else:
                print(f"  {i+1}. {iface}: No IPv4 address")
        except Exception as e:
            print(f"  {i+1}. {iface}: Error reading interface - {e}")
    
    return interfaces

def find_doip_interface():
    """Find the interface in the 192.168.100.x network"""
    interfaces = netifaces.interfaces()
    
    for iface in interfaces:
        try:
            addrs = netifaces.ifaddresses(iface)
            if netifaces.AF_INET in addrs:
                ip_info = addrs[netifaces.AF_INET][0]
                ip = ip_info.get('addr', '')
                if ip.startswith('192.168.100.'):
                    return iface, ip
        except:
            continue
    
    return None, None

def check_network_config():
    """Check if the network is properly configured for DOIP testing"""
    print("=== DOIP Network Configuration Check ===\n")
    
    # Find DOIP interface
    iface, ip = find_doip_interface()
    
    if iface and ip:
        print(f"✅ Found DOIP interface: {iface} ({ip})")
        
        # Test if we can bind to DOIP ports
        try:
            # Test UDP port 13400
            udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            udp_sock.bind((ip, 13400))
            udp_sock.close()
            print("✅ UDP port 13400 is available")
        except Exception as e:
            print(f"❌ UDP port 13400 is not available: {e}")
        
        try:
            # Test TCP port 13400
            tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tcp_sock.bind((ip, 13400))
            tcp_sock.close()
            print("✅ TCP port 13400 is available")
        except Exception as e:
            print(f"❌ TCP port 13400 is not available: {e}")
        
        print(f"\n📋 Network Configuration Summary:")
        print(f"   MacBook IP: {ip}")
        print(f"   Embedded Device IP: 192.168.100.2")
        print(f"   Interface: {iface}")
        
        return ip
    else:
        print("❌ No interface found in 192.168.100.x network")
        print("\n🔧 Please configure your USB-C Ethernet adapter:")
        print("   1. Go to System Preferences → Network")
        print("   2. Select USB Ethernet adapter")
        print("   3. Set Configure IPv4 to 'Manually'")
        print("   4. Set IP Address: 192.168.100.1")
        print("   5. Set Subnet Mask: 255.255.255.0")
        print("   6. Leave Router and DNS empty")
        
        print("\n🔍 Current network interfaces:")
        get_network_interfaces()
        
        return None

def test_connection():
    """Test connection to embedded device"""
    target_ip = "192.168.100.2"
    print(f"\n🔌 Testing connection to embedded device ({target_ip})...")
    
    try:
        # Test ping (using socket connect test instead of actual ping)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        result = sock.connect_ex((target_ip, 80))  # Test any port for basic connectivity
        sock.close()
        
        if result == 0:
            print(f"✅ Can reach embedded device at {target_ip}")
        else:
            print(f"⚠️  Cannot reach embedded device at {target_ip}")
            print("   Make sure the embedded device is powered on and connected")
    except Exception as e:
        print(f"⚠️  Connection test failed: {e}")

if __name__ == "__main__":
    print("DOIP Network Setup Helper")
    print("=" * 40)
    
    # Check if netifaces is available
    try:
        import netifaces
    except ImportError:
        print("❌ 'netifaces' module not found")
        print("Install it with: pip3 install netifaces")
        sys.exit(1)
    
    # Check network configuration
    doip_ip = check_network_config()
    
    if doip_ip:
        # Test connection
        test_connection()
        
        print(f"\n🚀 Ready to start DOIP emulator!")
        print(f"   Run: python3 doip_ecu_emulator.py")
        print(f"   The emulator will bind to: {doip_ip}:13400")
    else:
        print(f"\n❌ Network not ready for DOIP testing")
        sys.exit(1)