#!/usr/bin/env bash

proposed_commit_base_tree() {
  local candidate_commit="HEAD"
  local candidate_tree
  local commit_mode="${MY_OPINIONATED_EDITOR_GIT_COMMIT_MODE:-}"

  case "$commit_mode" in
    "")
      ;;
    "$MY_OPINIONATED_EDITOR_GIT_COMMIT_MODE_AMEND")
      candidate_commit="HEAD^"
      ;;
    *)
      echo "Unsupported repository commit mode: $commit_mode" >&2
      return 2
      ;;
  esac

  if candidate_tree="$(
    git rev-parse --verify --quiet "${candidate_commit}^{tree}"
  )"; then
    printf '%s\n' "$candidate_tree"
    return
  fi

  # An unborn branch and an amended root commit both have no base commit.
  git mktree </dev/null
}

list_proposed_commit_files() {
  local base_tree="$1"
  shift

  git diff --cached --name-only --diff-filter=ACMR "$base_tree" -- "$@"
}
