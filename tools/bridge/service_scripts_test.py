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

    def prepare_runner(self, repo_root: Path) -> Path:
        runner = repo_root / "tools" / "bridge" / "run_bridge.sh"
        runner.parent.mkdir(parents=True)
        shutil.copyfile(runfile_path("tools/bridge/run_bridge.sh"), runner)
        runner.chmod(0o755)
        return runner

    def prepare_bridge_worktree(self, worktree: Path) -> None:
        bridge = worktree / "bazel-bin" / "src" / "bridge" / "parent_ws_bridge"
        bridge.parent.mkdir(parents=True)
        bridge.write_text(
            "#!/usr/bin/env bash\n"
            'printf "cwd=%s\\n" "$PWD" > "$MOE_TEST_LOG"\n'
            'printf "arg=%s\\n" "$@" >> "$MOE_TEST_LOG"\n',
            encoding="utf-8",
        )
        bridge.chmod(0o755)
        parent = worktree / "bazel-bin" / "src" / "parent" / "workspace_parent"
        parent.parent.mkdir(parents=True)
        parent.touch()

    def runner_environment(self, state_root: Path, log_path: Path) -> dict[str, str]:
        return {
            **os.environ,
            "XDG_STATE_HOME": str(state_root),
            "MOE_TEST_LOG": str(log_path),
        }

    def test_runner_uses_loopback_upstream_for_public_port(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir) / "repo"
            runner = self.prepare_runner(repo_root)
            self.prepare_bridge_worktree(repo_root)

            log_path = repo_root / "runner.log"
            subprocess.run(
                [self.bash, runner, "18765", "8765"],
                env=self.runner_environment(repo_root / "state", log_path),
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

    def test_runner_uses_selected_worktree(self):
        for selection_source in ["argument", "service environment"]:
            with self.subTest(selection_source=selection_source):
                with tempfile.TemporaryDirectory() as temp_dir:
                    root = Path(temp_dir)
                    repo_root = root / "main"
                    selected_worktree = root / "feature worktree"
                    runner = self.prepare_runner(repo_root)
                    self.prepare_bridge_worktree(selected_worktree)
                    log_path = root / "runner.log"
                    command = [self.bash, runner, "18765", "8765"]
                    env = self.runner_environment(root / "state", log_path)
                    if selection_source == "argument":
                        command.append(selected_worktree)
                    else:
                        env["MOE_BRIDGE_WORKTREE"] = str(selected_worktree)

                    subprocess.run(command, env=env, check=True)

                    output = log_path.read_text(encoding="utf-8").splitlines()
                    self.assertEqual(output[0], f"cwd={selected_worktree}")
                    self.assertIn(f"arg={selected_worktree}", output)
                    self.assertIn("arg=18765", output)
                    self.assertIn(
                        f"arg={root / 'state' / 'my-opiniated-editor' / 'instances' / 'port-8765'}",
                        output,
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
            root = Path(temp_dir)
            bin_dir = root / "bin"
            bin_dir.mkdir()
            selected_worktree = root / "feature worktree"
            selected_worktree.mkdir()
            log_path = bin_dir / "commands.log"
            for name in ["bazel", "systemctl"]:
                script = bin_dir / name
                script.write_text(
                    "#!/usr/bin/env bash\n"
                    f'printf \'{name} cwd=%s args=%s\\n\' "$PWD" "$*" >> {log_path}\n',
                    encoding="utf-8",
                )
                script.chmod(0o755)

            subprocess.run(
                [
                    self.bash,
                    runfile_path("tools/bridge/restart_bridge.sh"),
                    "8765",
                    selected_worktree,
                ],
                env={
                    **os.environ,
                    "PATH": f"{bin_dir}{os.pathsep}{os.environ['PATH']}",
                },
                check=True,
            )

            self.assertEqual(
                log_path.read_text(encoding="utf-8").splitlines(),
                [
                    f"bazel cwd={selected_worktree} args=--batch build //src/bridge:parent_ws_bridge //src/parent:workspace_parent",
                    f"systemctl cwd={selected_worktree} args=--user daemon-reload",
                    f'systemctl cwd={selected_worktree} args=--user set-property --runtime my-opiniated-editor-bridge@8765.service Environment="MOE_BRIDGE_WORKTREE={selected_worktree}"',
                    f"systemctl cwd={selected_worktree} args=--user restart my-opiniated-editor-bridge@8765.service",
                    f"systemctl cwd={selected_worktree} args=--user restart my-opiniated-editor-bridge-https@8765.service",
                ],
            )


if __name__ == "__main__":
    unittest.main()
