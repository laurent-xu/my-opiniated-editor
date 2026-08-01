#!/usr/bin/env python3
"""Small HTTPS/authentication reverse proxy for the loopback C++ bridge."""

from __future__ import annotations

import argparse
import base64
import binascii
from dataclasses import dataclass
import getpass
import hashlib
import hmac
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import secrets
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading


SCRYPT_N = 16_384
SCRYPT_R = 8
SCRYPT_P = 1
PASSWORD_FORMAT = "moe-scrypt-v1"
HOP_BY_HOP_HEADERS = {
    "connection",
    "keep-alive",
    "proxy-authenticate",
    "proxy-authorization",
    "te",
    "trailers",
    "transfer-encoding",
    "upgrade",
}


@dataclass(frozen=True)
class PasswordRecord:
    username: str
    salt: bytes
    digest: bytes


@dataclass(frozen=True)
class SecretPaths:
    password_file: Path
    certificate: Path
    certificate_key: Path


def default_secret_paths() -> SecretPaths:
    secret_directory = Path(
        os.environ.get("MOE_BRIDGE_SECRETS_DIRECTORY", Path.home() / ".secrets")
    )
    return SecretPaths(
        password_file=Path(
            os.environ.get(
                "MOE_BRIDGE_PASSWORD_FILE",
                secret_directory / "my-opiniated-editor-password",
            )
        ),
        certificate=Path(
            os.environ.get(
                "MOE_BRIDGE_TLS_CERTIFICATE",
                secret_directory / "my-opiniated-editor-certificate.pem",
            )
        ),
        certificate_key=Path(
            os.environ.get(
                "MOE_BRIDGE_TLS_CERTIFICATE_KEY",
                secret_directory / "my-opiniated-editor-certificate-key.pem",
            )
        ),
    )


def _password_digest(password: str, salt: bytes) -> bytes:
    return hashlib.scrypt(
        password.encode("utf-8"),
        salt=salt,
        n=SCRYPT_N,
        r=SCRYPT_R,
        p=SCRYPT_P,
        dklen=32,
    )


def encode_password_record(
    username: str, password: str, *, salt: bytes | None = None
) -> str:
    if not username or ":" in username or "$" in username:
        raise ValueError("username contains unsupported characters")
    record_salt = secrets.token_bytes(16) if salt is None else salt
    digest = _password_digest(password, record_salt)
    encoded_salt = base64.b64encode(record_salt).decode("ascii")
    encoded_digest = base64.b64encode(digest).decode("ascii")
    return (
        f"{username}:{PASSWORD_FORMAT}${SCRYPT_N}${SCRYPT_R}${SCRYPT_P}"
        f"${encoded_salt}${encoded_digest}\n"
    )


def parse_password_record(encoded: str) -> PasswordRecord:
    username, separator, value = encoded.strip().partition(":")
    if not separator:
        raise ValueError("password file is missing its username")
    parts = value.split("$")
    if len(parts) != 6 or parts[0] != PASSWORD_FORMAT:
        raise ValueError("password file has an unsupported format")
    if tuple(map(int, parts[1:4])) != (SCRYPT_N, SCRYPT_R, SCRYPT_P):
        raise ValueError("password file has unsupported scrypt parameters")
    try:
        salt = base64.b64decode(parts[4], validate=True)
        digest = base64.b64decode(parts[5], validate=True)
    except (binascii.Error, ValueError) as error:
        raise ValueError("password file contains invalid base64") from error
    if not username or len(salt) != 16 or len(digest) != 32:
        raise ValueError("password file has invalid field lengths")
    return PasswordRecord(username=username, salt=salt, digest=digest)


def read_password_record(path: Path) -> PasswordRecord:
    return parse_password_record(path.read_text(encoding="utf-8"))


def verify_basic_authorization(
    authorization: str | None, record: PasswordRecord
) -> bool:
    if authorization is None or not authorization.startswith("Basic "):
        return False
    try:
        decoded = base64.b64decode(authorization[6:], validate=True).decode("utf-8")
    except (ValueError, UnicodeDecodeError):
        return False
    username, separator, password = decoded.partition(":")
    if not separator:
        return False
    candidate = _password_digest(password, record.salt)
    return hmac.compare_digest(username, record.username) and hmac.compare_digest(
        candidate, record.digest
    )


