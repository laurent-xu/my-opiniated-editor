import os
import sys
import unittest

sys.path.append(os.path.dirname(__file__))

from bridge_test_support import (
    WebSocketClient,
    fetch_text,
    free_loopback_port,
    start_bridge,
    stop_bridge,
    wait_for_health,
)
from browser_asset_assertions import assert_browser_assets


class BrowserReconnectIntegrationTest(unittest.TestCase):
    def test_reconnect_uses_same_parent_process(self):
        port = free_loopback_port()
        process = start_bridge(port)

        first_client = None
        second_client = None
        third_client = None
        try:
            health = wait_for_health(port, process)
            expected_pid = health["parentPid"]
            assert_browser_assets(self, port)

            first_client = WebSocketClient(port)
            first_client.send_shell_marker("__moe_first_client__")
            first_output = first_client.read_terminal_output_until(
                "__moe_first_client__"
            )
            self.assertIn("__moe_first_client__", first_output)
            html_during_open_websocket = fetch_text(port, "/")
            self.assertIn("@xterm/xterm", html_during_open_websocket)
            first_client.close()
            first_client = None

            health_after_reconnect = wait_for_health(port, process)
            self.assertEqual(health_after_reconnect["parentPid"], expected_pid)

            second_client = WebSocketClient(port)
            replayed_output = second_client.read_terminal_output_until(
                "__moe_first_client__"
            )
            self.assertIn("__moe_first_client__", replayed_output)
            third_client = WebSocketClient(port)
            second_client.send_shell_marker("__moe_broadcast__")

            second_output = second_client.read_terminal_output_until(
                "__moe_broadcast__"
            )
            third_output = third_client.read_terminal_output_until("__moe_broadcast__")
            self.assertIn("__moe_broadcast__", second_output)
            self.assertIn("__moe_broadcast__", third_output)
        finally:
            if first_client is not None:
                first_client.close()
            if second_client is not None:
                second_client.close()
            if third_client is not None:
                third_client.close()
            stop_bridge(process)


if __name__ == "__main__":
    unittest.main()
