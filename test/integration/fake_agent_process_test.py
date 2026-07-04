import os
import subprocess
import unittest


def runfile_path(path: str) -> str:
    return os.path.join(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], path)


class FakeAgentProcessTest(unittest.TestCase):
    def test_feedback_crosses_process_boundary(self):
        result = subprocess.run(
            [
                runfile_path("test/fixtures/fake_agent"),
                "--session-id",
                "integration-agent",
                "--echo-stdin",
            ],
            input="revise section 2\n",
            check=True,
            text=True,
            capture_output=True,
        )

        self.assertIn("SESSION integration-agent", result.stdout)
        self.assertIn("FEEDBACK revise section 2", result.stdout)


if __name__ == "__main__":
    unittest.main()
