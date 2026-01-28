#!/usr/bin/env python3
"""
Hybrid Demo - Python Owner

This owner node controls C/ESP32 devices and displays their data.

Commands:
    led on/off      - Toggle LED on all devices
    active <id>     - Set which device publishes state (e.g., "active linux_dev_01")
    active none     - Disable state publishing from all devices
    status          - Show all device statuses
    quit            - Exit

Usage:
    python owner.py [broker_host] [node_id]
"""

import sys
import time
import signal
import threading

# Add the SDS Python package to path (relative to this script)
import os
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(script_dir, "../../../python"))

from sds import SdsNode, Role, SdsError, SdsMqttError, LogLevel, set_log_level

# Import generated schema types
from demo_types import DeviceDemo


# Global flags
running = True
verbose = False  # Control live state/status message printing


def signal_handler(signum, frame):
    """Handle Ctrl+C for graceful shutdown."""
    global running
    print("\nShutting down...")
    running = False


def print_help():
    """Print available commands."""
    print("\nCommands:")
    print("  led on/off      - Toggle LED on all devices")
    print("  active <id>     - Set active device (e.g., 'active linux_dev_01')")
    print("  active none     - Disable state publishing")
    print("  status          - Show all device statuses")
    print("  verbose on/off  - Toggle live state/status messages")
    print("  help            - Show this help")
    print("  quit            - Exit")
    print()


def main():
    global running
    
    # Parse command line arguments
    broker_host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    node_id = sys.argv[2] if len(sys.argv) > 2 else "py_owner"
    
    print("=" * 60)
    print("  Hybrid Demo - Python Owner")
    print("=" * 60)
    print(f"  Node ID: {node_id}")
    print(f"  Broker:  {broker_host}:1883")
    print("=" * 60)
    
    # Set log level
    set_log_level(LogLevel.WARN)
    
    # Set up signal handler
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        with SdsNode(node_id, broker_host, port=1883) as node:
            print("\nConnected to MQTT broker")
            
            # Register as owner with generated schema
            try:
                table = node.register_table("DeviceDemo", Role.OWNER, schema=DeviceDemo)
                print("Registered as OWNER for DeviceDemo table")
            except SdsError as e:
                print(f"Failed to register table: {e}")
                return 1
            
            # Set initial config
            table.config.led_control = 0  # LED off
            table.config.active_device = ""  # No active device
            print("\nInitial config: LED=OFF, active_device=none")
            
            # Track last state update
            last_state_from = None
            last_state_time = 0
            
            # State callback
            @node.on_state("DeviceDemo")
            def handle_state(table_type, from_node):
                nonlocal last_state_from, last_state_time
                last_state_from = from_node
                last_state_time = time.time()
                # Only print if verbose mode is on
                if verbose:
                    print(f"\n[STATE] from {from_node}: "
                          f"temp={table.state.temperature:.1f}C, "
                          f"humidity={table.state.humidity:.1f}%")
                    print("> ", end="", flush=True)
            
            # Status callback
            @node.on_status("DeviceDemo")
            def handle_status(table_type, from_node):
                # Only print if verbose mode is on
                if not verbose:
                    return
                device = table.get_device(from_node)
                if device and device.status:
                    status_str = "ONLINE" if device.online else "OFFLINE"
                    print(f"\n[STATUS] {from_node} ({status_str}): "
                          f"power={device.status.power_consumption:.1f}W, "
                          f"log=\"{device.status.latest_log}\"")
                    print("> ", end="", flush=True)
                else:
                    print(f"\n[STATUS] Received status update from {from_node}")
                    print("> ", end="", flush=True)
            
            # Error callback
            @node.on_error
            def handle_error(error_code, context):
                print(f"\n[ERROR] Code {error_code}: {context}")
                print("> ", end="", flush=True)
            
            print_help()
            print("Waiting for devices to connect...\n")
            
            # Command input thread
            def input_thread():
                global running
                while running:
                    try:
                        cmd = input("> ").strip().lower()
                        if not cmd:
                            continue
                        
                        parts = cmd.split()
                        command = parts[0]
                        
                        if command == "led":
                            if len(parts) < 2:
                                print("Usage: led on/off")
                            elif parts[1] == "on":
                                table.config.led_control = 1
                                print("LED set to ON")
                            elif parts[1] == "off":
                                table.config.led_control = 0
                                print("LED set to OFF")
                            else:
                                print("Usage: led on/off")
                        
                        elif command == "active":
                            if len(parts) < 2:
                                print(f"Current active device: '{table.config.active_device}' or none")
                                print("Usage: active <device_id> or active none")
                            elif parts[1] == "none":
                                table.config.active_device = ""
                                print("Active device cleared - no device will publish state")
                            else:
                                device_id = parts[1]
                                table.config.active_device = device_id
                                print(f"Active device set to: {device_id}")
                        
                        elif command == "status":
                            print(f"\n--- Device Status ---")
                            print(f"Config: LED={'ON' if table.config.led_control else 'OFF'}, "
                                  f"active='{table.config.active_device or 'none'}'")
                            
                            device_count = table.device_count
                            if device_count > 0:
                                print(f"Known devices: {device_count}")
                                for dev_id, device in table.iter_devices():
                                    status_str = "ONLINE" if device.online else "OFFLINE"
                                    if device.status:
                                        print(f"  - {dev_id}: {status_str}, "
                                              f"power={device.status.power_consumption:.1f}W, "
                                              f"log=\"{device.status.latest_log}\"")
                                    else:
                                        print(f"  - {dev_id}: {status_str}")
                            else:
                                print("No devices connected yet.")
                            
                            if last_state_from:
                                age = time.time() - last_state_time
                                print(f"\nLast state from {last_state_from} ({age:.1f}s ago):")
                                print(f"  temperature={table.state.temperature:.1f}C")
                                print(f"  humidity={table.state.humidity:.1f}%")
                            print()
                        
                        elif command == "verbose":
                            global verbose
                            if len(parts) < 2:
                                print(f"Verbose mode: {'ON' if verbose else 'OFF'}")
                                print("Usage: verbose on/off")
                            elif parts[1] == "on":
                                verbose = True
                                print("Verbose mode ON - live state/status messages enabled")
                            elif parts[1] == "off":
                                verbose = False
                                print("Verbose mode OFF - live messages disabled")
                            else:
                                print("Usage: verbose on/off")
                        
                        elif command == "help":
                            print_help()
                        
                        elif command == "quit" or command == "exit":
                            running = False
                            break
                        
                        else:
                            print(f"Unknown command: {command}. Type 'help' for commands.")
                    
                    except EOFError:
                        running = False
                        break
                    except Exception as e:
                        print(f"Error: {e}")
            
            # Start input thread
            input_t = threading.Thread(target=input_thread, daemon=True)
            input_t.start()
            
            # Main loop - poll for MQTT messages
            while running:
                node.poll(timeout_ms=0)
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
