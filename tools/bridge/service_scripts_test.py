import os
import shutil
import subprocess
import unittest


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


if __name__ == "__main__":
    unittest.main()
