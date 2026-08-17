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
            "tools/bridge/run_https_proxy.sh",
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
            ("tools/bridge/run_https_proxy.sh", ["17682"]),
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

    def test_funnel_instance_examples_are_loopback_only(self):
        expected = {
            "bridge-7682.env.example": (
                "MOE_BRIDGE_HTTP_PORT=17682\n",
                "MOE_BRIDGE_ALLOWED_ORIGIN=https://nixos.example-tailnet.ts.net,https://127.0.0.1:7682\n",
            ),
            "bridge-7683.env.example": (
                "MOE_BRIDGE_HTTP_PORT=17683\n",
                "MOE_BRIDGE_ALLOWED_ORIGIN=https://nixos.example-tailnet.ts.net:10000,https://127.0.0.1:7683\n",
            ),
        }
        for example, required_lines in expected.items():
            with self.subTest(example=example):
                contents = Path(runfile_path(f"tools/bridge/{example}")).read_text(
                    encoding="utf-8"
                )
                self.assertIn("MOE_BRIDGE_HTTPS_INTERFACE=127.0.0.1\n", contents)
                self.assertNotIn("MOE_BRIDGE_HTTPS_INTERFACE=0.0.0.0", contents)
                for line in required_lines:
                    self.assertIn(line, contents)

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

    def prepare_https_runner(self, repo_root: Path) -> Path:
        runner = repo_root / "tools" / "bridge" / "run_https_proxy.sh"
        runner.parent.mkdir(parents=True)
        shutil.copyfile(runfile_path("tools/bridge/run_https_proxy.sh"), runner)
        runner.chmod(0o755)
        return runner

    def prepare_proxy_worktree(self, worktree: Path) -> None:
        proxy = worktree / "tools" / "bridge" / "https_proxy.py"
        proxy.parent.mkdir(parents=True)
        proxy.write_text(
            "#!/usr/bin/env bash\n"
            'printf "cwd=%s\\n" "$PWD" > "$MOE_TEST_LOG"\n'
            'printf "arg=%s\\n" "$@" >> "$MOE_TEST_LOG"\n',
            encoding="utf-8",
        )
        proxy.chmod(0o755)

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

    def test_https_runner_uses_selected_worktree(self):
        for selection_source in ["argument", "service environment"]:
            with self.subTest(selection_source=selection_source):
                with tempfile.TemporaryDirectory() as temp_dir:
                    root = Path(temp_dir)
                    repo_root = root / "main"
                    selected_worktree = root / "feature worktree"
                    runner = self.prepare_https_runner(repo_root)
                    self.prepare_proxy_worktree(selected_worktree)
                    log_path = root / "runner.log"
                    command = [self.bash, runner, "18765", "8765"]
                    env = {
                        **os.environ,
                        "MOE_TEST_LOG": str(log_path),
                    }
                    if selection_source == "argument":
                        command.append(selected_worktree)
                    else:
                        env["MOE_BRIDGE_WORKTREE"] = str(selected_worktree)

                    subprocess.run(command, env=env, check=True)

                    self.assertEqual(
                        log_path.read_text(encoding="utf-8").splitlines(),
                        [
                            f"cwd={selected_worktree}",
                            "arg=serve",
                            "arg=--http-port",
                            "arg=18765",
                            "arg=--https-port",
                            "arg=8765",
                        ],
                    )

    def test_services_use_main_launchers(self):
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
            "ExecStart=%h/my-opiniated-editor/main/tools/bridge/run_https_proxy.sh ${MOE_BRIDGE_HTTP_PORT} %i\n",
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
                contents = (
                    "#!/usr/bin/env bash\n"
                    f'printf \'{name} cwd=%s args=%s\\n\' "$PWD" "$*" >> {log_path}\n'
                )
                if name == "systemctl":
                    contents += (
                        'if [[ " $* " == *" edit "* ]]; then\n'
                        "  while IFS= read -r line; do\n"
                        f"    printf 'systemctl stdin=%s\\n' \"$line\" >> {log_path}\n"
                        "  done\n"
                        "fi\n"
                    )
                script.write_text(contents, encoding="utf-8")
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
                    f"systemctl cwd={selected_worktree} args=--user edit --runtime --stdin --drop-in=50-worktree.conf my-opiniated-editor-bridge@8765.service",
                    "systemctl stdin=[Service]",
                    f'systemctl stdin=Environment="MOE_BRIDGE_WORKTREE={selected_worktree}"',
                    f"systemctl cwd={selected_worktree} args=--user edit --runtime --stdin --drop-in=50-worktree.conf my-opiniated-editor-bridge-https@8765.service",
                    "systemctl stdin=[Service]",
                    f'systemctl stdin=Environment="MOE_BRIDGE_WORKTREE={selected_worktree}"',
                    "systemctl stdin=ExecStart=",
                    f'systemctl stdin=ExecStart="{selected_worktree}/tools/bridge/run_https_proxy.sh" ${{MOE_BRIDGE_HTTP_PORT}} %i',
                    f"systemctl cwd={selected_worktree} args=--user restart my-opiniated-editor-bridge@8765.service",
                    f"systemctl cwd={selected_worktree} args=--user restart my-opiniated-editor-bridge-https@8765.service",
                ],
            )


if __name__ == "__main__":
    unittest.main()
