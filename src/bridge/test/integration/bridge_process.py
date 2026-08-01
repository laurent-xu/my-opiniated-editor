import json
import os
from pathlib import Path
import socket
import subprocess
import time
import urllib.error
import urllib.request


def runfile_path(path: str) -> str:
    return os.path.join(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], path)


def free_loopback_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.bind(("127.0.0.1", 0))
        return int(server.getsockname()[1])


def bridge_state_directory(port: int) -> str:
    return os.path.join(
        os.environ["TEST_TMPDIR"], f"bridge-state-{port}", "my-opiniated-editor"
    )


def register_worktree_repository(port: int, repository: Path, porcelain: str) -> None:
    registry_path = Path(bridge_state_directory(port)) / "worktrees.pb"
    subprocess.run(
        [
            runfile_path("src/parent/workspace_parent"),
            "--register-worktree-repository",
            str(registry_path),
            str(repository),
        ],
        env={
            **os.environ,
            "MOE_GIT_EXECUTABLE": runfile_path("test/fixtures/fake_git"),
            "MOE_FAKE_GIT_WORKTREE_LIST": porcelain,
        },
        check=True,
        capture_output=True,
        text=True,
    )


def wait_for_health(
    port: int,
    process: subprocess.Popen,
    timeout_seconds: float = 10.0,
    token: str | None = None,
) -> dict:
    deadline = time.monotonic() + timeout_seconds
    path = "/health"
    if token is not None:
        path = f"/health?token={token}"
    url = f"http://127.0.0.1:{port}{path}"
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stdout, stderr = process.communicate()
            raise AssertionError(
                "parent bridge exited before serving health\n"
                f"stdout:\n{stdout}\n"
                f"stderr:\n{stderr}\n"
            )

        try:
            with urllib.request.urlopen(url, timeout=0.25) as response:
                return json.loads(response.read().decode())
        except (
            ConnectionError,
            TimeoutError,
            urllib.error.URLError,
            json.JSONDecodeError,
        ):
            time.sleep(0.05)

    raise AssertionError(f"timed out waiting for bridge health endpoint at {url}")


def fetch_text(port: int, path: str) -> str:
    with urllib.request.urlopen(
        f"http://127.0.0.1:{port}{path}", timeout=5
    ) as response:
        return response.read().decode()


def start_bridge(
    port: int,
    extra_args: list[str] | None = None,
    extra_environment: dict[str, str] | None = None,
) -> subprocess.Popen:
    environment = dict(os.environ)
    environment["XDG_STATE_HOME"] = os.path.join(
        os.environ["TEST_TMPDIR"], "shared-bridge-xdg-state"
    )
    environment["MOE_FZF_EXECUTABLE"] = runfile_path("test/fixtures/fake_fzf")
    if extra_environment is not None:
        environment.update(extra_environment)
    command = [
        runfile_path("src/bridge/parent_ws_bridge"),
        "--port",
        str(port),
        "--interface",
        "127.0.0.1",
        "--parent",
        runfile_path("src/parent/workspace_parent"),
        "--cwd",
        os.environ["TEST_TMPDIR"],
        "--state-directory",
        bridge_state_directory(port),
    ]
    if extra_args is not None:
        command.extend(extra_args)
    return subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=environment,
    )


def stop_bridge(process: subprocess.Popen):
    if process.poll() is None:
        process.terminate()
        try:
            process.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate(timeout=5)
    else:
        process.communicate(timeout=5)
