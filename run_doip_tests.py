#!/usr/bin/env python3
"""
DOIP Test Runner
Convenience script to run the comprehensive DOIP monitoring tests
"""

import sys
import os
import subprocess

def main():
    print("🚗 DOIP Comprehensive Monitoring Test Runner")
    print("=" * 50)
    
    # Path to the comprehensive test suite
    test_script_path = os.path.join(os.path.dirname(__file__), "pc", "python", "test_doip_fix.py")
    
    if not os.path.exists(test_script_path):
        print(f"❌ Test script not found at: {test_script_path}")
        return 1
    
    print(f"📂 Running tests from: {test_script_path}")
    print()
    
    try:
        # Run the comprehensive test suite
        result = subprocess.run([sys.executable, test_script_path], cwd=os.path.dirname(__file__))
        return result.returncode
    except KeyboardInterrupt:
        print("\n⏹️  Test execution interrupted by user")
        return 1
    except Exception as e:
        print(f"❌ Error running tests: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())