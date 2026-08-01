import json
import os
import pathlib
import shutil
import sys


def operation_name(arguments: list[str]) -> str:
    if not arguments:
        return ""
    if arguments[0] == "clone":
        return "clone"
    if arguments[0] == "--git-dir" and len(arguments) >= 3:
        if arguments[2] == "worktree" and len(arguments) >= 4:
            return f"worktree-{arguments[3]}"
        return arguments[2]
    return arguments[0]


def log_invocation(arguments: list[str]) -> None:
    log_path = os.environ.get("MOE_FAKE_GIT_LOG")
    if not log_path:
        return
    with open(log_path, "a", encoding="utf-8") as output:
        output.write(json.dumps(arguments))
        output.write("\n")


def should_fail(operation: str) -> bool:
    configured = os.environ.get("MOE_FAKE_GIT_FAIL_OPERATION")
    return configured == operation or (
        configured == "worktree" and operation.startswith("worktree-")
    )


def configured_branches(environment_name: str) -> set[str]:
    return set(json.loads(os.environ.get(environment_name, "[]")))


def main() -> int:
    arguments = sys.argv[1:]
    operation = operation_name(arguments)
    log_invocation(arguments)

    if should_fail(operation):
        print(f"fake git failure: {operation}", file=sys.stderr)
        return 23

    if operation == "clone":
        if len(arguments) != 4 or arguments[1] != "--bare":
            return 2
        pathlib.Path(arguments[3]).mkdir(parents=True)
        return 0

    if operation == "rev-parse":
        if "--is-bare-repository" in arguments:
            print("true")
            return 0

        reference = arguments[-1].removesuffix("^{commit}")
        default_reference = os.environ.get(
            "MOE_FAKE_GIT_DEFAULT_BRANCH", "refs/heads/main"
        )
        if reference == default_reference:
            print("1111111111111111111111111111111111111111")
            return 0
        if reference.startswith("refs/heads/"):
            branch = reference.removeprefix("refs/heads/")
            if branch in configured_branches("MOE_FAKE_GIT_LOCAL_BRANCHES"):
                print("1111111111111111111111111111111111111111")
                return 0
        if reference.startswith("refs/remotes/origin/"):
            branch = reference.removeprefix("refs/remotes/origin/")
            if branch in configured_branches("MOE_FAKE_GIT_REMOTE_BRANCHES"):
                print("1111111111111111111111111111111111111111")
                return 0
        return 1

    if operation == "symbolic-ref":
        print(os.environ.get("MOE_FAKE_GIT_DEFAULT_BRANCH", "refs/heads/main"))
        return 0

    if operation.startswith("worktree-"):
        if len(arguments) >= 6 and arguments[3] == "add":
            bare_directory = pathlib.Path(arguments[1])
            if arguments[4] == "-b" and len(arguments) == 8:
                branch = arguments[5]
                worktree = pathlib.Path(arguments[6])
            elif arguments[4:6] == ["--track", "-b"] and len(arguments) == 9:
                branch = arguments[6]
                worktree = pathlib.Path(arguments[7])
            elif len(arguments) == 6:
                worktree = pathlib.Path(arguments[4])
                branch = arguments[5]
            else:
                return 2
            administrative = bare_directory / "worktrees" / branch.replace("/", "-")
            administrative.mkdir(parents=True)
            worktree.mkdir(parents=True)
            (worktree / ".git").write_text(
                f"gitdir: {administrative}\n", encoding="utf-8"
            )
            (administrative / "HEAD").write_text(
                f"ref: refs/heads/{branch}\n", encoding="utf-8"
            )
            return 0
        if arguments[3] == "remove" and len(arguments) in {6, 7}:
            shutil.rmtree(arguments[-1], ignore_errors=True)
            return 0
        if arguments[3] == "prune":
            marker = os.environ.get("MOE_FAKE_GIT_PRUNE_MARKER")
            if marker:
                pathlib.Path(marker).touch()
            return 0
        marker = os.environ.get("MOE_FAKE_GIT_PRUNE_MARKER")
        after_prune = marker and pathlib.Path(marker).exists()
        porcelain = os.environ.get(
            "MOE_FAKE_GIT_WORKTREE_LIST_AFTER_PRUNE"
            if after_prune
            else "MOE_FAKE_GIT_WORKTREE_LIST",
            "",
        )
        sys.stdout.buffer.write(porcelain.replace("\n", "\0").encode())
        return 0

    if operation == "branch":
        if len(arguments) != 7 or arguments[3:6] != ["--delete", "--force", "--"]:
            return 2
        return 0

    if operation in {"config", "fetch"}:
        return 0

    print(f"unsupported fake git invocation: {arguments}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
