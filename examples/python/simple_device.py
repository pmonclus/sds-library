#!/usr/bin/env python3
"""
Simple SDS device example.

This example demonstrates how to create a Python device node that:
1. Connects to an MQTT broker
2. Registers as a device for a table
3. Receives configuration from the owner
4. Publishes state and status updates using C-like syntax

Requirements:
- MQTT broker running (e.g., mosquitto on localhost:1883)
- SDS library built with generated types that include "SensorData"
- Generated sds_types.py from schema.sds

Usage:
    python simple_device.py [broker_host] [node_id]
"""

import sys
import time
import random
import signal

# Import SDS library
from sds import SdsNode, Role, SdsError, SdsMqttError, LogLevel, set_log_level

# Import generated schema types (from sds_codegen.py)
# Add parent directory to path for development
sys.path.insert(0, str(__file__).rsplit('/', 2)[0])
try:
    from sds_types import SensorData
except ImportError:
    print("Error: sds_types.py not found.")
    print("Generate it with: python tools/sds_codegen.py schema.sds --python -o python/")
    sys.exit(1)


# Global flag for graceful shutdown
running = True


def signal_handler(signum, frame):
    """Handle Ctrl+C for graceful shutdown."""
    global running
    print("\nShutting down...")
    running = False


def main():
    # Parse command line arguments
    broker_host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    node_id = sys.argv[2] if len(sys.argv) > 2 else f"py_sensor_{random.randint(1000, 9999)}"
    
    print(f"SDS Python Device Example")
    print(f"  Node ID: {node_id}")
    print(f"  Broker:  {broker_host}:1883")
    print()
    
    # Set log level (optional - can be called before creating node)
    set_log_level(LogLevel.INFO)
    
    # Set up signal handler for graceful shutdown
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        # Create and initialize node
        with SdsNode(node_id, broker_host, port=1883) as node:
            print("Connected to MQTT broker")
            
            # Register with generated schema bundle - no manual dataclass needed!
            try:
                table = node.register_table("SensorData", Role.DEVICE, schema=SensorData)
                print("Registered as DEVICE for SensorData table")
            except SdsError as e:
                print(f"Failed to register table: {e}")
                print("Make sure the C library includes generated types for SensorData")
                return 1
            
            # Set up config callback
            @node.on_config("SensorData")
            def handle_config(table_type):
                # Read config using C-like syntax
                print(f"[CONFIG] Received new configuration:")
                print(f"         command={table.config.command}")
                print(f"         threshold={table.config.threshold}")
            
            # Set up error callback
            @node.on_error
            def handle_error(error_code, context):
                print(f"[ERROR] Code {error_code}: {context}")
            
            print("\nDevice running. Press Ctrl+C to stop.\n")
            
            # Simulated sensor readings
            iteration = 0
            
            # Main loop
            while running:
                # Simulate sensor readings
                temperature = 20.0 + random.uniform(-5, 5) + (iteration % 10) * 0.1
                humidity = 50.0 + random.uniform(-10, 10)
                
                # Update state using C-like syntax
                table.state.temperature = temperature
                table.state.humidity = humidity
                
                print(f"[STATE] temp={table.state.temperature:.1f}C, humidity={table.state.humidity:.1f}%")
                
                # Update status periodically
                if iteration % 5 == 0:
                    table.status.error_code = 0
                    table.status.battery_percent = 95
                    table.status.uptime_seconds = int(time.time()) % 86400
                    print(f"[STATUS] error={table.status.error_code}, battery={table.status.battery_percent}%")
                
                # Process MQTT messages (publishes state/status changes)
                node.poll(timeout_ms=0)
                
                # Wait before next iteration
                time.sleep(1)
                iteration += 1
            
            print("\nDevice stopped.")
    
    except SdsMqttError as e:
        print(f"MQTT connection error: {e}")
        return 1
    except SdsError as e:
        print(f"SDS error: {e}")
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
