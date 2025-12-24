import logging
from homeassistant.components.select import SelectEntity
from homeassistant.config_entries import ConfigEntry, ConfigEntryState
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .const import (
    DOMAIN,
    CONF_NODE_ID,
    CONF_ENDPOINT_ID,
    VENDOR_CLUSTER_ID,
    ATTR_STATE_ID,
    ATTR_MODE_ID,
    STATE_MAP,
    MODE_MAP,
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
    matter_entries = hass.config_entries.async_entries("matter")
    for m_entry in matter_entries:
        if m_entry.state == ConfigEntryState.LOADED:
            if hasattr(m_entry, "runtime_data") and hasattr(m_entry.runtime_data, "matter_client"):
                matter_client = m_entry.runtime_data.matter_client
                break
            matter_data = hass.data.get("matter", {})
            m_data = matter_data.get(m_entry.entry_id)
            if m_data and hasattr(m_data, "matter_client"):
                matter_client = m_data.matter_client
                break

    if not matter_client:
        matter_data = hass.data.get("matter", {})
        if isinstance(matter_data, dict):
            for val in matter_data.values():
                if hasattr(val, "matter_client"):
                    matter_client = val.matter_client
                    break

    if not matter_client:
        return

    async_add_entities([
        HaierSelect(matter_client, node_id, endpoint_id, ATTR_STATE_ID, "Haier State", STATE_MAP),
        HaierSelect(matter_client, node_id, endpoint_id, ATTR_MODE_ID, "Haier Mode", MODE_MAP),
    ])

class HaierSelect(SelectEntity):
    _attr_should_poll = False

    def __init__(self, client, node_id, endpoint_id, attr_id, name, mapping):
        self._client = client
        self._node_id = node_id
        self._endpoint_id = endpoint_id
        self._attr_id = attr_id
        self._name = name
        self._mapping = mapping
        self._reverse_mapping = {v: k for k, v in mapping.items()}
        self._attr_options = list(mapping.values())
        self._attr_unique_id = f"haier_select_{node_id}_{endpoint_id}_{attr_id}"
        self._current_option = None

    @property
    def name(self): return self._name

    @property
    def current_option(self): return self._current_option

    async def async_select_option(self, option: str) -> None:
        val = self._reverse_mapping.get(option)
        if val is None: return
        try:
            await self._client.write_attribute(self._node_id, self._endpoint_id, VENDOR_CLUSTER_ID, self._attr_id, val)
            self._current_option = option
            self.async_write_ha_state()
        except Exception as err:
            _LOGGER.error("Haier Bridge: Write failed for %s: %s", self._name, err)

    async def async_added_to_hass(self):
        try:
            await self._client.subscribe_attribute(
                self._node_id, self._endpoint_id, VENDOR_CLUSTER_ID, self._attr_id, self._handle_update,
            )
            val = await self._client.read_attribute(self._node_id, self._endpoint_id, VENDOR_CLUSTER_ID, self._attr_id)
            self._current_option = self._mapping.get(val)
            self.async_write_ha_state()
        except Exception as err:
            _LOGGER.error("Haier Bridge: Subscription failed for %s: %s", self._name, err)

    def _handle_update(self, node_id, endpoint_id, cluster_id, attribute_id, value):
        if cluster_id == VENDOR_CLUSTER_ID and attribute_id == self._attr_id:
            self._current_option = self._mapping.get(value)
            self.async_write_ha_state()
