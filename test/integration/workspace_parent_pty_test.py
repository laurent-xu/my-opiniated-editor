import os
import pty
import select
import subprocess
import time
import unittest


def runfile_path(path: str) -> str:
    return os.path.join(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], path)


def read_until(fd: int, needle: str, timeout_seconds: float = 5.0) -> str:
    deadline = time.monotonic() + timeout_seconds
    output = b""
    while time.monotonic() < deadline:
        text = output.decode(errors="replace")
        if needle in text:
            return text

        remaining = max(0.0, deadline - time.monotonic())
        readable, _, _ = select.select([fd], [], [], remaining)
        if not readable:
            break
        chunk = os.read(fd, 4096)
        if not chunk:
            break
        output += chunk

    text = output.decode(errors="replace")
    raise AssertionError(f"timed out waiting for {needle!r}; output was {text!r}")


class WorkspaceParentPtyTest(unittest.TestCase):
    def test_parent_process_responds_through_pty(self):
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
        )
        os.close(slave_fd)

        try:
            startup = read_until(master_fd, "moe> ")
            self.assertIn("my-opiniated-editor phase-1-parent-pty-spike", startup)
            self.assertIn("parent-ready", startup)

            os.write(master_fd, b"status\n")
            status = read_until(master_fd, "moe> ")
            self.assertIn("workspace-parent status=ready", status)

            os.write(master_fd, b"quit\n")
            goodbye = read_until(master_fd, "goodbye")
            self.assertIn("goodbye", goodbye)

            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)


if __name__ == "__main__":
    unittest.main()
