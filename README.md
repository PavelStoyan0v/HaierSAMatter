## HaierSA Matter Vendor Cluster Notes

This firmware exposes standard Matter interfaces for core controls and a vendor-specific cluster for telemetry:

- **Target Temperature**: Standard `TemperatureControlledCabinet` setpoint.
- **State (OFF/HEAT/COOL)**: Exposed as a standard `TemperatureControlledCabinet` endpoint (Value 0=OFF, 1=HEAT, 2=COOL).
- **Mode (ECO/QUIET/TURBO)**: Exposed as a standard `TemperatureControlledCabinet` endpoint (Value 0=ECO, 1=QUIET, 2=TURBO).
- **Inlet/Outlet Temperature**: Standard `TemperatureSensor` endpoints.
- **Compressor Telemetry (Vendor ID: `0xFFF1`, Cluster ID: `0xFFF1FC01`)**:
  - `0x0003` CompressorFrequencyHz (uint16, read-only)  
  - `0x0004` TargetFrequencyHz (uint16, read-only)

### Home Assistant Integration
The custom component in `ha_haier_matter` is now only required for the **Compressor Frequency** sensors. State, Mode, and Temperature are handled natively by the standard Matter integration.

#### Installation
1. Copy the `custom_components/haier_matter` folder from `ha_haier_matter` into your Home Assistant `config/custom_components/` directory.
2. Restart Home Assistant.
3. Go to **Settings -> Devices & Services -> Add Integration**.
4. Search for "Haier Matter Bridge".
5. Enter the **Node ID** and **Endpoint ID** for your device.

Note: The **Endpoint ID** requested during setup is the one for the main Temperature control; the integration will find the telemetry on the same endpoint.

### Status LED (NeoPixel)
The onboard LED indicates the bridge and network status:

| Color | State | Meaning |
| :--- | :--- | :--- |
| **White (Pulsing)** | **Pairing** | Device is not commissioned. Waiting for Matter pairing. |
| **Orange** | **Offline** | Commissioned but lost connection to Matter network. |
| **Green (Dim)** | **Healthy** | Connected to Matter and receiving Modbus data. |
| **Magenta** | **Pending** | Matter command received, waiting for safe Modbus window. |
| **Yellow** | **Writing** | Actively sending command to Heat Pump (Relay isolated). |
| **Red (Blinking)** | **Error** | Hardware/Modbus timeout (No data for >10s). |

Notes:
- Keep using the standard TemperatureControlledCabinet setpoint for target temperature; no vendor attribute is needed for temperature.
- State/Mode writes remain single-attempt with relay isolation; if a write fails the device reverts the attribute value.


