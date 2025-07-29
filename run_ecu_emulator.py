#!/usr/bin/env python3
"""
DOIP ECU Emulator Runner
Convenience script to run the enhanced DOIP ECU emulator
"""

import sys
import os
import subprocess

def main():
    print("🏭 DOIP ECU Emulator Runner")
    print("=" * 50)
    
    # Path to the ECU emulator
    emulator_script_path = os.path.join(os.path.dirname(__file__), "pc", "python", "doip_ecu_emulator.py")
    
    if not os.path.exists(emulator_script_path):
        print(f"❌ Emulator script not found at: {emulator_script_path}")
        return 1
    
    print(f"📂 Running emulator from: {emulator_script_path}")
    print()
    
    # Allow VIN to be passed as argument
    args = [sys.executable, emulator_script_path] + sys.argv[1:]
    
    try:
        # Run the ECU emulator
        result = subprocess.run(args, cwd=os.path.dirname(__file__))
        return result.returncode
    except KeyboardInterrupt:
        print("\n⏹️  Emulator execution interrupted by user")
        return 1
    except Exception as e:
        print(f"❌ Error running emulator: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())