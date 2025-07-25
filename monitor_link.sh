#!/bin/bash

echo "🔍 Monitoring Ethernet link status..."
echo "Press Ctrl+C to stop"
echo

while true; do
    # Check MacBook interface
    mac_status=$(ifconfig en10 | grep "status:" | awk '{print $2}')
    mac_media=$(ifconfig en10 | grep "media:" | cut -d'(' -f2 | cut -d')' -f1)
    
    # Try ping to device
    ping_result=$(ping -c 1 -W 1000 192.168.100.2 2>/dev/null)
    if [ $? -eq 0 ]; then
        device_status="✅ REACHABLE"
    else
        device_status="❌ NO RESPONSE"
    fi
    
    timestamp=$(date '+%H:%M:%S')
    
    echo "[$timestamp] MacBook: $mac_status ($mac_media) | Device: $device_status"
    
    # Check if we have successful ping
    if [ "$device_status" = "✅ REACHABLE" ]; then
        echo "🎉 LINK ESTABLISHED! Device is now reachable."
        echo "The DOIP communication should start working now."
        break
    fi
    
    sleep 2
done