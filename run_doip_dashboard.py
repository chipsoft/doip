#!/usr/bin/env python3
"""
DOIP Dashboard Runner
Convenience script to run the real-time monitoring dashboard
"""

import sys
import os
import subprocess

def main():
    print("📊 DOIP Real-Time Monitoring Dashboard Runner")
    print("=" * 50)
    
    # Path to the monitoring dashboard
    dashboard_script_path = os.path.join(os.path.dirname(__file__), "pc", "python", "doip_monitoring_dashboard.py")
    
    if not os.path.exists(dashboard_script_path):
        print(f"❌ Dashboard script not found at: {dashboard_script_path}")
        return 1
    
    print(f"📂 Running dashboard from: {dashboard_script_path}")
    print()
    
    try:
        # Run the monitoring dashboard
        result = subprocess.run([sys.executable, dashboard_script_path], cwd=os.path.dirname(__file__))
        return result.returncode
    except KeyboardInterrupt:
        print("\n⏹️  Dashboard execution interrupted by user")
        return 1
    except Exception as e:
        print(f"❌ Error running dashboard: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())