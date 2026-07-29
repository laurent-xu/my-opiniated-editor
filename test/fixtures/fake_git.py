import json
import os
import pathlib
import sys


def operation_name(arguments: list[str]) -> str:
    if not arguments:
        return ""
    if arguments[0] == "clone":
        return "clone"
    if arguments[0] == "--git-dir" and len(arguments) >= 3:
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
    return os.environ.get("MOE_FAKE_GIT_FAIL_OPERATION") == operation


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
        else:
            print("1111111111111111111111111111111111111111")
        return 0

    if operation == "symbolic-ref":
        print(os.environ.get("MOE_FAKE_GIT_DEFAULT_BRANCH", "refs/heads/main"))
        return 0

    if operation == "worktree":
        porcelain = os.environ.get("MOE_FAKE_GIT_WORKTREE_LIST", "")
        sys.stdout.buffer.write(porcelain.replace("\n", "\0").encode())
        return 0

    if operation in {"config", "fetch"}:
        return 0

    print(f"unsupported fake git invocation: {arguments}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
