import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


def runfile_path(path: str) -> str:
    return os.path.join(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], path)


class ServiceScriptsTest(unittest.TestCase):
    def setUp(self):
        self.bash = shutil.which("bash")
        self.assertIsNotNone(self.bash, "bash must be available on PATH")

    def test_bridge_scripts_are_valid_bash(self):
        for script in [
            "tools/bridge/run_bridge.sh",
            "tools/bridge/restart_bridge.sh",
        ]:
            with self.subTest(script=script):
                subprocess.run(
                    [self.bash, "-n", runfile_path(script)],
                    check=True,
                )

    def test_bridge_scripts_require_port_arguments(self):
        invocations = [
            ("tools/bridge/run_bridge.sh", ["17682"]),
            ("tools/bridge/restart_bridge.sh", []),
        ]
        for script, arguments in invocations:
            with self.subTest(script=script):
                result = subprocess.run(
                    [self.bash, runfile_path(script), *arguments],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    check=False,
                )

                self.assertEqual(result.returncode, 2)
                self.assertIn("usage:", result.stderr)

    def test_runner_uses_loopback_upstream_for_public_port(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir) / "repo"
            runner = repo_root / "tools" / "bridge" / "run_bridge.sh"
            runner.parent.mkdir(parents=True)
            shutil.copyfile(runfile_path("tools/bridge/run_bridge.sh"), runner)
            runner.chmod(0o755)

            bridge = repo_root / "bazel-bin" / "src" / "bridge" / "parent_ws_bridge"
            bridge.parent.mkdir(parents=True)
            bridge.write_text(
                "#!/usr/bin/env bash\n"
                'printf "cwd=%s\\n" "$PWD" > "$MOE_TEST_LOG"\n'
                'printf "arg=%s\\n" "$@" >> "$MOE_TEST_LOG"\n',
                encoding="utf-8",
            )
            bridge.chmod(0o755)
            parent = repo_root / "bazel-bin" / "src" / "parent" / "workspace_parent"
            parent.parent.mkdir(parents=True)
            parent.touch()

            log_path = repo_root / "runner.log"
            subprocess.run(
                [self.bash, runner, "18765", "8765"],
                env={
                    **os.environ,
                    "XDG_STATE_HOME": str(repo_root / "state"),
                    "MOE_TEST_LOG": str(log_path),
                },
                check=True,
            )

            self.assertEqual(
                log_path.read_text(encoding="utf-8").splitlines(),
                [
                    f"cwd={repo_root}",
                    "arg=--interface",
                    "arg=127.0.0.1",
                    "arg=--port",
                    "arg=18765",
                    "arg=--parent",
                    "arg=bazel-bin/src/parent/workspace_parent",
                    "arg=--cwd",
                    f"arg={repo_root}",
                    "arg=--state-directory",
                    f"arg={repo_root / 'state' / 'my-opiniated-editor' / 'instances' / 'port-8765'}",
                ],
            )

    def test_service_uses_main_worktree(self):
        service = Path(
            runfile_path("tools/bridge/my-opiniated-editor-bridge@.service")
        ).read_text(encoding="utf-8")

        self.assertIn("WorkingDirectory=%h/my-opiniated-editor/main\n", service)
        self.assertIn(
            "EnvironmentFile=%h/.config/my-opiniated-editor/bridge-%i.env\n",
            service,
        )
        self.assertIn(
            "ExecStart=%h/my-opiniated-editor/main/tools/bridge/run_bridge.sh ${MOE_BRIDGE_HTTP_PORT} %i\n",
            service,
        )

        https_service = Path(
            runfile_path("tools/bridge/my-opiniated-editor-bridge-https@.service")
        ).read_text(encoding="utf-8")
        self.assertIn("Requires=my-opiniated-editor-bridge@%i.service\n", https_service)
        self.assertIn(
            "EnvironmentFile=%h/.config/my-opiniated-editor/bridge-%i.env\n",
            https_service,
        )
        self.assertIn(
            "ExecStart=%h/my-opiniated-editor/main/tools/bridge/https_proxy.py serve --http-port ${MOE_BRIDGE_HTTP_PORT} --https-port %i\n",
            https_service,
        )

    def test_restart_builds_before_restart(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            bin_dir = Path(temp_dir)
            log_path = bin_dir / "commands.log"
            for name in ["bazel", "systemctl"]:
                script = bin_dir / name
                script.write_text(
                    "#!/usr/bin/env bash\n"
                    f"printf '{name} %s\\n' \"$*\" >> {log_path}\n",
                    encoding="utf-8",
                )
                script.chmod(0o755)

            subprocess.run(
                [self.bash, runfile_path("tools/bridge/restart_bridge.sh"), "8765"],
                env={
                    **os.environ,
                    "PATH": f"{bin_dir}{os.pathsep}{os.environ['PATH']}",
                },
                check=True,
            )

            self.assertEqual(
                log_path.read_text(encoding="utf-8").splitlines(),
                [
                    "bazel --batch build //src/bridge:parent_ws_bridge //src/parent:workspace_parent",
                    "systemctl --user daemon-reload",
                    "systemctl --user restart my-opiniated-editor-bridge@8765.service",
                    "systemctl --user restart my-opiniated-editor-bridge-https@8765.service",
                ],
            )


if __name__ == "__main__":
    unittest.main()
