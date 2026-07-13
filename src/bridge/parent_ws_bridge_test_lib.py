import base64
import json
import os
import socket
import struct
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


def shell_marker_command(marker: str) -> bytes:
    escaped = "".join(f"\\{ord(character):03o}" for character in marker)
    return f"printf '{escaped}\\012'\n".encode()


class WebSocketClient:
    def __init__(self, port: int, token: str | None = None):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(5)
        self.pending = b""

        key = base64.b64encode(os.urandom(16)).decode()
        path = "/ws"
        if token is not None:
            path = f"/ws?token={token}"
        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: 127.0.0.1:{port}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: workspace-pty\r\n"
            f"Origin: http://127.0.0.1:{port}\r\n"
            "\r\n"
        )
        self.sock.sendall(request.encode())

        response = b""
        while b"\r\n\r\n" not in response:
            response += self.sock.recv(4096)

        header_bytes, self.pending = response.split(b"\r\n\r\n", 1)
        header_text = header_bytes.decode(errors="replace")
        if " 101 " not in header_text.splitlines()[0]:
            raise AssertionError(f"websocket upgrade failed: {header_text!r}")

    def close(self):
        self.send_close()
        self.sock.close()

    def send_close(self):
        self._send_frame(opcode=0x8, payload=b"")

    def send_binary(self, payload: bytes):
        self._send_frame(opcode=0x2, payload=payload)

    def send_terminal_input(self, payload: bytes):
        self.send_binary(b"0" + payload)

    def send_shell_marker(self, marker: str):
        self.send_terminal_input(shell_marker_command(marker))

    def _send_frame(self, opcode: int, payload: bytes):
        mask = os.urandom(4)
        first_byte = 0x80 | opcode
        length = len(payload)
        if length < 126:
            header = struct.pack("!BB", first_byte, 0x80 | length)
        elif length <= 0xFFFF:
            header = struct.pack("!BBH", first_byte, 0x80 | 126, length)
        else:
            header = struct.pack("!BBQ", first_byte, 0x80 | 127, length)

        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def read_exact(self, size: int) -> bytes:
        chunks = []
        if self.pending:
            chunk = self.pending[:size]
            self.pending = self.pending[size:]
            chunks.append(chunk)
            size -= len(chunk)

        while size > 0:
            chunk = self.sock.recv(size)
            if not chunk:
                raise ConnectionError("socket closed while reading websocket frame")
            chunks.append(chunk)
            size -= len(chunk)
        return b"".join(chunks)

    def read_frame(self) -> tuple[int, bytes]:
        first_byte, second_byte = self.read_exact(2)
        opcode = first_byte & 0x0F
        masked = (second_byte & 0x80) != 0
        length = second_byte & 0x7F
        if length == 126:
            length = struct.unpack("!H", self.read_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self.read_exact(8))[0]

        mask = self.read_exact(4) if masked else b""
        payload = self.read_exact(length)
        if masked:
            payload = bytes(
                byte ^ mask[index % 4] for index, byte in enumerate(payload)
            )
        return opcode, payload

    def read_terminal_output_until(
        self, needle: str, timeout_seconds: float = 5.0
    ) -> str:
        deadline = time.monotonic() + timeout_seconds
        output = bytearray()

        while time.monotonic() < deadline:
            self.sock.settimeout(max(0.1, deadline - time.monotonic()))
            opcode, payload = self.read_frame()
            if opcode == 0x8:
                break
            if opcode not in (0x1, 0x2) or not payload:
                continue

            command = chr(payload[0])
            data = payload[1:]
            if command == "0":
                output.extend(data)

            text = output.decode(errors="replace")
            if needle in text:
                return text

        text = output.decode(errors="replace")
        raise AssertionError(f"timed out waiting for {needle!r}; output was {text!r}")


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


def start_bridge(port: int, extra_args: list[str] | None = None) -> subprocess.Popen:
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
    ]
    if extra_args is not None:
        command.extend(extra_args)
    return subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
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
