import logging
from homeassistant.components.sensor import SensorEntity, SensorStateClass
from homeassistant.config_entries import ConfigEntry, ConfigEntryState
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import (
    DOMAIN,
    CONF_NODE_ID,
    CONF_ENDPOINT_ID,
    VENDOR_CLUSTER_ID,
    ATTR_COMP_HZ_ID,
    ATTR_COMP_TARGET_ID,
)

_LOGGER = logging.getLogger(__name__)

async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    node_id = entry.data[CONF_NODE_ID]
    endpoint_id = entry.data[CONF_ENDPOINT_ID]

    matter_client = None
    
    # 1. Try official lookup using ConfigEntryState enum
    matter_entries = hass.config_entries.async_entries("matter")
    for m_entry in matter_entries:
        if m_entry.state == ConfigEntryState.LOADED:
            # Path A: runtime_data (HA 2024.4+)
            if hasattr(m_entry, "runtime_data") and hasattr(m_entry.runtime_data, "matter_client"):
                matter_client = m_entry.runtime_data.matter_client
                break
            # Path B: hass.data dictionary
            matter_data = hass.data.get("matter", {})
            m_data = matter_data.get(m_entry.entry_id)
            if m_data and hasattr(m_data, "matter_client"):
                matter_client = m_data.matter_client
                break

    # 2. EMERGENCY FALLBACK: If still not found, search everything in hass.data['matter']
    if not matter_client:
        matter_data = hass.data.get("matter", {})
        if isinstance(matter_data, dict):
            for val in matter_data.values():
                if hasattr(val, "matter_client"):
                    matter_client = val.matter_client
                    break

    if not matter_client:
        _LOGGER.error("Haier Bridge: Matter client not found. Is the Matter integration running?")
        return

    async_add_entities([
        HaierHzSensor(matter_client, node_id, endpoint_id, ATTR_COMP_HZ_ID, "Compressor Frequency"),
        HaierHzSensor(matter_client, node_id, endpoint_id, ATTR_COMP_TARGET_ID, "Compressor Target Frequency"),
    ])

class HaierHzSensor(SensorEntity):
    _attr_native_unit_of_measurement = "Hz"
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_should_poll = False

    def __init__(self, client, node_id, endpoint_id, attr_id, name):
        self._client = client
        self._node_id = node_id
        self._endpoint_id = endpoint_id
        self._attr_id = attr_id
        self._name = name
        self._attr_unique_id = f"haier_sensor_{node_id}_{endpoint_id}_{attr_id}"
        self._state = None

    @property
    def name(self): return self._name

    @property
    def native_value(self): return self._state

    async def async_added_to_hass(self):
        try:
            await self._client.subscribe_attribute(
                self._node_id, self._endpoint_id, VENDOR_CLUSTER_ID, self._attr_id, self._handle_update,
            )
            val = await self._client.read_attribute(self._node_id, self._endpoint_id, VENDOR_CLUSTER_ID, self._attr_id)
            self._state = val
            self.async_write_ha_state()
        except Exception as err:
            _LOGGER.error("Haier Bridge: Failed to connect %s: %s", self._name, err)

    def _handle_update(self, node_id, endpoint_id, cluster_id, attribute_id, value):
        if cluster_id == VENDOR_CLUSTER_ID and attribute_id == self._attr_id:
            self._state = value
            self.async_write_ha_state()
