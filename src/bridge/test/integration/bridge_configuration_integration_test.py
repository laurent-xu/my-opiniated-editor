import os
from pathlib import Path
import subprocess
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


class BridgeConfigurationIntegrationTest(unittest.TestCase):
    def test_bridge_instances_do_not_share_worktree_registry_state(self):
        first_port = free_loopback_port()
        second_port = free_loopback_port()
        while second_port == first_port:
            second_port = free_loopback_port()
        repository = (
            Path(os.environ["TEST_TMPDIR"]) / f"isolated-repository-{first_port}"
        )
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
        register_worktree_repository(first_port, repository, porcelain)
        environment = {
            "MOE_GIT_EXECUTABLE": runfile_path("test/fixtures/fake_git"),
            "MOE_FAKE_GIT_WORKTREE_LIST": porcelain,
        }
        first_process = start_bridge(first_port, extra_environment=environment)
        second_process = start_bridge(second_port, extra_environment=environment)

        first_client = None
        second_client = None
        try:
            wait_for_health(first_port, first_process)
            wait_for_health(second_port, second_process)
            first_client = WebSocketClient(first_port)
            second_client = WebSocketClient(second_port)
            first_client.read_parent_status_until({"trayKey": "anonymous:1"})
            second_client.read_parent_status_until({"trayKey": "anonymous:1"})

            first_client.toggle_command_mode()
            first_client.read_parent_status_until({"commandMode": True})
            first_client.open_worktree_manager()
            first_client.read_parent_status_until(
                {"commandMode": False, "overlay": "worktreeManagement"}
            )
            first_output = first_client.read_terminal_output_until("/anonymous/1")
            worktree_suffix = f"{repository.name}/topic"
            self.assertIn(worktree_suffix, first_output)

            second_client.toggle_command_mode()
            second_client.read_parent_status_until({"commandMode": True})
            second_client.open_worktree_manager()
            second_client.read_parent_status_until(
                {"commandMode": False, "overlay": "worktreeManagement"}
            )
            second_output = second_client.read_terminal_output_until("/anonymous/1")
            self.assertNotIn(worktree_suffix, second_output)
        finally:
            if first_client is not None:
                first_client.close()
            if second_client is not None:
                second_client.close()
            stop_bridge(first_process)
            stop_bridge(second_process)

    def test_network_bind_rejects_non_loopback_interface(self):
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
                "--state-directory",
                str(Path(os.environ["TEST_TMPDIR"]) / "network-bind-state"),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        stdout, stderr = process.communicate(timeout=5)

        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(stdout, "")
        self.assertIn("--interface must be a loopback IPv4 address", stderr)

    def test_unauthenticated_network_override_is_not_accepted(self):
        process = subprocess.run(
            [
                runfile_path("src/bridge/parent_ws_bridge"),
                "--allow-unauthenticated-network",
            ],
            capture_output=True,
            text=True,
        )

        self.assertNotEqual(process.returncode, 0)
        self.assertEqual(process.stdout, "")
        self.assertIn(
            "unknown argument: --allow-unauthenticated-network", process.stderr
        )


if __name__ == "__main__":
    unittest.main()
