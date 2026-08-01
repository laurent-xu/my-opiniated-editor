import json
import os
import pwd
import re
import time
import unittest

from test.integration.support.workspace_parent_pty import (
    WorkspaceParentPty,
    numbered_line_script,
    read_until,
    set_pty_size,
    shell_marker_command,
    shell_pid_marker_command,
    switch_worktree_overlay_to_add_worktree_mode,
    switch_worktree_overlay_to_repository_mode,
    workspace_parent_test_environment,
    worktree_test_environment,
)


class WorkspaceParentPtyTest(unittest.TestCase):
    def test_parent_process_renders_child_shell_pty(self):
        parent = WorkspaceParentPty()
        self.addCleanup(parent.close)
        master_fd = parent.master_fd
        process = parent.process

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
        home_directory = pwd.getpwuid(os.getuid()).pw_dir
        pwd_output = read_until(master_fd, home_directory)
        self.assertIn(home_directory, pwd_output)

        os.write(master_fd, b"exit\n")
        time.sleep(0.2)
        os.write(master_fd, shell_pid_marker_command())
        replacement_output = read_until(master_fd, "__moe_pid_done__")
        replacement = re.search(
            r"__moe_shell_pid_(\d+)_parent_(\d+)__moe_pid_done__",
            replacement_output,
        )
        self.assertIsNotNone(replacement, replacement_output)
        self.assertNotEqual(int(replacement.group(1)), shell_pid)
        self.assertEqual(int(replacement.group(2)), process.pid)
        self.assertIsNone(process.poll())

    def test_parent_pty_resize_reaches_child_shell_pty(self):
        parent = WorkspaceParentPty()
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

        set_pty_size(master_fd, 31, 103)
        os.write(
            master_fd, b"stty size\n" + shell_marker_command("__moe_resize_done__")
        )
        output = read_until(master_fd, "__moe_resize_done__")
        self.assertIn("31 103", output)

    def test_parent_command_switches_between_anonymous_trays(self):
        parent = WorkspaceParentPty()
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

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

        home_directory = pwd.getpwuid(os.getuid()).pw_dir
        os.write(master_fd, b"pwd\n")
        tray_two_pwd = read_until(master_fd, home_directory)
        self.assertIn(home_directory, tray_two_pwd)

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

    def test_parent_command_redraws_tray_scrollback_on_switch(self):
        parent = WorkspaceParentPty()
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

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

    def test_parent_process_defaults_terminal_type_for_shell(self):
        env = workspace_parent_test_environment("default-terminal-type")
        env.pop("TERM", None)
        parent = WorkspaceParentPty(env)
        self.addCleanup(parent.close)

        os.write(parent.master_fd, b"printf '%s\\n' \"$TERM\"\n")
        terminal_type = read_until(parent.master_fd, "xterm-256color")
        self.assertIn("xterm-256color", terminal_type)

    def test_parent_process_starts_without_home_when_test_state_is_isolated(self):
        environment = workspace_parent_test_environment("without-home")
        environment.pop("HOME", None)
        parent = WorkspaceParentPty(environment)
        self.addCleanup(parent.close)

        os.write(parent.master_fd, shell_marker_command("__moe_without_home_ready__"))
        output = read_until(parent.master_fd, "__moe_without_home_ready__")
        self.assertIn("__moe_without_home_ready__", output)

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
        parent = WorkspaceParentPty(environment)
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

        os.write(master_fd, b"\x18w")
        switch_worktree_overlay_to_repository_mode(master_fd)
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

    def test_worktree_manager_creates_new_bare_root(self):
        test_root = os.path.join(os.environ["TEST_TMPDIR"], "create-repository")
        repository = os.path.join(test_root, "repository")
        os.makedirs(test_root)
        environment = worktree_test_environment("create-repository")
        parent = WorkspaceParentPty(environment)
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

        os.write(master_fd, b"\x18w")
        switch_worktree_overlay_to_repository_mode(master_fd)
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

    def test_worktree_manager_creates_worktree_and_switches_to_its_tray(self):
        test_root = os.path.join(os.environ["TEST_TMPDIR"], "create-worktree")
        repository = os.path.join(test_root, "repository")
        worktree = os.path.join(repository, "feature-terminal")
        os.makedirs(os.path.join(repository, ".bare"))
        with open(os.path.join(repository, ".git"), "w", encoding="utf-8") as output:
            output.write("gitdir: ./.bare\n")

        environment = worktree_test_environment("create-worktree")
        environment["MOE_FAKE_GIT_WORKTREE_LIST"] = (
            f"worktree {repository}/.bare\n" "HEAD 111\n" "bare\n" "\n"
        )
        parent = WorkspaceParentPty(environment)
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

        os.write(master_fd, b"\x18w")
        switch_worktree_overlay_to_repository_mode(master_fd)
        read_until(master_fd, "Repository root:")
        os.write(master_fd, repository.encode() + b"\r")
        read_until(master_fd, "Completed")
        os.write(master_fd, b"\x18w")
        read_until(master_fd, "\x1b[H\x1b[2J")

        os.write(master_fd, b"\x18w")
        switch_worktree_overlay_to_add_worktree_mode(master_fd)
        repository_selection = read_until(master_fd, "Repository> ")
        self.assertIn(
            "\x1b[48;5;244mAdd worktree\x1b[48;5;236m",
            repository_selection,
        )
        with open(
            environment["MOE_FAKE_FZF_CANDIDATES_LOG"], encoding="utf-8"
        ) as candidate_log:
            self.assertEqual(json.loads(candidate_log.readline()), [repository])

        os.write(master_fd, b"\t")
        repository_mode = read_until(master_fd, "Repository root:")
        self.assertIn("\x1b[H\x1b[2J", repository_mode)
        os.write(master_fd, b"\x1b[Z")
        read_until(master_fd, "Repository> ")

        os.write(master_fd, b"\r")
        branch_form = read_until(master_fd, "Branch:")
        self.assertIn("\x1b[H\x1b[2J", branch_form)
        os.write(master_fd, b"feature/terminal\r")
        provision_output = read_until(master_fd, "\x1b[H\x1b[2J")
        self.assertIn("Worktree created:", provision_output)

        os.write(
            master_fd,
            b"pwd\n" + shell_marker_command("__moe_created_worktree_ready__"),
        )
        selected_output = read_until(master_fd, "__moe_created_worktree_ready__")
        self.assertIn(worktree, selected_output)
        self.assertTrue(os.path.isfile(os.path.join(worktree, ".git")))

        with open(environment["MOE_FAKE_GIT_LOG"], encoding="utf-8") as git_log:
            invocations = git_log.read()
        self.assertIn('"worktree", "add", "-b", "feature/terminal"', invocations)

    def test_worktree_manager_is_retained_by_each_tray_until_toggled_closed(self):
        environment = worktree_test_environment("per-tray-worktree-manager")
        parent = WorkspaceParentPty(environment)
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

        os.write(master_fd, b"\x18w")
        switch_worktree_overlay_to_repository_mode(master_fd)
        read_until(master_fd, "Repository root:")
        os.write(master_fd, b"tray-one-input")
        read_until(master_fd, "> tray-one-input")

        os.write(master_fd, b"\x182")
        read_until(master_fd, "\x1b[H\x1b[2J")
        os.write(master_fd, b"\x18w")
        switch_worktree_overlay_to_repository_mode(master_fd)
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
        parent = WorkspaceParentPty(environment)
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

        os.write(master_fd, b"\x18w")
        switch_worktree_overlay_to_repository_mode(master_fd)
        read_until(master_fd, "Repository root:")
        os.write(master_fd, repository.encode() + b"\r")
        read_until(master_fd, "Completed")
        os.write(master_fd, b"\x18w")
        read_until(master_fd, "\x1b[H\x1b[2J")

        os.write(master_fd, b"\x18w")
        read_until(master_fd, "Worktree> ")
        with open(
            environment["MOE_FAKE_FZF_CANDIDATES_LOG"], encoding="utf-8"
        ) as candidate_log:
            candidates = json.loads(candidate_log.readline())
        self.assertEqual(candidates, [feature_worktree, main_worktree, "/anonymous/1"])
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

        os.write(master_fd, b"\x18w")
        initial_preview = read_until(master_fd, "Preview: ...")
        self.assertIn("repository/feature", initial_preview)
        read_until(master_fd, "Worktree> ")
        os.write(master_fd, b"\x1b[B")
        focused_preview = read_until(
            master_fd, "export MOE_WORKTREE_TRAY_MARKER=reused"
        )
        self.assertIn("Preview: ...", focused_preview)
        self.assertIn("repository/main", focused_preview)
        os.write(master_fd, b"\r")
        read_until(master_fd, "\x1b[H\x1b[2J")
        os.write(
            master_fd,
            b"printf '__moe_reused_%s__\\n' \"$MOE_WORKTREE_TRAY_MARKER\"\n",
        )
        reused = read_until(master_fd, "__moe_reused_reused__")
        self.assertIn("__moe_reused_reused__", reused)

    def test_worktree_picker_cancel_redraws_drained_tray_without_forwarding_command(
        self,
    ):
        environment = worktree_test_environment("cancel-picker")
        parent = WorkspaceParentPty(environment)
        self.addCleanup(parent.close)
        master_fd = parent.master_fd

        os.write(
            master_fd,
            b"sleep 0.2; printf '__moe_hidden_while_picker__\\n'\n\x18w",
        )
        read_until(master_fd, "\x1b[48;5;244mWorktrees\x1b[48;5;236m")
        time.sleep(0.4)

        live_preview = read_until(master_fd, "__moe_hidden_while_picker__")
        self.assertIn("Preview: /anonymous/1", live_preview)
        os.write(master_fd, b"\x18w")
        redraw = read_until(master_fd, "\x1b[H\x1b[2J")
        if "__moe_hidden_while_picker__" not in redraw:
            redraw += read_until(master_fd, "__moe_hidden_while_picker__")
        self.assertIn("\x1b[H\x1b[2J", redraw)
        self.assertIn("__moe_hidden_while_picker__", redraw)

        os.write(master_fd, shell_marker_command("__moe_after_picker_cancel__"))
        shell_output = read_until(master_fd, "__moe_after_picker_cancel__")
        self.assertIn("__moe_after_picker_cancel__", shell_output)


if __name__ == "__main__":
    unittest.main()