def _write_private_file(path: Path, contents: str) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    path.parent.chmod(0o700)
    descriptor, temporary_name = tempfile.mkstemp(dir=path.parent)
    temporary_path = Path(temporary_name)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(contents)
        temporary_path.replace(path)
        path.chmod(0o600)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def _lan_subject_alt_names() -> list[str]:
    host_name = socket.gethostname()
    entries = [
        f"DNS:{host_name}",
        f"DNS:{host_name}.local",
        "DNS:localhost",
        "IP:127.0.0.1",
    ]
    try:
        addresses = subprocess.run(
            ["hostname", "-I"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.split()
    except (OSError, subprocess.CalledProcessError):
        addresses = []
    for address in addresses:
        try:
            socket.inet_pton(socket.AF_INET, address)
        except OSError:
            continue
        entry = f"IP:{address}"
        if entry not in entries:
            entries.append(entry)
    return entries


def _generate_certificate(paths: SecretPaths) -> None:
    if paths.certificate.exists() or paths.certificate_key.exists():
        if (
            not paths.certificate.is_file()
            or not paths.certificate_key.is_file()
            or paths.certificate.stat().st_size == 0
            or paths.certificate_key.stat().st_size == 0
        ):
            raise RuntimeError("TLS certificate pair is incomplete")
        return
    openssl = os.environ.get("MOE_OPENSSL_EXECUTABLE") or shutil.which("openssl")
    if openssl is None or not os.access(openssl, os.X_OK):
        raise RuntimeError("openssl is required to generate the TLS certificate")
    paths.certificate.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    paths.certificate_key.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    paths.certificate.parent.chmod(0o700)
    paths.certificate_key.parent.chmod(0o700)
    with tempfile.TemporaryDirectory(dir=paths.certificate.parent) as temporary:
        temporary_directory = Path(temporary)
        certificate = temporary_directory / "certificate.pem"
        certificate_key = temporary_directory / "certificate-key.pem"
        subprocess.run(
            [
                openssl,
                "req",
                "-x509",
                "-newkey",
                "rsa:3072",
                "-sha256",
                "-nodes",
                "-days",
                "3650",
                "-subj",
                f"/CN={socket.gethostname()}",
                "-addext",
                f"subjectAltName={','.join(_lan_subject_alt_names())}",
                "-keyout",
                str(certificate_key),
                "-out",
                str(certificate),
            ],
            check=True,
        )
        certificate.replace(paths.certificate)
        certificate_key.replace(paths.certificate_key)
    paths.certificate.chmod(0o600)
    paths.certificate_key.chmod(0o600)


def create_secrets(username: str, paths: SecretPaths) -> None:
    password = getpass.getpass("Password: ")
    confirmation = getpass.getpass("Confirm password: ")
    if not password:
        raise ValueError("password must not be empty")
    if password != confirmation:
        raise ValueError("passwords do not match")
    _write_private_file(paths.password_file, encode_password_record(username, password))
    _generate_certificate(paths)
    print(f"HTTPS secrets ready in {paths.password_file.parent}")


class ProxyServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self,
        address: tuple[str, int],
        upstream_port: int,
        password_record: PasswordRecord,
    ) -> None:
        super().__init__(address, ProxyHandler)
        self.upstream_port = upstream_port
        self.password_record = password_record


class ProxyHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "moe-https"
    sys_version = ""

    @property
    def proxy_server(self) -> ProxyServer:
        assert isinstance(self.server, ProxyServer)
        return self.server

    def do_GET(self) -> None:
        self._proxy_request()

    def do_HEAD(self) -> None:
        self._proxy_request()

    def _proxy_request(self) -> None:
        if not verify_basic_authorization(
            self.headers.get("Authorization"), self.proxy_server.password_record
        ):
            body = b"authentication required\n"
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="My Opiniated Editor"')
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)
            self.close_connection = True
            return
        if self.headers.get("Upgrade", "").lower() == "websocket":
            self._proxy_websocket()
            return
        self._proxy_http()

    def _proxy_http(self) -> None:
        headers = {
            name: value
            for name, value in self.headers.items()
            if name.lower() not in HOP_BY_HOP_HEADERS
            and name.lower() != "authorization"
        }
        headers["Host"] = f"127.0.0.1:{self.proxy_server.upstream_port}"
        headers["X-Forwarded-Proto"] = "https"
        connection = http.client.HTTPConnection(
            "127.0.0.1", self.proxy_server.upstream_port, timeout=10
        )
        try:
            connection.request(self.command, self.path, headers=headers)
            response = connection.getresponse()
            response_body = response.read()
        except (OSError, http.client.HTTPException) as error:
            self.send_error(502, f"bridge unavailable: {error}")
            return
        finally:
            connection.close()
        self.send_response(response.status, response.reason)
        for name, value in response.getheaders():
            if (
                name.lower() not in HOP_BY_HOP_HEADERS
                and name.lower() != "content-length"
            ):
                self.send_header(name, value)
        self.send_header("Content-Length", str(len(response_body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(response_body)

    def _upstream_websocket_request(self) -> bytes:
        lines = [f"GET {self.path} HTTP/1.1"]
        for name, value in self.headers.items():
            if name.lower() in {"authorization", "proxy-authorization", "host"}:
                continue
            lines.append(f"{name}: {value}")
        lines.append(f"Host: 127.0.0.1:{self.proxy_server.upstream_port}")
        return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1")

    @staticmethod
    def _read_response_headers(upstream: socket.socket) -> bytes:
        response = bytearray()
        while b"\r\n\r\n" not in response:
            chunk = upstream.recv(4096)
            if not chunk:
                raise ConnectionError("bridge closed during WebSocket handshake")
            response.extend(chunk)
            if len(response) > 65_536:
                raise ConnectionError("bridge WebSocket response headers are too large")
        return bytes(response)

    @staticmethod
    def _pump(source: socket.socket, destination: socket.socket) -> None:
        try:
            while data := source.recv(65_536):
                destination.sendall(data)
        except OSError:
            pass
        finally:
            for current in (source, destination):
                try:
                    current.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

    def _proxy_websocket(self) -> None:
        self.close_connection = True
        try:
            upstream = socket.create_connection(
                ("127.0.0.1", self.proxy_server.upstream_port), timeout=10
            )
            upstream.sendall(self._upstream_websocket_request())
            response = self._read_response_headers(upstream)
            self.connection.sendall(response)
        except OSError as error:
            self.send_error(502, f"bridge unavailable: {error}")
            return
        if not response.startswith(b"HTTP/1.1 101 "):
            upstream.close()
            return
        upstream.settimeout(None)
        browser_to_bridge = threading.Thread(
            target=self._pump, args=(self.connection, upstream), daemon=True
        )
        browser_to_bridge.start()
        self._pump(upstream, self.connection)
        browser_to_bridge.join(timeout=1)
        upstream.close()

    def log_message(self, format_string: str, *args: object) -> None:
        print(f"https-proxy client={self.client_address[0]} {format_string % args}")


def serve_https(
    interface: str,
    public_port: int,
    upstream_port: int,
    paths: SecretPaths,
) -> None:
    password_record = read_password_record(paths.password_file)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(paths.certificate, paths.certificate_key)
    server = ProxyServer((interface, public_port), upstream_port, password_record)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print(
        f"https-proxy listening interface={interface} port={public_port} "
        f"upstream=127.0.0.1:{upstream_port}"
    )
    try:
        server.serve_forever()
    finally:
        server.server_close()


def _network_port(value: str) -> int:
    port = int(value)
    if not 1 <= port <= 65_535:
        raise argparse.ArgumentTypeError("port must be between 1 and 65535")
    return port


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    create = commands.add_parser("create-secrets")
    create.add_argument("--username", required=True)

    serve = commands.add_parser("serve")
    serve.add_argument("--https-port", required=True, type=_network_port)
    serve.add_argument("--http-port", required=True, type=_network_port)
    serve.add_argument(
        "--interface",
        default=os.environ.get("MOE_BRIDGE_HTTPS_INTERFACE", "0.0.0.0"),
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    paths = default_secret_paths()
    if args.command == "create-secrets":
        create_secrets(args.username, paths)
        return
    serve_https(
        args.interface,
        args.https_port,
        args.http_port,
        paths,
    )


if __name__ == "__main__":
    main()
