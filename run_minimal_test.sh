#!/bin/bash

# Script to build and flash the minimal KSZ test via JLink

echo "=========================================="
echo "Building and Flashing KSZ Minimal Test"
echo "=========================================="

# Step 1: Build the minimal test
echo "Step 1: Building minimal test firmware..."
make simple

# Check if build was successful
if [ ! -f "build/AtmelStart.bin" ]; then
    echo "ERROR: Build failed - build/AtmelStart.bin not found"
    exit 1
fi

echo "✓ Build successful: $(ls -lh build/AtmelStart.bin)"

# Step 2: Flash via JLink
echo ""
echo "Step 2: Flashing via JLink..."
echo "Make sure:"
echo "  - JLink debugger is connected to SAME54"
echo "  - Target power is ON"
echo "  - SWD connections are correct"
echo ""

# Flash using JLink
JLinkExe -CommanderScript flash_minimal_test.jlink

echo ""
echo "=========================================="
echo "Minimal Test Flash Complete!"
echo "=========================================="
echo ""
echo "What to expect:"
echo "1. Serial output should show:"
echo "   'KSZ8851SNL MINIMAL HARDWARE TEST'"
echo "   'NO ECU, NO DOIP, NO COMPLEX SETUP'"
echo ""
echo "2. Wireshark should show:"
echo "   - Clean 60-byte frames (not 1029 bytes)"
echo "   - Pattern: 01 02 03 04 05... (not 0x55)"
echo "   - EtherType: 0x8888"
echo "   - Source MAC: 00:00:00:00:20:76"
echo ""
echo "3. If you still see 0x55 corruption:"
echo "   - Hardware issue (SPI wiring, power, etc.)"
echo "   - KSZ chip problem"
echo ""
echo "Monitor serial output and Wireshark now!"