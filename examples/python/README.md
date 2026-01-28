# Python Examples

These examples demonstrate how to use the SDS Python library.

## Prerequisites

1. **MQTT Broker**: Install and run Mosquitto or another MQTT broker:
   ```bash
   # macOS
   brew install mosquitto
   brew services start mosquitto
   
   # Ubuntu
   sudo apt-get install mosquitto
   sudo systemctl start mosquitto
   ```

2. **SDS Python Library**: Build and install the library:
   ```bash
   cd python
   pip install -e .
   ```

3. **Generated Types**: Make sure the C library is built with the generated
   types that include "SensorData" (from the example schema.sds).

## Examples

### simple_device.py

A simulated sensor device that:
- Connects to the MQTT broker
- Registers as a DEVICE for the SensorData table
- Receives configuration updates from the owner
- Publishes simulated sensor readings

```bash
python simple_device.py localhost my_sensor_01
```

### simple_owner.py

A fleet manager/owner that:
- Connects to the MQTT broker
- Registers as OWNER for the SensorData table
- Receives state and status updates from devices
- Tracks which devices are online

```bash
python simple_owner.py localhost my_owner_01
```

## Running Both Together

In one terminal, start the owner:
```bash
python simple_owner.py
```

In another terminal, start one or more devices:
```bash
python simple_device.py localhost sensor_01
python simple_device.py localhost sensor_02
python simple_device.py localhost sensor_03
```

The owner will show status updates as devices connect and publish data.

## Interoperability with C

These Python examples are fully interoperable with C-based SDS nodes:

- A Python owner can manage C-based devices
- A C owner can manage Python-based devices
- You can mix Python and C devices in the same fleet

This works because the Python wrapper uses the same C library under the hood,
ensuring identical protocol behavior.
