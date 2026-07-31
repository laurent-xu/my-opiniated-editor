#!/usr/bin/env bash
set -euo pipefail

readonly RUNFILES_ROOT="$TEST_SRCDIR/$TEST_WORKSPACE"
readonly COMMIT_WRAPPER="$RUNFILES_ROOT/tools/git/commit"
readonly COMMIT_MODE="$RUNFILES_ROOT/tools/git/commit_mode.sh"
readonly COMMIT_FILES="$RUNFILES_ROOT/tools/git-hooks/commit_files.sh"
readonly PRE_COMMIT="$RUNFILES_ROOT/tools/git-hooks/pre-commit"

temp_root="$(mktemp -d)"
trap 'rm -rf "$temp_root"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

assert_log_contains() {
  local log_file="$1"
  local expected="$2"

  grep -Fxq "$expected" "$log_file" ||
    fail "expected '$expected' in $log_file"
}

assert_log_excludes() {
  local log_file="$1"
  local unexpected="$2"

  if grep -Fxq "$unexpected" "$log_file"; then
    fail "did not expect '$unexpected' in $log_file"
  fi
}

initialize_repository() {
  local repository="$1"

  git init -q "$repository"
  git -C "$repository" config user.email "hook-test@example.com"
  git -C "$repository" config user.name "Hook Test"
  mkdir -p \
    "$repository/.git/hooks" \
    "$repository/fake-bin" \
    "$repository/src" \
    "$repository/tools/bazel" \
    "$repository/tools/git" \
    "$repository/tools/git-hooks"

  cp "$COMMIT_MODE" "$repository/tools/git/commit_mode.sh"
  cp "$COMMIT_FILES" "$repository/tools/git-hooks/commit_files.sh"
  cp "$PRE_COMMIT" "$repository/.git/hooks/pre-commit"
  chmod +x "$repository/.git/hooks/pre-commit"

  cat >"$repository/fake-bin/clang-format" <<'EOF'
#!/usr/bin/env bash
printf 'format:%s\n' "${@: -1}" >>"$HOOK_TEST_LOG"
EOF
  cat >"$repository/fake-bin/clang-tidy" <<'EOF'
#!/usr/bin/env bash
printf 'tidy:%s\n' "$1" >>"$HOOK_TEST_LOG"
EOF
  cat >"$repository/fake-bin/bazel" <<'EOF'
#!/usr/bin/env bash
printf 'bazel:%s\n' "$*" >>"$HOOK_TEST_LOG"
EOF
  cat >"$repository/tools/bazel/refresh_compile_commands.sh" <<'EOF'
#!/usr/bin/env bash
printf 'refresh\n' >>"$HOOK_TEST_LOG"
EOF
  chmod +x \
    "$repository/fake-bin/clang-format" \
    "$repository/fake-bin/clang-tidy" \
    "$repository/fake-bin/bazel" \
    "$repository/tools/bazel/refresh_compile_commands.sh"
}

commit_without_hook() {
  local repository="$1"
  local message="$2"

  git -C "$repository" add -A
  git -C "$repository" commit -q --no-verify -m "$message"
}

run_repository_commit() {
  local repository="$1"
  local log_file="$2"
  shift 2

  (
    cd "$repository"
    HOOK_TEST_LOG="$log_file" \
      PATH="$repository/fake-bin:$PATH" \
      "$COMMIT_WRAPPER" "$@"
  )
}

test_normal_commit_selects_only_index_diff() {
  local repository="$temp_root/normal"
  local log_file="$repository/hook.log"

  initialize_repository "$repository"
  printf 'int previous;\n' >"$repository/src/previous.cc"
  commit_without_hook "$repository" "previous"

  printf 'int selected;\n' >"$repository/src/selected.cc"
  git -C "$repository" add src/selected.cc
  run_repository_commit "$repository" "$log_file" -q -m "normal"

  assert_log_contains "$log_file" "refresh"
  assert_log_contains "$log_file" "format:src/selected.cc"
  assert_log_contains "$log_file" "tidy:src/selected.cc"
  assert_log_excludes "$log_file" "format:src/previous.cc"
  assert_log_excludes "$log_file" "tidy:src/previous.cc"
}

test_amend_selects_complete_proposed_commit() {
  local repository="$temp_root/amend"
  local log_file="$repository/hook.log"

  initialize_repository "$repository"
  printf 'int base;\n' >"$repository/src/base.cc"
  commit_without_hook "$repository" "base"

  printf 'int unchanged_from_head;\n' >"$repository/src/unchanged_from_head.cc"
  commit_without_hook "$repository" "to amend"
  printf 'int newly_staged;\n' >"$repository/src/newly_staged.cc"
  git -C "$repository" add src/newly_staged.cc

  run_repository_commit "$repository" "$log_file" -q --amend --no-edit

  assert_log_contains "$log_file" "format:src/unchanged_from_head.cc"
  assert_log_contains "$log_file" "tidy:src/unchanged_from_head.cc"
  assert_log_contains "$log_file" "format:src/newly_staged.cc"
  assert_log_contains "$log_file" "tidy:src/newly_staged.cc"
  assert_log_excludes "$log_file" "format:src/base.cc"
  assert_log_excludes "$log_file" "tidy:src/base.cc"
}

test_root_amend_uses_empty_base_tree() {
  local repository="$temp_root/root-amend"
  local log_file="$repository/hook.log"

  initialize_repository "$repository"
  printf 'int root_commit;\n' >"$repository/src/root_commit.cc"
  commit_without_hook "$repository" "root"

  run_repository_commit "$repository" "$log_file" -q --amend --no-edit

  assert_log_contains "$log_file" "format:src/root_commit.cc"
  assert_log_contains "$log_file" "tidy:src/root_commit.cc"
}

test_python_only_commit_skips_cpp_setup() {
  local repository="$temp_root/python-only"
  local log_file="$repository/hook.log"

  initialize_repository "$repository"
  printf '# baseline\n' >"$repository/README.md"
  commit_without_hook "$repository" "base"

  mkdir -p "$repository/test"
  printf 'value = 1\n' >"$repository/test/selected.py"
  git -C "$repository" add test/selected.py
  run_repository_commit "$repository" "$log_file" -q -m "python only"

  assert_log_contains \
    "$log_file" \
    "bazel:--batch run //tools/python:pyformat -- --check ."
  assert_log_excludes "$log_file" "refresh"
  if grep -Eq '^(format|tidy):' "$log_file"; then
    fail "Python-only commit unexpectedly ran a C++ tool"
  fi
}

test_normal_commit_selects_only_index_diff
test_amend_selects_complete_proposed_commit
test_root_amend_uses_empty_base_tree
test_python_only_commit_skips_cpp_setup
