import argparse
import sys


def emit_plan(session_id: str) -> str:
    return "\n".join(
        [
            f"SESSION {session_id}",
            "STATUS waiting-for-feedback",
            "PLAN",
            "- inspect workspace",
            "- make focused change",
            "- run bazel test //...",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--session-id", default="fake-agent-1")
    parser.add_argument("--echo-stdin", action="store_true")
    args = parser.parse_args()

    print(emit_plan(args.session_id))
    if args.echo_stdin:
        for line in sys.stdin:
            print(f"FEEDBACK {line.rstrip()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

