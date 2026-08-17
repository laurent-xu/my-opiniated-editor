import base64
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
import socket
import sys
import threading
import unittest
from unittest.mock import patch

sys.path.append(os.path.dirname(__file__))

from https_proxy import (
    ProxyServer,
    encode_password_record,
    parse_password_record,
    parse_args,
    validate_allowed_origin,
    validate_allowed_origins,
    verify_basic_authorization,
)


USERNAME = "notmyfoo"
PASSWORD = "test-password"
ALLOWED_ORIGIN = "https://editor.example.ts.net"


def basic_authorization(username: str, password: str) -> str:
    encoded = base64.b64encode(f"{username}:{password}".encode()).decode()
    return f"Basic {encoded}"


class UpstreamHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = f"upstream path={self.path}\n".encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format, *_args):
        pass


class HttpsProxyTest(unittest.TestCase):
    def setUp(self):
        encoded = encode_password_record(USERNAME, PASSWORD, salt=b"0123456789abcdef")
        self.record = parse_password_record(encoded)

    def test_password_record_contains_scrypt_hash_not_plaintext(self):
        encoded = encode_password_record(USERNAME, PASSWORD, salt=b"0123456789abcdef")

        self.assertTrue(encoded.startswith(f"{USERNAME}:moe-scrypt-v1$"))
        self.assertNotIn(PASSWORD, encoded)
        self.assertEqual(parse_password_record(encoded), self.record)

    def test_basic_authorization_verifies_username_and_password(self):
        self.assertTrue(
            verify_basic_authorization(
                basic_authorization(USERNAME, PASSWORD), self.record
            )
        )
        self.assertFalse(
            verify_basic_authorization(
                basic_authorization(USERNAME, "wrong"), self.record
            )
        )
        self.assertFalse(
            verify_basic_authorization(
                basic_authorization("another-user", PASSWORD), self.record
            )
        )
        self.assertFalse(verify_basic_authorization(None, self.record))

    def test_allowed_origin_requires_an_exact_https_origin(self):
        self.assertEqual(validate_allowed_origin(ALLOWED_ORIGIN), ALLOWED_ORIGIN)
        self.assertEqual(
            validate_allowed_origin("https://editor.example.ts.net:8443"),
            "https://editor.example.ts.net:8443",
        )

        for origin in [
            "http://editor.example.ts.net",
            "https://editor.example.ts.net/",
            "https://editor.example.ts.net/path",
            "https://user:password@editor.example.ts.net",
            "https://editor.example.ts.net?query",
            "https://editor.example.ts.net#fragment",
        ]:
            with self.subTest(origin=origin):
                with self.assertRaisesRegex(ValueError, "must be an HTTPS origin"):
                    validate_allowed_origin(origin)

    def test_allowed_origins_accepts_an_explicit_comma_separated_allowlist(self):
        local_origin = "https://127.0.0.1:7683"

        self.assertEqual(
            validate_allowed_origins(f"{ALLOWED_ORIGIN}, {local_origin}"),
            frozenset({ALLOWED_ORIGIN, local_origin}),
        )
        self.assertEqual(validate_allowed_origins(None), frozenset())
        with self.assertRaisesRegex(ValueError, "comma-separated list"):
            validate_allowed_origins(f"{ALLOWED_ORIGIN},")

    def test_serve_defaults_to_loopback_and_accepts_configured_origin(self):
        with patch.dict(
            os.environ,
            {"MOE_BRIDGE_ALLOWED_ORIGIN": ALLOWED_ORIGIN},
            clear=True,
        ):
            with patch.object(
                sys,
                "argv",
                [
                    "https_proxy.py",
                    "serve",
                    "--http-port",
                    "17683",
                    "--https-port",
                    "7683",
                ],
            ):
                args = parse_args()

        self.assertEqual(args.interface, "127.0.0.1")
        self.assertEqual(args.allowed_origin, ALLOWED_ORIGIN)

    def test_http_proxy_requires_auth_and_forwards_authorized_request(self):
        upstream = ThreadingHTTPServer(("127.0.0.1", 0), UpstreamHandler)
        proxy = ProxyServer(("127.0.0.1", 0), upstream.server_port, self.record)
        threads = [
            threading.Thread(target=upstream.serve_forever, daemon=True),
            threading.Thread(target=proxy.serve_forever, daemon=True),
        ]
        for thread in threads:
            thread.start()
        try:
            connection = http.client.HTTPConnection("127.0.0.1", proxy.server_port)
            connection.request("GET", "/health")
            unauthorized = connection.getresponse()
            self.assertEqual(unauthorized.status, 401)
            unauthorized.read()
            connection.close()

            connection = http.client.HTTPConnection("127.0.0.1", proxy.server_port)
            connection.request(
                "GET",
                "/health",
                headers={"Authorization": basic_authorization(USERNAME, PASSWORD)},
            )
            response = connection.getresponse()
            self.assertEqual(response.status, 200)
            self.assertEqual(response.read(), b"upstream path=/health\n")
            connection.close()
        finally:
            proxy.shutdown()
            upstream.shutdown()
            proxy.server_close()
            upstream.server_close()

    def test_failed_authentication_globally_delays_the_next_attempt(self):
        current_time = [100.0]
        upstream = ThreadingHTTPServer(("127.0.0.1", 0), UpstreamHandler)
        proxy = ProxyServer(
            ("127.0.0.1", 0),
            upstream.server_port,
            self.record,
            monotonic_time=lambda: current_time[0],
        )
        threads = [
            threading.Thread(target=upstream.serve_forever, daemon=True),
            threading.Thread(target=proxy.serve_forever, daemon=True),
        ]
        for thread in threads:
            thread.start()

        def request(password):
            connection = http.client.HTTPConnection("127.0.0.1", proxy.server_port)
            connection.request(
                "GET",
                "/health",
                headers={"Authorization": basic_authorization(USERNAME, password)},
            )
            response = connection.getresponse()
            status = response.status
            retry_after = response.getheader("Retry-After")
            response.read()
            connection.close()
            return status, retry_after

        try:
            self.assertEqual(request("wrong"), (401, None))
            self.assertEqual(
                proxy.authenticate(basic_authorization(USERNAME, PASSWORD)),
                (False, 3),
            )
            self.assertEqual(request(PASSWORD), (429, "3"))

            current_time[0] += 2
            self.assertEqual(request(PASSWORD), (429, "1"))

            current_time[0] += 1
            self.assertEqual(request(PASSWORD), (200, None))
        finally:
            proxy.shutdown()
            upstream.shutdown()
            proxy.server_close()
            upstream.server_close()

    def test_websocket_upgrade_is_authenticated_and_tunneled(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)

        def fake_websocket_bridge():
            connection, _address = listener.accept()
            with connection:
                request = bytearray()
                while b"\r\n\r\n" not in request:
                    request.extend(connection.recv(4096))
                connection.sendall(
                    b"HTTP/1.1 101 Switching Protocols\r\n"
                    b"Upgrade: websocket\r\n"
                    b"Connection: Upgrade\r\n\r\n"
                )
                connection.sendall(b"bridge:" + connection.recv(4096))

        upstream_thread = threading.Thread(target=fake_websocket_bridge, daemon=True)
        upstream_thread.start()
        proxy = ProxyServer(
            ("127.0.0.1", 0),
            listener.getsockname()[1],
            self.record,
            allowed_origin=ALLOWED_ORIGIN,
        )
        proxy_thread = threading.Thread(target=proxy.serve_forever, daemon=True)
        proxy_thread.start()
        try:
            with socket.create_connection(("127.0.0.1", proxy.server_port)) as client:
                authorization = basic_authorization(USERNAME, PASSWORD)
                client.sendall(
                    (
                        "GET /ws HTTP/1.1\r\n"
                        "Host: example\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        f"Origin: {ALLOWED_ORIGIN}\r\n"
                        f"Authorization: {authorization}\r\n\r\n"
                    ).encode()
                )
                response = bytearray()
                while b"\r\n\r\n" not in response:
                    response.extend(client.recv(4096))
                self.assertTrue(response.startswith(b"HTTP/1.1 101 "))
                client.sendall(b"payload")
                self.assertEqual(client.recv(4096), b"bridge:payload")
        finally:
            proxy.shutdown()
            proxy.server_close()
            listener.close()
            upstream_thread.join(timeout=1)

    def test_websocket_upgrade_rejects_missing_or_mismatched_origin(self):
        unused_upstream = socket.socket()
        unused_upstream.bind(("127.0.0.1", 0))
        unused_upstream_port = unused_upstream.getsockname()[1]
        unused_upstream.close()

        proxy = ProxyServer(
            ("127.0.0.1", 0),
            unused_upstream_port,
            self.record,
            allowed_origin=ALLOWED_ORIGIN,
        )
        proxy_thread = threading.Thread(target=proxy.serve_forever, daemon=True)
        proxy_thread.start()
        try:
            for origin in [None, "https://evil.example"]:
                with self.subTest(origin=origin):
                    with socket.create_connection(
                        ("127.0.0.1", proxy.server_port)
                    ) as client:
                        authorization = basic_authorization(USERNAME, PASSWORD)
                        origin_header = (
                            "" if origin is None else f"Origin: {origin}\r\n"
                        )
                        client.sendall(
                            (
                                "GET /ws HTTP/1.1\r\n"
                                "Host: example\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                f"{origin_header}"
                                f"Authorization: {authorization}\r\n\r\n"
                            ).encode()
                        )
                        response = bytearray()
                        while b"\r\n\r\n" not in response:
                            response.extend(client.recv(4096))
                        self.assertTrue(response.startswith(b"HTTP/1.1 403 "))
        finally:
            proxy.shutdown()
            proxy.server_close()


if __name__ == "__main__":
    unittest.main()
