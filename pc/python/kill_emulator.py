#!/usr/bin/env python3
"""Helper script to kill existing DOIP emulator processes"""

import os
import subprocess
import signal
import sys

def find_emulator_processes():
    """Find running DOIP emulator processes"""
    try:
        result = subprocess.run(['ps', 'aux'], capture_output=True, text=True)
        lines = result.stdout.split('\n')
        
        emulator_processes = []
        for line in lines:
            if 'doip_multi_ecu_emulator' in line and 'python' in line:
                parts = line.split()
                if len(parts) >= 2:
                    pid = parts[1]
                    emulator_processes.append((pid, line.strip()))
        
        return emulator_processes
    except Exception as e:
        print(f"Error finding processes: {e}")
        return []

def kill_process(pid):
    """Kill a process by PID"""
    try:
        os.kill(int(pid), signal.SIGTERM)
        return True
    except Exception as e:
        print(f"Error killing process {pid}: {e}")
        return False

def check_port_usage(port):
    """Check what's using a specific port"""
    try:
        result = subprocess.run(['lsof', '-i', f':{port}'], capture_output=True, text=True)
        if result.stdout.strip():
            print(f"Port {port} is in use by:")
            print(result.stdout)
            return True
        else:
            print(f"Port {port} is available")
            return False
    except Exception as e:
        print(f"Error checking port {port}: {e}")
        return False

def main():
    print("🔍 DOIP Emulator Process Manager")
    print("=" * 40)
    
    # Check for existing emulator processes
    processes = find_emulator_processes()
    
    if processes:
        print(f"Found {len(processes)} running emulator process(es):")
        for pid, cmd in processes:
            print(f"  PID {pid}: {cmd}")
        
        # Ask user if they want to kill them
        response = input("\nDo you want to kill these processes? (y/N): ").strip().lower()
        
        if response in ['y', 'yes']:
            killed_count = 0
            for pid, cmd in processes:
                if kill_process(pid):
                    print(f"✅ Killed process {pid}")
                    killed_count += 1
                else:
                    print(f"❌ Failed to kill process {pid}")
            
            print(f"\nKilled {killed_count}/{len(processes)} processes")
        else:
            print("No processes killed")
    else:
        print("✅ No running emulator processes found")
    
    # Check port usage
    print(f"\n🔍 Checking port usage:")
    check_port_usage(13400)  # DOIP port
    
    print(f"\n💡 If ports are still in use, you can:")
    print(f"   • Wait a few seconds for processes to fully terminate")
    print(f"   • Use: sudo lsof -i :13400 to see what's using the port")
    print(f"   • Use: sudo kill -9 <PID> to force kill if needed")

if __name__ == "__main__":
    main()
