## HaierSA Matter Vendor Cluster Notes

This firmware exposes a vendor-specific server cluster on the same endpoint as the TemperatureControlledCabinet:

- Vendor ID: `0xFFF1`
- Cluster ID: `0xFC01`
- Attributes  
  - `0x0001` State (enum8) — `0:OFF`, `1:HEAT`, `2:COOL` (writeable)  
  - `0x0002` Mode (enum8) — `0:ECO`, `1:QUIET`, `2:TURBO` (writeable)  
  - `0x0003` CompressorFrequencyHz (uint16, read-only)  
  - `0x0004` TargetFrequencyHz (uint16, read-only)

The normal TemperatureControlledCabinet setpoint remains for target temperature. Inlet/outlet temperatures use standard TemperatureSensor endpoints.

### Home Assistant Custom Integration
This project includes a Home Assistant custom component in the `ha_haier_matter` folder. 

#### Installation
1. Copy the `custom_components/haier_matter` folder from `ha_haier_matter` into your Home Assistant `config/custom_components/` directory.
2. Restart Home Assistant.
3. Go to **Settings -> Devices & Services -> Add Integration**.
4. Search for "Haier Matter Bridge".
5. Enter the **Node ID** and **Endpoint ID** for your device.

The integration will create:
- **Select** entities for **State** (OFF/HEAT/COOL) and **Mode** (ECO/QUIET/TURBO).
- **Sensor** entities for **Compressor Frequency** and **Compressor Target Frequency**.

Note: The standard **Target Temperature** setpoint and **Inlet/Outlet** temperatures are handled by the native Matter integration.

Notes:
- Keep using the standard TemperatureControlledCabinet setpoint for target temperature; no vendor attribute is needed for temperature.
- State/Mode writes remain single-attempt with relay isolation; if a write fails the device reverts the attribute value.


