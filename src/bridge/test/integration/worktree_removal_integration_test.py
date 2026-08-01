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


class WorktreeRemovalIntegrationTest(unittest.TestCase):
    def test_remove_of_bridge_startup_worktree_is_blocked(self):
        port = free_loopback_port()
        repository = Path(os.environ["TEST_TMPDIR"]) / f"protected-repository-{port}"
        worktree = repository / "main"
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
            cwd=worktree,
        )

        client = None
        try:
            wait_for_health(port, process)
            client = WebSocketClient(port)
            client.read_parent_status_until({"trayKey": "anonymous:1"})

            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})
            client.open_worktree_manager()
            client.read_parent_status_until({"commandMode": False})
            client.read_terminal_output_until("Worktree> ")
            client.toggle_command_mode()
            client.read_parent_status_until({"commandMode": True})

            client.send_worktree_picker_command("r")

            blocked = client.read_terminal_output_until(
                "Remove blocked: this worktree runs my-opiniated-editor"
            )
            self.assertNotIn("[y/N]", blocked)
            self.assertTrue(worktree.exists())
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
        git_log = repository / "git.log"
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
                "MOE_FAKE_GIT_LOG": str(git_log),
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
            invocations = git_log.read_text(encoding="utf-8")
            remove_position = invocations.find('"worktree", "remove"')
            branch_position = invocations.find(
                '"branch", "--delete", "--force", "--", "topic"'
            )
            self.assertGreaterEqual(remove_position, 0)
            self.assertGreater(branch_position, remove_position)

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


if __name__ == "__main__":
    unittest.main()
