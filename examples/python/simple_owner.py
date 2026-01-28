#!/usr/bin/env python3
"""
Simple SDS owner example.

This example demonstrates how to create a Python owner node that:
1. Connects to an MQTT broker
2. Registers as an owner for a table
3. Receives state and status updates from devices
4. Uses C-like syntax to access device data
5. Iterates over connected devices

Requirements:
- MQTT broker running (e.g., mosquitto on localhost:1883)
- SDS library built with generated types that include "SensorData"
- Generated sds_types.py from schema.sds

Usage:
    python simple_owner.py [broker_host] [node_id]
"""

import sys
import time
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
    node_id = sys.argv[2] if len(sys.argv) > 2 else "py_owner_01"
    
    print(f"SDS Python Owner Example")
    print(f"  Node ID: {node_id}")
    print(f"  Broker:  {broker_host}:1883")
    print()
    
    # Set log level (optional)
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
                table = node.register_table("SensorData", Role.OWNER, schema=SensorData)
                print("Registered as OWNER for SensorData table")
            except SdsError as e:
                print(f"Failed to register table: {e}")
                print("Make sure the C library includes generated types for SensorData")
                return 1
            
            # Set initial config using C-like syntax
            table.config.command = 0
            table.config.threshold = 25.0
            print(f"Initial config: command={table.config.command}, threshold={table.config.threshold}")
            
            # Set up state callback - called when a device publishes state
            @node.on_state("SensorData")
            def handle_state(table_type, from_node):
                # Get device data using C-like syntax
                device = table.get_device(from_node)
                if device:
                    print(f"[STATE] Device {from_node}: online={device.online}")
                else:
                    print(f"[STATE] Device {from_node}: state update received")
            
            # Set up status callback - called when a device publishes status
            @node.on_status("SensorData")
            def handle_status(table_type, from_node):
                # Get device data using C-like syntax
                device = table.get_device(from_node)
                if device and device.status:
                    print(f"[STATUS] Device {from_node}: "
                          f"error={device.status.error_code}, "
                          f"battery={device.status.battery_percent}%, "
                          f"online={device.online}")
                else:
                    print(f"[STATUS] Device {from_node}: status update received")
            
            # Set up error callback
            @node.on_error
            def handle_error(error_code, context):
                print(f"[ERROR] Code {error_code}: {context}")
            
            print("\nOwner running. Waiting for device updates. Press Ctrl+C to stop.\n")
            
            # Track last status print time
            last_status_time = 0
            
            # Main loop
            while running:
                # Process MQTT messages
                node.poll(timeout_ms=0)
                
                # Print summary every 10 seconds
                current_time = time.time()
                if current_time - last_status_time >= 10:
                    last_status_time = current_time
                    
                    print(f"\n--- Device Summary ---")
                    print(f"Known devices: {table.device_count}")
                    
                    # Iterate over all devices using C-like syntax
                    for device_id, device in table.iter_devices():
                        status_str = "ONLINE" if device.online else "OFFLINE"
                        if device.status:
                            print(f"  - {device_id}: {status_str}, "
                                  f"battery={device.status.battery_percent}%")
                        else:
                            print(f"  - {device_id}: {status_str}")
                    
                    if table.device_count == 0:
                        print("  (no devices connected yet)")
                    print()
                
                # Small sleep to prevent CPU spinning
                time.sleep(0.1)
            
            print("\nOwner stopped.")
    
    except SdsMqttError as e:
        print(f"MQTT connection error: {e}")
        return 1
    except SdsError as e:
        print(f"SDS error: {e}")
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
