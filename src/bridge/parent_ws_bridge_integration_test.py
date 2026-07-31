import os
from pathlib import Path
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


def register_worktree_repository(port: int, repository: Path, porcelain: str) -> None:
    registry_path = (
        Path(os.environ["TEST_TMPDIR"])
        / f"bridge-state-{port}"
        / "my-opiniated-editor"
        / "worktrees.pb"
    )
    subprocess.run(
        [
            runfile_path("src/parent/workspace_parent"),
            "--register-worktree-repository",
            str(registry_path),
            str(repository),
        ],
        env={
            **os.environ,
            "MOE_GIT_EXECUTABLE": runfile_path("test/fixtures/fake_git"),
            "MOE_FAKE_GIT_WORKTREE_LIST": porcelain,
        },
        check=True,
        capture_output=True,
        text=True,
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


class ParentWsBridgeIntegrationTest(unittest.TestCase):
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

    def test_tray_switch_control_message_changes_active_parent_tray(self):
        port = free_loopback_port()
        process = start_bridge(port)

        client = None
        try:
            wait_for_health(port, process)
            client = WebSocketClient(port)
            client.send_terminal_input(b"export MOE_TRAY_MARKER=tray_one\n")
            client.send_shell_marker("__moe_export_done__")
            client.read_terminal_output_until("__moe_export_done__")

            client.send_tray_switch(2)
            client.send_terminal_input(
                b"printf '__moe_tray2_%s__\\n' \"${MOE_TRAY_MARKER:-empty}\"\n"
            )
            tray_two_output = client.read_terminal_output_until("__moe_tray2_empty__")
            self.assertIn("__moe_tray2_empty__", tray_two_output)

            client.send_tray_switch(1)
            redraw_output = client.read_terminal_output_until("__moe_export_done__")
            self.assertIn("\x1b[H\x1b[2J", redraw_output)
            self.assertIn("__moe_export_done__", redraw_output)

            client.send_terminal_input(
                b"printf '__moe_tray1_%s__\\n' \"$MOE_TRAY_MARKER\"\n"
            )
            tray_one_output = client.read_terminal_output_until(
                "__moe_tray1_tray_one__"
            )
            self.assertIn("__moe_tray1_tray_one__", tray_one_output)
        finally:
            if client is not None:
                client.close()
            stop_bridge(process)

    def test_worktree_manager_control_opens_parent_overlay(self):
        port = free_loopback_port()
        process = start_bridge(port)

        client = None
        try:
            wait_for_health(port, process)
            client = WebSocketClient(port)
            client.read_parent_status_until({"commandMode": False})
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.open_worktree_manager()
            client.read_parent_status_until(
                {"commandMode": False, "overlay": "worktreeManagement"}
            )
            output = client.read_terminal_output_until("/anonymous/1")
            self.assertIn("\x1b[48;5;244mWorktrees\x1b[48;5;236m", output)
            self.assertIn("/anonymous/1", output)

            client.send_tray_switch(2)
            client.read_parent_status_until(
                {"trayKey": "anonymous:2", "overlay": "none"}
            )
            client.send_tray_switch(1)
            client.read_parent_status_until(
                {"trayKey": "anonymous:1", "overlay": "worktreeManagement"}
            )
            refreshed_picker = client.read_terminal_output_until("/anonymous/2")
            self.assertIn("/anonymous/1", refreshed_picker)
            self.assertIn("/anonymous/2", refreshed_picker)

            client.send_terminal_input(b"\t\t")
            repository_mode = client.read_terminal_output_until("Repository root:")
            self.assertIn(
                "\x1b[48;5;244mAdd repository\x1b[48;5;236m",
                repository_mode,
            )
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.open_worktree_manager()
            client.read_parent_status_until({"commandMode": True, "overlay": "none"})
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": False})
            client.send_shell_marker("__moe_after_worktree_manager_close__")
            shell_output = client.read_terminal_output_until(
                "__moe_after_worktree_manager_close__"
            )
            self.assertIn("__moe_after_worktree_manager_close__", shell_output)
        finally:
            if client is not None:
                client.close()
            stop_bridge(process)

    def test_command_mode_forwards_navigation_to_worktree_overlay(self):
        port = free_loopback_port()
        process = start_bridge(port)

        client = None
        try:
            wait_for_health(port, process)
            client = WebSocketClient(port)
            client.read_parent_status_until({"trayKey": "anonymous:1"})
            client.send_tray_switch(2)
            client.read_parent_status_until({"trayKey": "anonymous:2"})
            client.send_tray_switch(1)
            client.read_parent_status_until({"trayKey": "anonymous:1"})

            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.open_worktree_manager()
            client.read_parent_status_until(
                {"commandMode": False, "overlay": "worktreeManagement"}
            )
            client.read_terminal_output_until("/anonymous/2")
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})

            client.send_worktree_overlay_navigation("down")
            client.read_terminal_output_until("Preview: /anonymous/2")
            client.send_worktree_picker_command("c")
            confirmation = client.read_terminal_output_until("[y/N]")
            self.assertIn("Clear /anonymous/2? [y/N]", confirmation)
            client.send_worktree_picker_command("n")
            client.read_terminal_output_until("Worktree> ")

            client.send_worktree_overlay_navigation("tab")
            client.read_terminal_output_until("No registered repositories")
            client.send_worktree_overlay_navigation("backtab")
            client.read_terminal_output_until("Worktree> ")
        finally:
            if client is not None:
                client.close()
            stop_bridge(process)

    def test_selected_worktree_status_is_shared_with_other_clients(self):
        port = free_loopback_port()
        repository = Path(os.environ["TEST_TMPDIR"]) / f"repository-{port}"
        worktree = repository / "feature-status"
        (repository / ".bare").mkdir(parents=True)
        (repository / ".git").write_text("gitdir: ./.bare\n", encoding="utf-8")
        worktree.mkdir()
        (worktree / ".git").touch()
        repository = repository.resolve()
        worktree = worktree.resolve()

        porcelain = (
            f"worktree {repository / '.bare'}\n"
            "HEAD 111\n"
            "bare\n\n"
            f"worktree {worktree}\n"
            "HEAD 222\n"
            "branch refs/heads/main\n\n"
        )
        register_worktree_repository(port, repository, porcelain)
        process = start_bridge(
            port,
            extra_environment={
                "MOE_GIT_EXECUTABLE": runfile_path("test/fixtures/fake_git"),
                "MOE_FAKE_GIT_WORKTREE_LIST": porcelain,
            },
        )

        first_client = None
        second_client = None
        try:
            wait_for_health(port, process)
            first_client = WebSocketClient(port)
            second_client = WebSocketClient(port)
            first_client.read_parent_status_until({"trayKey": "anonymous:1"})
            second_client.read_parent_status_until({"trayKey": "anonymous:1"})
            first_client.open_worktree_manager()
            first_client.read_terminal_output_until("Worktree> ")
            first_client.send_terminal_input(b"\r")

            expected = {
                "commandMode": False,
                "trayKey": f"worktree:{worktree}",
                "trayLabel": f"worktree {worktree}",
            }
            first_client.read_parent_status_until(expected)
            second_client.read_parent_status_until(expected)
        finally:
            if first_client is not None:
                first_client.close()
            if second_client is not None:
                second_client.close()
            stop_bridge(process)

    def test_clear_picker_command_requires_command_mode_and_recreates_tray_one(self):
        port = free_loopback_port()
        process = start_bridge(port)

        client = None
        try:
            wait_for_health(port, process)
            client = WebSocketClient(port)
            client.read_parent_status_until({"commandMode": False})
            client.send_terminal_input(b"export MOE_CLEAR_MARKER=preserved\n")
            client.send_shell_marker("__moe_clear_exported__")
            client.read_terminal_output_until("__moe_clear_exported__")

            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.open_worktree_manager()
            client.read_parent_status_until(
                {"commandMode": False, "overlay": "worktreeManagement"}
            )
            client.read_terminal_output_until("/anonymous/1")

            client.send_worktree_picker_command("c")
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.open_worktree_manager()
            client.read_parent_status_until({"commandMode": True, "overlay": "none"})
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": False})
            client.send_terminal_input(
                b"printf '__moe_before_clear_%s__\\n' \"$MOE_CLEAR_MARKER\"\n"
            )
            client.read_terminal_output_until("__moe_before_clear_preserved__")

            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.open_worktree_manager()
            client.read_parent_status_until({"commandMode": False})
            client.read_terminal_output_until("/anonymous/1")
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.send_worktree_picker_command("c")
            client.read_terminal_output_until("Clear /anonymous/1? [y/N]")
            client.send_worktree_picker_command("n")
            client.read_terminal_output_until("Worktree> ")
            client.send_worktree_picker_command("c")
            client.read_terminal_output_until("Clear /anonymous/1? [y/N]")
            client.send_worktree_picker_command("y")
            client.read_parent_status_until(
                {
                    "commandMode": True,
                    "trayKey": "anonymous:1",
                    "overlay": "none",
                }
            )

            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": False})
            client.send_terminal_input(
                b"printf '__moe_after_clear_%s__\\n' " b'"${MOE_CLEAR_MARKER:-empty}"\n'
            )
            client.read_terminal_output_until("__moe_after_clear_empty__")
        finally:
            if client is not None:
                client.close()
            stop_bridge(process)

    def test_clearing_inactive_tray_one_defers_recreation_until_active_tray_dies(
        self,
    ):
        port = free_loopback_port()
        process = start_bridge(port)

        client = None
        try:
            wait_for_health(port, process)
            client = WebSocketClient(port)
            client.read_parent_status_until({"trayKey": "anonymous:1"})
            client.send_tray_switch(2)
            client.read_parent_status_until({"trayKey": "anonymous:2"})
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.open_worktree_manager()
            client.read_parent_status_until(
                {"commandMode": False, "overlay": "worktreeManagement"}
            )
            client.read_terminal_output_until("/anonymous/2")
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})

            client.send_worktree_picker_command("c")
            client.read_terminal_output_until("Clear /anonymous/1? [y/N]")
            client.send_worktree_picker_command("y")
            client.read_parent_status_until(
                {
                    "commandMode": True,
                    "trayKey": "anonymous:2",
                    "overlay": "worktreeManagement",
                }
            )
            refreshed_picker = client.read_terminal_output_until("/anonymous/2")
            self.assertNotIn("/anonymous/1", refreshed_picker)

            client.send_worktree_picker_command("c")
            client.read_terminal_output_until("Clear /anonymous/2? [y/N]")
            client.send_worktree_picker_command("y")
            client.read_parent_status_until(
                {
                    "commandMode": True,
                    "trayKey": "anonymous:1",
                    "overlay": "none",
                }
            )
        finally:
            if client is not None:
                client.close()
            stop_bridge(process)

    def test_confirmed_remove_purges_worktree_and_broadcasts_fallback(self):
        port = free_loopback_port()
        repository = Path(os.environ["TEST_TMPDIR"]) / f"remove-repository-{port}"
        worktree = repository / "topic"
        (repository / ".bare").mkdir(parents=True)
        (repository / ".git").write_text("gitdir: ./.bare\n", encoding="utf-8")
        worktree.mkdir()
        (worktree / ".git").touch()
        repository = repository.resolve()
        worktree = worktree.resolve()
        porcelain = (
            f"worktree {repository / '.bare'}\n"
            "HEAD 111\n"
            "bare\n\n"
            f"worktree {worktree}\n"
            "HEAD 222\n"
            "branch refs/heads/topic\n\n"
        )
        register_worktree_repository(port, repository, porcelain)
        process = start_bridge(
            port,
            extra_environment={
                "MOE_GIT_EXECUTABLE": runfile_path("test/fixtures/fake_git"),
                "MOE_FAKE_GIT_WORKTREE_LIST": porcelain,
            },
        )

        first_client = None
        second_client = None
        try:
            wait_for_health(port, process)
            first_client = WebSocketClient(port)
            second_client = WebSocketClient(port)
            first_client.read_parent_status_until({"trayKey": "anonymous:1"})
            second_client.read_parent_status_until({"trayKey": "anonymous:1"})

            first_client.toggle_command_mode()
            first_client.read_parent_status_until({"commandMode": True})
            first_client.open_worktree_manager()
            first_client.read_parent_status_until({"commandMode": False})
            first_client.read_terminal_output_until("Worktree> ")
            first_client.send_terminal_input(b"\r")
            worktree_status = {
                "commandMode": False,
                "trayKey": f"worktree:{worktree}",
            }
            first_client.read_parent_status_until(worktree_status)
            second_client.read_parent_status_until(worktree_status)

            first_client.toggle_command_mode()
            first_client.read_parent_status_until({"commandMode": True})
            first_client.open_worktree_manager()
            first_client.read_parent_status_until({"commandMode": False})
            first_client.read_terminal_output_until("Worktree> ")
            first_client.toggle_command_mode()
            first_client.read_parent_status_until({"commandMode": True})
            first_client.send_worktree_picker_command("c")
            clear_confirmation = first_client.read_terminal_output_until("[y/N]")
            self.assertIn("Clear ", clear_confirmation)
            first_client.send_worktree_picker_command("y")
            clear_fallback = {
                "commandMode": True,
                "trayKey": "anonymous:1",
                "overlay": "none",
            }
            first_client.read_parent_status_until(clear_fallback)
            second_client.read_parent_status_until(clear_fallback)
            self.assertTrue(worktree.exists())

            first_client.open_worktree_manager()
            first_client.read_parent_status_until({"commandMode": False})
            retained_picker = first_client.read_terminal_output_until("Worktree> ")
            self.assertIn("topic", retained_picker)
            first_client.send_terminal_input(b"\r")
            first_client.read_parent_status_until(worktree_status)
            second_client.read_parent_status_until(worktree_status)

            first_client.toggle_command_mode()
            first_client.read_parent_status_until({"commandMode": True})
            first_client.open_worktree_manager()
            first_client.read_parent_status_until({"commandMode": False})
            first_client.read_terminal_output_until("Worktree> ")
            first_client.toggle_command_mode()
            first_client.read_parent_status_until({"commandMode": True})
            first_client.send_worktree_picker_command("r")
            confirmation = first_client.read_terminal_output_until("[y/N]")
            self.assertIn("Remove ", confirmation)
            self.assertIn("topic? [y/N]", confirmation)

            first_client.send_worktree_picker_command("n")
            first_client.read_terminal_output_until("Worktree> ")
            self.assertTrue(worktree.exists())

            first_client.send_worktree_picker_command("r")
            first_client.read_terminal_output_until("[y/N]")
            first_client.send_worktree_picker_command("y")
            fallback = {
                "commandMode": True,
                "trayKey": "anonymous:1",
                "trayLabel": "tray 1",
                "overlay": "none",
            }
            first_client.read_parent_status_until(fallback)
            second_client.read_parent_status_until(fallback)
            self.assertFalse(worktree.exists())

            first_client.open_worktree_manager()
            first_client.read_parent_status_until(
                {"commandMode": False, "overlay": "worktreeManagement"}
            )
            picker = first_client.read_terminal_output_until("/anonymous/1")
            self.assertNotIn(str(worktree), picker)
        finally:
            if first_client is not None:
                first_client.close()
            if second_client is not None:
                second_client.close()
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
