import os
import sys
import unittest

sys.path.append(os.path.dirname(__file__))

from bridge_test_support import (
    WebSocketClient,
    free_loopback_port,
    start_bridge,
    stop_bridge,
    wait_for_health,
)


class BrowserParentStatusIntegrationTest(unittest.TestCase):
    def test_parent_status_is_shared_and_replayed_to_all_clients(self):
        port = free_loopback_port()
        process = start_bridge(port)

        first_client = None
        second_client = None
        reconnected_client = None
        try:
            wait_for_health(port, process)
            first_client = WebSocketClient(port)
            second_client = WebSocketClient(port)
            initial_status = {
                "commandMode": False,
                "trayKey": "anonymous:1",
                "trayLabel": "tray 1",
                "paneMode": "none",
                "paneSelectedNodes": 0,
            }
            first_client.read_parent_status_until(initial_status)
            second_client.read_parent_status_until(initial_status)

            first_client.toggle_command_mode()
            command_status = {"commandMode": True, "trayKey": "anonymous:1"}
            first_client.read_parent_status_until(command_status)
            second_client.read_parent_status_until(command_status)

            second_client.send_tray_switch(2)
            tray_status = {
                "commandMode": True,
                "trayKey": "anonymous:2",
                "trayLabel": "tray 2",
            }
            first_client.read_parent_status_until(tray_status)
            second_client.read_parent_status_until(tray_status)

            second_client.send_pane_action("splitLeftToRight")
            second_client.send_pane_action("toggleSelectionOrSwap")
            pane_status = {
                **tray_status,
                "paneMode": "selection",
                "paneSelectedNodes": 1,
            }
            first_client.read_parent_status_until(pane_status)
            second_client.read_parent_status_until(pane_status)

            reconnected_client = WebSocketClient(port)
            reconnected_client.read_parent_status_until(pane_status)
        finally:
            if first_client is not None:
                first_client.close()
            if second_client is not None:
                second_client.close()
            if reconnected_client is not None:
                reconnected_client.close()
            stop_bridge(process)


if __name__ == "__main__":
    unittest.main()
