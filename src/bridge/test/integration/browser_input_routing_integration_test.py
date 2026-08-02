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


class BrowserInputRoutingIntegrationTest(unittest.TestCase):
    def test_command_mode_routes_pane_actions_to_the_parent_layout(self):
        port = free_loopback_port()
        process = start_bridge(port)

        client = None
        try:
            wait_for_health(port, process)
            client = WebSocketClient(port)
            client.read_parent_status_until({"commandMode": False})
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})

            client.send_pane_action("splitLeftToRight")
            redraw = client.read_terminal_output_until("\x1b[0;48;5;240m")
            self.assertIn("\x1b[0;48;5;240m", redraw)

            client.send_pane_action("close")
            client.read_parent_status_until({"commandMode": True})
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


if __name__ == "__main__":
    unittest.main()
