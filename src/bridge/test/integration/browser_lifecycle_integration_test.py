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


def assert_browser_assets(test_case: unittest.TestCase, port: int):
    expected_font_stack = (
        '"JetBrainsMono Nerd Font Mono", "JetBrainsMono NFM", '
        '"JetBrainsMonoNL Nerd Font Mono", "JetBrainsMonoNL NFM", '
        '"MesloLGS NF", "FiraCode Nerd Font Mono", "Hack Nerd Font Mono", '
        '"CaskaydiaCove Nerd Font Mono", "Symbols Nerd Font Mono", '
        '"PowerlineSymbols", "DejaVu Sans Mono for Powerline", '
        '"DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono", '
        '"Cascadia Mono", "JetBrains Mono", monospace'
    )

    html = fetch_text(port, "/")
    test_case.assertIn("@xterm/xterm@6.0.0/css/xterm.css", html)
    test_case.assertIn("@xterm/xterm@6.0.0/lib/xterm.js", html)
    test_case.assertIn("@xterm/addon-fit@0.11.0/lib/addon-fit.js", html)
    test_case.assertIn("@xterm/addon-webgl@0.19.0/lib/addon-webgl.js", html)
    test_case.assertNotIn("@xterm/xterm/css/xterm.css", html)
    test_case.assertNotIn("@xterm/xterm/lib/xterm.js", html)
    test_case.assertNotIn("@xterm/addon-fit/lib/addon-fit.js", html)
    test_case.assertNotIn("@xterm/addon-canvas", html)
    test_case.assertNotIn("@xterm/addon-webgl/lib/addon-webgl.js", html)
    test_case.assertIn("/client.js", html)

    client_js = fetch_text(port, "/client.js")
    test_case.assertTrue(client_js.startswith("(async () => {"), client_js[:32])
    test_case.assertTrue(client_js.rstrip().endswith("})();"))
    test_case.assertIn("async function awaitTerminalFont()", client_js)
    test_case.assertIn(
        'document.fonts.load(\'14px "JetBrainsMono Nerd Font Mono"\', "\\ue0b0")',
        client_js,
    )
    test_case.assertIn("await document.fonts.ready", client_js)
    test_case.assertIn("await awaitTerminalFont()", client_js)
    test_case.assertIn("new Terminal", client_js)
    test_case.assertIn("customGlyphs: true", client_js)
    test_case.assertIn(f"fontFamily: '{expected_font_stack}'", client_js)
    test_case.assertIn("lineHeight: 1.15", client_js)
    test_case.assertIn("new WebglAddon.WebglAddon()", client_js)
    test_case.assertIn("terminal.loadAddon(webglAddon)", client_js)
    test_case.assertIn('console.warn("WebGL terminal renderer unavailable"', client_js)
    test_case.assertIn(
        "new WebSocket(`${protocol}//${window.location.host}${websocketPath}`",
        client_js,
    )
    test_case.assertIn("new URLSearchParams(window.location.search)", client_js)
    test_case.assertIn(
        "const payload = decoder.decode(bytes.slice(1), { stream: true })",
        client_js,
    )
    test_case.assertIn(
        "applyParentStatus(JSON.parse(statusDecoder.decode(bytes.slice(1))))",
        client_js,
    )
    test_case.assertIn("const pendingPayload = decoder.decode()", client_js)
    test_case.assertIn("terminal.attachCustomKeyEventHandler", client_js)
    test_case.assertIn('document.addEventListener("keydown"', client_js)
    test_case.assertIn('let activeTrayLabel = "tray 1"', client_js)
    test_case.assertIn("let commandMode = false", client_js)
    test_case.assertNotIn("let worktreeManagerOpen", client_js)
    test_case.assertIn('parts.push("command")', client_js)
    test_case.assertIn(
        'sendCommand("2", JSON.stringify({ tray: trayNumber }))', client_js
    )
    test_case.assertNotIn("activeTrayNumber", client_js)
    test_case.assertNotIn("setCommandMode", client_js)
    test_case.assertIn("activeTrayLabel = status.trayLabel", client_js)
    test_case.assertIn('event.key === "Escape"', client_js)
    test_case.assertIn('sendCommand("5", "")', client_js)
    test_case.assertIn('event.key === "Shift"', client_js)
    test_case.assertIn("isModifierOnlyKey(event)", client_js)
    test_case.assertIn('/^Digit([1-9])$/.exec(event.code || "")', client_js)
    test_case.assertNotIn('event.code === "KeyT" || event.key === "T"', client_js)
    test_case.assertIn('event.code === "KeyW" || event.key === "W"', client_js)
    test_case.assertIn('event.code === "KeyC" || event.key === "C"', client_js)
    test_case.assertIn('event.code === "KeyR" || event.key === "R"', client_js)
    test_case.assertIn('sendCommand("3", "")', client_js)
    test_case.assertIn("toggleWorktreeManager()", client_js)
    test_case.assertIn('sendCommand("6", command)', client_js)
    test_case.assertIn('sendCommand("7", navigation)', client_js)
    test_case.assertIn('sendWorktreePickerCommand("c")', client_js)
    test_case.assertIn('sendWorktreePickerCommand("r")', client_js)
    test_case.assertIn('ArrowUp: "up"', client_js)
    test_case.assertIn('ArrowDown: "down"', client_js)
    test_case.assertIn('return "enter"', client_js)
    test_case.assertIn('return event.shiftKey ? "backtab" : "tab"', client_js)
    test_case.assertNotIn('sendCommand("4", "")', client_js)
    test_case.assertNotIn("toggleWorktreePicker()", client_js)
    test_case.assertNotIn('sendCommand("0", "\\x03")', client_js)
    test_case.assertNotIn("tray find not implemented", client_js)
    test_case.assertIn('event.key === "Tab"', client_js)
    test_case.assertIn('event.code === "Tab"', client_js)
    test_case.assertIn("event.keyCode === 9", client_js)
    test_case.assertIn("terminalShouldReceiveKey()", client_js)
    test_case.assertIn("event.stopImmediatePropagation()", client_js)
    test_case.assertIn(
        'sendCommand("0", event.shiftKey ? "\\x1b[Z" : "\\t")', client_js
    )

    css = fetch_text(port, "/style.css")
    test_case.assertIn("#terminal", css)
    test_case.assertNotIn('@font-face {\n  font-family: "Moe Terminal Nerd Font"', css)
    test_case.assertIn(f"font-family: {expected_font_stack};", css)


class BrowserLifecycleIntegrationTest(unittest.TestCase):
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

            reconnected_client = WebSocketClient(port)
            reconnected_client.read_parent_status_until(tray_status)
        finally:
            if first_client is not None:
                first_client.close()
            if second_client is not None:
                second_client.close()
            if reconnected_client is not None:
                reconnected_client.close()
            stop_bridge(process)

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
