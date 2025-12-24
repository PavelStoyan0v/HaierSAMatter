import logging
import voluptuous as vol
from homeassistant import config_entries
from .const import DOMAIN, CONF_NODE_ID, CONF_ENDPOINT_ID

_LOGGER = logging.getLogger(__name__)

class HaierMatterConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        errors = {}
        
        # Try to find the Matter client to list devices
        matter_client = None
        matter_data = self.hass.data.get("matter", {})
        for adapter in matter_data.values():
            if hasattr(adapter, "matter_client"):
                matter_client = adapter.matter_client
                break
        
        devices = {}
        if matter_client:
            try:
                # Fetch all commissioned nodes
                nodes = await matter_client.get_nodes()
                for node in nodes:
                    # Filter for likely candidates or just list all
                    name = node.device_info.node_label if node.device_info else f"Node {node.node_id}"
                    devices[node.node_id] = f"{name} (ID: {node.node_id})"
            except Exception as err:
                _LOGGER.error("Failed to list Matter nodes: %s", err)

        if user_input is not None:
            return self.async_create_entry(
                title=f"Haier Bridge ({devices.get(user_input[CONF_NODE_ID], f'Node {user_input[CONF_NODE_ID]}')})",
                data=user_input
            )

        # If we found devices, show a dropdown. Otherwise, fallback to text input.
        if devices:
            data_schema = vol.Schema({
                vol.Required(CONF_NODE_ID): vol.In(devices),
                vol.Required(CONF_ENDPOINT_ID, default=1): int,
            })
        else:
            data_schema = vol.Schema({
                vol.Required(CONF_NODE_ID): int,
                vol.Required(CONF_ENDPOINT_ID, default=1): int,
            })

        return self.async_show_form(
            step_id="user",
            data_schema=data_schema,
            errors=errors,
        )
