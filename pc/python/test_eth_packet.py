from scapy.all import Ether, sendp, get_if_list

# Broadcast Ethernet address
broadcast_mac = "ff:ff:ff:ff:ff:ff"

# Create the packet
pkt = Ether(dst=broadcast_mac) / b"Hello KSZ8851"

print("All detected interfaces:")
interfaces = get_if_list()
for iface in interfaces:
    print(f" - {iface}")

# Send the broadcast packet specifically on en10 interface
target_interface = "en10"
if target_interface in interfaces:
    print(f"\nSending broadcast packet on {target_interface}...")
    sendp(pkt, iface=target_interface, count=10)
    print(f"✅ Sent 10 packets on {target_interface}")
else:
    print(f"\n❌ Interface {target_interface} not found!")
    print("Available interfaces:")
    for iface in interfaces:
        print(f" - {iface}")
