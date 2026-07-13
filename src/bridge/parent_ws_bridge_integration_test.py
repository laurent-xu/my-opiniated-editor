import os
import subprocess
import sys
import urllib.error
import unittest

sys.path.append(os.path.dirname(__file__))

from parent_ws_bridge_test_lib import (
    WebSocketClient,
    fetch_text,
    free_loopback_port,
    runfile_path,
    start_bridge,
    stop_bridge,
    wait_for_health,
)


def assert_browser_assets(test_case: unittest.TestCase, port: int):
    html = fetch_text(port, "/")
    test_case.assertIn("@xterm/xterm@6.0.0/css/xterm.css", html)
    test_case.assertIn("@xterm/xterm@6.0.0/lib/xterm.js", html)
    test_case.assertIn("@xterm/addon-fit@0.11.0/lib/addon-fit.js", html)
    test_case.assertNotIn("@xterm/xterm/css/xterm.css", html)
    test_case.assertNotIn("@xterm/xterm/lib/xterm.js", html)
    test_case.assertNotIn("@xterm/addon-fit/lib/addon-fit.js", html)
    test_case.assertIn("/client.js", html)

    client_js = fetch_text(port, "/client.js")
    test_case.assertTrue(client_js.startswith("(() => {"), client_js[:32])
    test_case.assertTrue(client_js.rstrip().endswith("})();"))
    test_case.assertIn("new Terminal", client_js)
    test_case.assertIn(
        "new WebSocket(`${protocol}//${window.location.host}${websocketPath}`",
        client_js,
    )
    test_case.assertIn("new URLSearchParams(window.location.search)", client_js)
    test_case.assertIn("terminal.attachCustomKeyEventHandler", client_js)
    test_case.assertIn('event.key === "Tab"', client_js)
    test_case.assertIn(
        'sendCommand("0", event.shiftKey ? "\\x1b[Z" : "\\t")', client_js
    )

    css = fetch_text(port, "/style.css")
    test_case.assertIn("#terminal", css)


class ParentWsBridgeIntegrationTest(unittest.TestCase):
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

    def test_token_protects_http_and_websocket_endpoints(self):
        port = free_loopback_port()
        process = start_bridge(port, extra_args=["--token", "devsecret"])

        client = None
        try:
            health = wait_for_health(port, process, token="devsecret")
            self.assertTrue(health["ok"])

            with self.assertRaises(urllib.error.HTTPError) as caught:
                fetch_text(port, "/health")
            self.assertEqual(caught.exception.code, 401)

            html = fetch_text(port, "/?token=devsecret")
            self.assertIn("/client.js", html)

            client = WebSocketClient(port, token="devsecret")
            client.send_shell_marker("__moe_token_client__")
            output = client.read_terminal_output_until("__moe_token_client__")
            self.assertIn("__moe_token_client__", output)
        finally:
            if client is not None:
                client.close()
            stop_bridge(process)

    def test_network_bind_requires_token_or_explicit_unsafe_override(self):
        process = subprocess.Popen(
            [
                runfile_path("src/bridge/parent_ws_bridge"),
                "--interface",
                "0.0.0.0",
                "--port",
                "0",
                "--parent",
                runfile_path("src/parent/workspace_parent"),
                "--cwd",
                os.environ["TEST_TMPDIR"],
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        stdout, stderr = process.communicate(timeout=5)

        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(stdout, "")
        self.assertIn("network bind requires --token", stderr)


if __name__ == "__main__":
    unittest.main()
