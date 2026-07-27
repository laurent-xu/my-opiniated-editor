import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import termios
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


def shell_marker_command(marker: str) -> bytes:
    escaped = "".join(f"\\{ord(character):03o}" for character in marker)
    return f"printf '{escaped}\\012'\n".encode()


def shell_pid_marker_command() -> bytes:
    return (
        b"printf '\\137\\137moe_shell_pid_%s_parent_%s"
        b'\\137\\137moe_pid_done\\137\\137\\012\' "$$" "$PPID"\n'
    )


def set_pty_size(fd: int, rows: int, cols: int):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


class WorkspaceParentPtyTest(unittest.TestCase):
    def test_parent_process_renders_child_shell_pty(self):
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
        )
        os.close(slave_fd)

        try:
            os.write(master_fd, shell_pid_marker_command())
            pid_output = read_until(master_fd, "__moe_pid_done__")
            match = re.search(
                r"__moe_shell_pid_(\d+)_parent_(\d+)__moe_pid_done__",
                pid_output,
            )
            self.assertIsNotNone(match, pid_output)
            shell_pid = int(match.group(1))
            shell_parent_pid = int(match.group(2))
            self.assertNotEqual(shell_pid, process.pid)
            self.assertEqual(shell_parent_pid, process.pid)

            os.write(master_fd, shell_marker_command("__moe_shell_ready__"))
            ready = read_until(master_fd, "__moe_shell_ready__")
            self.assertIn("__moe_shell_ready__", ready)

            os.write(master_fd, b"pwd\n")
            pwd = read_until(master_fd, os.environ["TEST_TMPDIR"])
            self.assertIn(os.environ["TEST_TMPDIR"], pwd)

            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)

    def test_parent_pty_resize_reaches_child_shell_pty(self):
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
        )
        os.close(slave_fd)

        try:
            set_pty_size(master_fd, 31, 103)
            os.write(
                master_fd, b"stty size\n" + shell_marker_command("__moe_resize_done__")
            )
            output = read_until(master_fd, "__moe_resize_done__")
            self.assertIn("31 103", output)

            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)

    def test_parent_command_switches_between_anonymous_trays(self):
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
        )
        os.close(slave_fd)

        try:
            os.write(
                master_fd,
                b"export MOE_TRAY_MARKER=tray_one\n"
                + shell_marker_command("__moe_export_done__"),
            )
            read_until(master_fd, "__moe_export_done__")

            os.write(master_fd, b"\x182")
            os.write(
                master_fd,
                b"printf '__moe_tray2_%s__\\n' \"${MOE_TRAY_MARKER:-empty}\"\n",
            )
            tray_two_output = read_until(master_fd, "__moe_tray2_empty__")
            self.assertIn("__moe_tray2_empty__", tray_two_output)

            os.write(master_fd, b"\x181")
            replay_output = read_until(master_fd, "__moe_export_done__")
            self.assertIn("\x1b[H\x1b[2J", replay_output)
            self.assertIn("__moe_export_done__", replay_output)

            os.write(
                master_fd,
                b"printf '__moe_tray1_%s__\\n' \"$MOE_TRAY_MARKER\"\n",
            )
            tray_one_output = read_until(master_fd, "__moe_tray1_tray_one__")
            self.assertIn("__moe_tray1_tray_one__", tray_one_output)

            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)

    def test_parent_process_defaults_terminal_type_for_shell(self):
        master_fd, slave_fd = pty.openpty()
        env = dict(os.environ)
        env.pop("TERM", None)
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
            env=env,
        )
        os.close(slave_fd)

        try:
            os.write(master_fd, b"printf '%s\\n' \"$TERM\"\n")
            terminal_type = read_until(master_fd, "xterm-256color")
            self.assertIn("xterm-256color", terminal_type)

            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)


if __name__ == "__main__":
    unittest.main()
