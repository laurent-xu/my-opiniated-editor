import fcntl
import os
import pty
import select
import struct
import subprocess
import termios
import time

DEFAULT_READ_TIMEOUT_SECONDS = 5.0
PROCESS_SHUTDOWN_TIMEOUT_SECONDS = 5


def runfile_path(path: str) -> str:
    return os.path.join(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], path)


def read_until(
    fd: int,
    needle: str,
    timeout_seconds: float = DEFAULT_READ_TIMEOUT_SECONDS,
) -> str:
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


def numbered_line_script(line_count: int, done_marker: str) -> bytes:
    return (
        (
            "i=1; while [ $i -le {line_count} ]; do "
            "printf '__moe_line_%02d__\\n' \"$i\"; "
            "i=$((i + 1)); "
            "done; printf '{done_marker}\\n'\n"
        )
        .format(line_count=line_count, done_marker=done_marker)
        .encode()
    )


def set_pty_size(fd: int, rows: int, columns: int):
    fcntl.ioctl(
        fd,
        termios.TIOCSWINSZ,
        struct.pack("HHHH", rows, columns, 0, 0),
    )


def switch_worktree_overlay_to_repository_mode(fd: int):
    os.write(fd, b"\t\t")


def switch_worktree_overlay_to_add_worktree_mode(fd: int):
    os.write(fd, b"\t")


def worktree_test_environment(name: str) -> dict[str, str]:
    environment = dict(os.environ)
    environment["XDG_STATE_HOME"] = os.path.join(
        os.environ["TEST_TMPDIR"], name, "state"
    )
    environment["MOE_GIT_EXECUTABLE"] = runfile_path("test/fixtures/fake_git")
    environment["MOE_FZF_EXECUTABLE"] = runfile_path("test/fixtures/fake_fzf")
    environment["MOE_FAKE_GIT_LOG"] = os.path.join(
        os.environ["TEST_TMPDIR"], name, "git.log"
    )
    environment["MOE_FAKE_FZF_CANDIDATES_LOG"] = os.path.join(
        os.environ["TEST_TMPDIR"], name, "fzf-candidates.log"
    )
    environment["MOE_FAKE_FZF_INPUT_LOG"] = os.path.join(
        os.environ["TEST_TMPDIR"], name, "fzf-input.log"
    )
    environment.pop("MOE_FAKE_GIT_FAIL_OPERATION", None)
    environment.pop("MOE_FAKE_GIT_DEFAULT_BRANCH", None)
    environment.pop("MOE_FAKE_GIT_WORKTREE_LIST", None)
    return environment


class WorkspaceParentPty:
    def __init__(self, environment: dict[str, str] | None = None):
        master_fd, slave_fd = pty.openpty()
        try:
            process = subprocess.Popen(
                [runfile_path("src/parent/workspace_parent")],
                stdin=slave_fd,
                stdout=slave_fd,
                stderr=slave_fd,
                close_fds=True,
                cwd=os.environ["TEST_TMPDIR"],
                env=environment,
            )
        except BaseException:
            os.close(master_fd)
            os.close(slave_fd)
            raise

        os.close(slave_fd)
        self._master_fd = master_fd
        self._process = process

    def close(self):
        if self._master_fd is None:
            return
        try:
            if self._process.poll() is None:
                self._process.terminate()
                self._process.wait(timeout=PROCESS_SHUTDOWN_TIMEOUT_SECONDS)
        finally:
            os.close(self._master_fd)
            self._master_fd = None

    @property
    def master_fd(self) -> int:
        if self._master_fd is None:
            raise RuntimeError("workspace parent PTY has not been started")
        return self._master_fd

    @property
    def process(self) -> subprocess.Popen:
        if self._process is None:
            raise RuntimeError("workspace parent process has not been started")
        return self._process
