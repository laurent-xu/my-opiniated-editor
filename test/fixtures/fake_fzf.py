import json
import os
import pathlib
import sys
import termios
import tty


def write_json_log(environment_name: str, value: object) -> None:
    path = os.environ.get(environment_name)
    if not path:
        return
    pathlib.Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "a", encoding="utf-8") as output:
        output.write(json.dumps(value))
        output.write("\n")


def main() -> int:
    candidate_bytes = sys.stdin.buffer.read()
    candidates = [
        candidate.decode(errors="surrogateescape")
        for candidate in candidate_bytes.split(b"\0")
        if candidate
    ]
    write_json_log("MOE_FAKE_FZF_CANDIDATES_LOG", candidates)

    tty_descriptor = os.open("/dev/tty", os.O_RDWR)
    original_attributes = termios.tcgetattr(tty_descriptor)
    tty.setraw(tty_descriptor)
    selected_index = 0
    received = bytearray()
    pending = bytearray()
    try:
        os.write(
            tty_descriptor,
            ("\x1b[2J\x1b[HWorktree picker\n" + "\n".join(candidates)).encode(
                errors="surrogateescape"
            ),
        )
        while True:
            chunk = os.read(tty_descriptor, 64)
            if not chunk:
                return 130
            received.extend(chunk)
            pending.extend(chunk)
            if b"\x1b[B" in pending and candidates:
                selected_index = min(selected_index + 1, len(candidates) - 1)
                arrow_index = pending.index(b"\x1b[B")
                del pending[arrow_index : arrow_index + 3]
            if b"\r" in pending or b"\n" in pending:
                if candidates:
                    sys.stdout.buffer.write(
                        candidates[selected_index].encode(errors="surrogateescape")
                        + b"\0"
                    )
                    sys.stdout.buffer.flush()
                return 0
    finally:
        write_json_log("MOE_FAKE_FZF_INPUT_LOG", received.hex())
        termios.tcsetattr(tty_descriptor, termios.TCSANOW, original_attributes)
        os.close(tty_descriptor)


if __name__ == "__main__":
    raise SystemExit(main())
