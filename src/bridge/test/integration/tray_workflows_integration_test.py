import os
from pathlib import Path
import sys
import unittest

sys.path.append(os.path.dirname(__file__))

from bridge_test_support import (
    WebSocketClient,
    free_loopback_port,
    register_worktree_repository,
    runfile_path,
    start_bridge,
    stop_bridge,
    wait_for_health,
)


class TrayWorkflowsIntegrationTest(unittest.TestCase):
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
            first_client.read_parent_status_until(
                {"commandMode": False, "overlay": "worktreeManagement"}
            )
            first_client.read_terminal_output_until("Worktree> ")
            first_client.toggle_command_mode()
            first_client.read_parent_status_until({"commandMode": True})
            first_client.send_worktree_overlay_navigation("enter")

            expected = {
                "commandMode": True,
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
            client.send_worktree_overlay_navigation("enter")
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


if __name__ == "__main__":
    unittest.main()
