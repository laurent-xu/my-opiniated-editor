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

    def test_runner_requires_token_before_building(self):
        env = dict(os.environ)
        env.pop("MOE_BRIDGE_TOKEN", None)
        env.update(
            {
                "MOE_BRIDGE_INTERFACE": "0.0.0.0",
                "MOE_BRIDGE_PORT": "7682",
            }
        )

        result = subprocess.run(
            [self.bash, runfile_path("tools/bridge/run_bridge.sh")],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("MOE_BRIDGE_TOKEN is required", result.stderr)

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
                [self.bash, runfile_path("tools/bridge/restart_bridge.sh")],
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
                    "systemctl --user restart my-opiniated-editor-bridge.service",
                ],
            )


if __name__ == "__main__":
    unittest.main()
