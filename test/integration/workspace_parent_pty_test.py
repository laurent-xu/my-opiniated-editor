import fcntl
import json
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


def set_pty_size(fd: int, rows: int, cols: int):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


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
            redraw_output = read_until(master_fd, "__moe_export_done__")
            self.assertIn("\x1b[H\x1b[2J", redraw_output)
            self.assertIn("__moe_export_done__", redraw_output)

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

    def test_parent_command_redraws_tray_scrollback_on_switch(self):
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
            os.write(master_fd, numbered_line_script(40, "__moe_lines_done__"))
            read_until(master_fd, "__moe_lines_done__")

            os.write(master_fd, b"\x182")
            os.write(master_fd, shell_marker_command("__moe_tray2_ready__"))
            read_until(master_fd, "__moe_tray2_ready__")

            os.write(master_fd, b"\x181")
            redraw_output = read_until(master_fd, "__moe_line_40__")
            self.assertIn("\x1b[H\x1b[2J", redraw_output)
            self.assertIn("__moe_line_01__", redraw_output)
            self.assertIn("__moe_line_40__", redraw_output)

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

    def test_worktree_manager_registers_existing_bare_root(self):
        test_root = os.path.join(os.environ["TEST_TMPDIR"], "register-existing")
        repository = os.path.join(test_root, "repository")
        worktree = os.path.join(repository, "main")
        os.makedirs(os.path.join(repository, ".bare"))
        os.makedirs(worktree)
        with open(os.path.join(repository, ".git"), "w", encoding="utf-8") as output:
            output.write("gitdir: ./.bare\n")

        environment = worktree_test_environment("register-existing")
        environment["MOE_FAKE_GIT_WORKTREE_LIST"] = (
            f"worktree {repository}/.bare\n"
            "HEAD 111\n"
            "bare\n"
            "\n"
            f"worktree {worktree}\n"
            "HEAD 222\n"
            "branch refs/heads/main\n"
            "\n"
        )
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
            env=environment,
        )
        os.close(slave_fd)

        try:
            os.write(master_fd, b"\x18w")
            read_until(master_fd, "Repository root:")
            repository_with_typo = repository.removesuffix("repository") + "reposiory"
            os.write(
                master_fd,
                repository_with_typo.encode() + b"\x1b[D\x1b[D\x1b[Dt\r",
            )
            result = read_until(master_fd, "Completed")
            self.assertIn("Repository registered", result)

            registry_path = os.path.join(
                environment["XDG_STATE_HOME"],
                "my-opiniated-editor",
                "worktrees.pb",
            )
            self.assertTrue(os.path.isfile(registry_path))
            self.assertGreater(os.path.getsize(registry_path), 0)

            os.write(master_fd, b"\x18w")
            read_until(master_fd, "\x1b[H\x1b[2J")
            os.write(master_fd, shell_marker_command("__moe_after_worktree_overlay__"))
            shell_output = read_until(master_fd, "__moe_after_worktree_overlay__")
            self.assertIn("__moe_after_worktree_overlay__", shell_output)

            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)

    def test_worktree_manager_creates_new_bare_root(self):
        test_root = os.path.join(os.environ["TEST_TMPDIR"], "create-repository")
        repository = os.path.join(test_root, "repository")
        os.makedirs(test_root)
        environment = worktree_test_environment("create-repository")
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
            env=environment,
        )
        os.close(slave_fd)

        try:
            os.write(master_fd, b"\x18w")
            read_until(master_fd, "Repository root:")
            os.write(master_fd, repository.encode() + b"\r")
            read_until(master_fd, "Clone URL:")
            os.write(master_fd, b"ssh://example.invalid/repository.git\r")
            result = read_until(master_fd, "Completed")
            self.assertIn("Repository registered", result)

            self.assertTrue(os.path.isdir(os.path.join(repository, ".bare")))
            with open(os.path.join(repository, ".git"), encoding="utf-8") as pointer:
                self.assertEqual(pointer.read(), "gitdir: ./.bare\n")

            os.write(master_fd, b"\x18w")
            read_until(master_fd, "\x1b[H\x1b[2J")
            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)

    def test_worktree_manager_is_retained_by_each_tray_until_toggled_closed(self):
        environment = worktree_test_environment("per-tray-worktree-manager")
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
            env=environment,
        )
        os.close(slave_fd)

        try:
            os.write(master_fd, b"\x18w")
            read_until(master_fd, "Repository root:")
            os.write(master_fd, b"tray-one-input")
            read_until(master_fd, "> tray-one-input")

            os.write(master_fd, b"\x182")
            read_until(master_fd, "\x1b[H\x1b[2J")
            os.write(master_fd, b"\x18w")
            read_until(master_fd, "Repository root:")
            os.write(master_fd, b"tray-two-input")
            read_until(master_fd, "> tray-two-input")

            os.write(master_fd, b"\x181")
            tray_one = read_until(master_fd, "> tray-one-input")
            self.assertIn("\x1b[H\x1b[2J", tray_one)

            os.write(master_fd, b"\x182")
            tray_two = read_until(master_fd, "> tray-two-input")
            self.assertIn("\x1b[H\x1b[2J", tray_two)

            os.write(master_fd, b"\x18w")
            read_until(master_fd, "\x1b[H\x1b[2J")
            os.write(master_fd, shell_marker_command("__moe_tray_two_after_close__"))
            read_until(master_fd, "__moe_tray_two_after_close__")

            os.write(master_fd, b"\x181")
            read_until(master_fd, "> tray-one-input")
            os.write(master_fd, b"\x18w")
            read_until(master_fd, "\x1b[H\x1b[2J")
            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)

    def test_worktree_picker_switches_to_existing_worktree_and_reuses_its_tray(self):
        test_root = os.path.join(os.environ["TEST_TMPDIR"], "pick-existing")
        repository = os.path.join(test_root, "repository")
        main_worktree = os.path.join(repository, "main")
        feature_worktree = os.path.join(repository, "feature")
        os.makedirs(os.path.join(repository, ".bare"))
        os.makedirs(os.path.join(main_worktree, ".git"))
        os.makedirs(os.path.join(feature_worktree, ".git"))
        with open(os.path.join(repository, ".git"), "w", encoding="utf-8") as output:
            output.write("gitdir: ./.bare\n")

        environment = worktree_test_environment("pick-existing")
        environment["MOE_FAKE_GIT_WORKTREE_LIST"] = (
            f"worktree {repository}/.bare\n"
            "HEAD 111\n"
            "bare\n"
            "\n"
            f"worktree {feature_worktree}\n"
            "HEAD 222\n"
            "branch refs/heads/feature\n"
            "\n"
            f"worktree {main_worktree}\n"
            "HEAD 333\n"
            "branch refs/heads/main\n"
            "\n"
        )
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
            env=environment,
        )
        os.close(slave_fd)

        try:
            os.write(master_fd, b"\x18w")
            read_until(master_fd, "Repository root:")
            os.write(master_fd, repository.encode() + b"\r")
            read_until(master_fd, "Completed")
            os.write(master_fd, b"\x18w")
            read_until(master_fd, "\x1b[H\x1b[2J")

            os.write(master_fd, b"\x18t")
            read_until(master_fd, "Worktree picker")
            with open(
                environment["MOE_FAKE_FZF_CANDIDATES_LOG"], encoding="utf-8"
            ) as candidate_log:
                candidates = json.loads(candidate_log.readline())
            self.assertEqual(candidates, [feature_worktree, main_worktree])
            os.write(master_fd, b"\x1b[B\r")
            read_until(master_fd, "\x1b[H\x1b[2J")
            os.write(master_fd, b"pwd\n")
            selected_pwd = read_until(master_fd, main_worktree)
            self.assertIn(main_worktree, selected_pwd)

            os.write(master_fd, b"export MOE_WORKTREE_TRAY_MARKER=reused\n")
            os.write(master_fd, shell_marker_command("__moe_worktree_exported__"))
            read_until(master_fd, "__moe_worktree_exported__")
            os.write(master_fd, b"\x181")
            read_until(master_fd, "\x1b[H\x1b[2J")

            os.write(master_fd, b"\x18t")
            read_until(master_fd, "Worktree picker")
            os.write(master_fd, b"\x1b[B\r")
            read_until(master_fd, "__moe_worktree_exported__")
            os.write(
                master_fd,
                b"printf '__moe_reused_%s__\\n' \"$MOE_WORKTREE_TRAY_MARKER\"\n",
            )
            reused = read_until(master_fd, "__moe_reused_reused__")
            self.assertIn("__moe_reused_reused__", reused)

            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)

    def test_worktree_picker_cancel_redraws_drained_tray_without_forwarding_command(
        self,
    ):
        environment = worktree_test_environment("cancel-picker")
        master_fd, slave_fd = pty.openpty()
        process = subprocess.Popen(
            [runfile_path("src/parent/workspace_parent")],
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            cwd=os.environ["TEST_TMPDIR"],
            env=environment,
        )
        os.close(slave_fd)

        try:
            os.write(
                master_fd,
                b"sleep 0.2; printf '__moe_hidden_while_picker__\\n'\n\x18t",
            )
            read_until(master_fd, "Worktree picker")
            time.sleep(0.4)

            os.write(master_fd, b"\x18t")
            redraw = read_until(master_fd, "__moe_hidden_while_picker__")
            self.assertIn("\x1b[H\x1b[2J", redraw)

            os.write(master_fd, shell_marker_command("__moe_after_picker_cancel__"))
            shell_output = read_until(master_fd, "__moe_after_picker_cancel__")
            self.assertIn("__moe_after_picker_cancel__", shell_output)

            os.write(master_fd, b"exit\n")
            self.assertEqual(process.wait(timeout=5), 0)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            os.close(master_fd)


if __name__ == "__main__":
    unittest.main()
