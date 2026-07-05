import os
import subprocess
import unittest


def runfile_path(path: str) -> str:
    return os.path.join(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], path)


class FakeAgentTest(unittest.TestCase):
    def test_fake_agent_emits_deterministic_plan(self):
        result = subprocess.run(
            [
                runfile_path("test/fixtures/fake_agent"),
                "--session-id",
                "agent-123",
            ],
            check=True,
            text=True,
            capture_output=True,
        )

        self.assertIn("SESSION agent-123", result.stdout)
        self.assertIn("STATUS waiting-for-feedback", result.stdout)
        self.assertIn("- run bazel test //...", result.stdout)


if __name__ == "__main__":
    unittest.main()
