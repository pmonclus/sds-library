#!/usr/bin/env python3
"""
Hybrid Demo - Python Device

A simulated sensor device that:
- Responds to LED control from owner
- Publishes temperature/humidity if it's the "active" device
- Always publishes power consumption and rotating log messages

Usage: python device.py <node_id> [broker_host]
Example: python device.py py_dev_01 localhost
"""

import sys
import time
import signal
import random

# Add the SDS Python package to path (relative to this script)
import os
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(script_dir, "../../../python"))

from sds import SdsNode, Role, SdsError, SdsMqttError, LogLevel, set_log_level

# Import generated schema types
sys.path.insert(0, os.path.join(script_dir, "../python_owner"))
from demo_types import DeviceDemo


# Global state
running = True
node_id = None
led_state = 0

# Log message rotation
log_messages = [
    "I am {} and I feel good!",
    "I feel gloomy, {} here",
    "I need help, help {}!",
    "All systems nominal, {} reporting",
    "Running smoothly, {} out"
]
log_index = 0

# Simulated sensor state
base_temp = 22.0
base_humidity = 50.0


def signal_handler(signum, frame):
    """Handle Ctrl+C for graceful shutdown."""
    global running
    print("\nShutting down...")
    running = False


def read_temperature():
    """Simulate temperature reading."""
    global base_temp
    variation = random.uniform(-1.0, 1.0)
    base_temp += variation * 0.1
    base_temp = max(18.0, min(28.0, base_temp))
    return base_temp


def read_humidity():
    """Simulate humidity reading."""
    global base_humidity
    variation = random.uniform(-2.0, 2.0)
    base_humidity += variation * 0.2
    base_humidity = max(30.0, min(70.0, base_humidity))
    return base_humidity


def read_power_consumption():
    """Simulate power consumption."""
    base_power = 2.5  # 2.5W idle
    if led_state:
        base_power += 0.5  # LED adds 0.5W
    variation = random.uniform(0, 0.2)
    return base_power + variation


def get_log_message():
    """Get next rotating log message."""
    global log_index
    msg = log_messages[log_index].format(node_id)
    log_index = (log_index + 1) % len(log_messages)
    return msg


def main():
    global running, node_id, led_state
    
    # Parse command line arguments
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <node_id> [broker_host]")
        print(f"Example: {sys.argv[0]} py_dev_01 localhost")
        return 1
    
    node_id = sys.argv[1]
    broker_host = sys.argv[2] if len(sys.argv) > 2 else "localhost"
    
    print("=" * 60)
    print("  Hybrid Demo - Python Device")
    print("=" * 60)
    print(f"  Node ID: {node_id}")
    print(f"  Broker:  {broker_host}:1883")
    print("=" * 60)
    print()
    
    # Set log level
    set_log_level(LogLevel.WARN)
    
    # Set up signal handler
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # Seed random
    random.seed()
    
    # Track previous active device for change detection
    prev_active = ""
    
    try:
        with SdsNode(node_id, broker_host, port=1883) as node:
            print("Connected to MQTT broker")
            
            # Register as device with generated schema
            try:
                table = node.register_table("DeviceDemo", Role.DEVICE, schema=DeviceDemo)
                print("Registered as DEVICE for DeviceDemo table")
            except SdsError as e:
                print(f"Failed to register table: {e}")
                return 1
            
            # Config callback
            @node.on_config("DeviceDemo")
            def handle_config(table_type):
                nonlocal prev_active
                global led_state
                
                # Handle LED control
                new_led_state = table.config.led_control
                if new_led_state != led_state:
                    led_state = new_led_state
                    print(f"[LED] {'ON' if led_state else 'OFF'}")
                
                # Handle active device change
                active = table.config.active_device
                if active != prev_active:
                    prev_active = active
                    is_active = (active == node_id)
                    print(f"[ACTIVE] {active or 'none'} -> I am {'ACTIVE (will publish state)' if is_active else 'INACTIVE'}")
            
            print("\nDevice running. Press Ctrl+C to stop.\n")
            
            # Timing
            last_status_time = 0
            last_state_time = 0
            
            # Main loop
            while running:
                # Process MQTT messages
                node.poll(timeout_ms=0)
                
                now = time.time()
                
                # Publish state if we are the active device (every 1 second)
                if now - last_state_time >= 1.0:
                    last_state_time = now
                    
                    if table.config.active_device == node_id:
                        table.state.temperature = read_temperature()
                        table.state.humidity = read_humidity()
                        print(f"[STATE] temp={table.state.temperature:.1f}C, humidity={table.state.humidity:.1f}%")
                
                # Publish status (every 5 seconds)
                if now - last_status_time >= 5.0:
                    last_status_time = now
                    
                    table.status.power_consumption = read_power_consumption()
                    table.status.latest_log = get_log_message()
                    
                    print(f"[STATUS] power={table.status.power_consumption:.1f}W, log=\"{table.status.latest_log}\"")
                
                # Small delay to prevent CPU spinning
                time.sleep(0.1)
            
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
