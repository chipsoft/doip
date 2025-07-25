#!/usr/bin/env python3
import socket
import time

print("🔍 Simple UDP Listener Test")
print("Listening on port 13400...")
print("Press Ctrl+C to stop\n")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(('0.0.0.0', 13400))

packet_count = 0
while True:
    try:
        data, addr = sock.recvfrom(1024)
        packet_count += 1
        timestamp = time.strftime('%H:%M:%S.%f')[:-3]
        print(f"[{timestamp}] 📦 Packet #{packet_count} from {addr[0]}:{addr[1]}")
        print(f"   Size: {len(data)} bytes")
        print(f"   Raw: {data.hex()}")
        print()
    except KeyboardInterrupt:
        break

sock.close()
print(f"\n🛑 Received {packet_count} packets total")
