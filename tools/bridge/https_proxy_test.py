import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import socket
import sqlite3
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from urllib.parse import urlencode

sys.path.append(os.path.dirname(__file__))

from https_proxy import (
    AUTHENTICATION_ATTEMPT_RETENTION_SECONDS,
    PERSISTENT_SESSION_DURATION_SECONDS,
    SECURITY_SUMMARY_INTERVAL_SECONDS,
    SESSION_COOKIE_NAME,
    SESSION_DURATION_SECONDS,
    ProxyServer,
    SessionStore,
    default_session_database,
    encode_password_record,
    parse_args,
    parse_password_record,
    validate_allowed_origin,
    validate_allowed_origins,
    verify_credentials,
)


USERNAME = "notmyfoo"
PASSWORD = "test-password"
ALLOWED_ORIGIN = "https://editor.example.ts.net"


class UpstreamHandler(BaseHTTPRequestHandler):
    received_cookies: list[str | None] = []

    def do_GET(self):
        self.received_cookies.append(self.headers.get("Cookie"))
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
        UpstreamHandler.received_cookies.clear()

    def proxy_server(
        self,
        upstream_port: int,
        *,
        allowed_origin: str | None = None,
        monotonic_time=time.monotonic,
        wall_time=time.time,
    ) -> ProxyServer:
        session_store = SessionStore(":memory:", self.record, current_time=wall_time)
        return ProxyServer(
            ("127.0.0.1", 0),
            upstream_port,
            self.record,
            session_store,
            allowed_origin=allowed_origin,
            monotonic_time=monotonic_time,
            wall_time=wall_time,
        )

    @staticmethod
    def start(*servers: ThreadingHTTPServer) -> list[threading.Thread]:
        threads = []
        for server in servers:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            threads.append(thread)
        return threads

    @staticmethod
    def request(
        port: int,
        method: str,
        path: str,
        *,
        body: bytes | None = None,
        headers: dict[str, str] | None = None,
    ) -> tuple[int, http.client.HTTPMessage, bytes]:
        connection = http.client.HTTPConnection("127.0.0.1", port)
        connection.request(method, path, body=body, headers=headers or {})
        response = connection.getresponse()
        result = (response.status, response.headers, response.read())
        connection.close()
        return result

    def login(
        self,
        port: int,
        *,
        username: str = USERNAME,
        password: str = PASSWORD,
        stay_connected: bool = False,
    ) -> tuple[int, http.client.HTTPMessage, bytes]:
        fields = {"username": username, "password": password}
        if stay_connected:
            fields["stay_connected"] = "on"
        body = urlencode(fields).encode()
        return self.request(
            port,
            "POST",
            "/auth/login",
            body=body,
            headers={
                "Content-Type": "application/x-www-form-urlencoded",
                "Content-Length": str(len(body)),
            },
        )

    @staticmethod
    def cookie_header(response_headers: http.client.HTTPMessage) -> str:
        return response_headers["Set-Cookie"].partition(";")[0]

    def test_password_record_contains_scrypt_hash_not_plaintext(self):
        encoded = encode_password_record(USERNAME, PASSWORD, salt=b"0123456789abcdef")

        self.assertTrue(encoded.startswith(f"{USERNAME}:moe-scrypt-v1$"))
        self.assertNotIn(PASSWORD, encoded)
        self.assertEqual(parse_password_record(encoded), self.record)

    def test_credentials_verify_username_and_password(self):
        self.assertTrue(verify_credentials(USERNAME, PASSWORD, self.record))
        self.assertFalse(verify_credentials(USERNAME, "wrong", self.record))
        self.assertFalse(verify_credentials("another-user", PASSWORD, self.record))

    def test_session_store_persists_only_a_token_hash_and_honors_expiry(self):
        current_time = [1_000.0]
        with tempfile.TemporaryDirectory() as temp_dir:
            database = Path(temp_dir) / "state" / "browser-sessions.sqlite3"
            store = SessionStore(
                database, self.record, current_time=lambda: current_time[0]
            )
            token, expires_at = store.create(USERNAME, SESSION_DURATION_SECONDS)
            self.assertEqual(expires_at, 1_000 + 3 * 60 * 60)
            self.assertEqual(store.authenticate(token).username, USERNAME)
            store.close()

            self.assertEqual(database.stat().st_mode & 0o777, 0o600)
            connection = sqlite3.connect(database)
            stored_digest = connection.execute(
                "SELECT token_digest FROM sessions"
            ).fetchone()[0]
            connection.close()
            self.assertNotEqual(stored_digest, token.encode())

            store = SessionStore(
                database, self.record, current_time=lambda: current_time[0]
            )
            current_time[0] = expires_at - 1
            self.assertIsNotNone(store.authenticate(token))
            current_time[0] = expires_at
            self.assertIsNone(store.authenticate(token))
            store.close()

    def test_password_change_invalidates_persisted_sessions(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            database = Path(temp_dir) / "browser-sessions.sqlite3"
            store = SessionStore(database, self.record)
            token, _expires_at = store.create(USERNAME, SESSION_DURATION_SECONDS)
            store.close()

            changed_record = parse_password_record(
                encode_password_record(
                    USERNAME, "replacement-password", salt=b"fedcba9876543210"
                )
            )
            store = SessionStore(database, changed_record)
            self.assertIsNone(store.authenticate(token))
            store.close()

    def test_existing_session_database_is_migrated_for_security_summaries(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            database = Path(temp_dir) / "browser-sessions.sqlite3"
            connection = sqlite3.connect(database)
            connection.execute(
                """
                CREATE TABLE sessions (
                    token_digest BLOB PRIMARY KEY,
                    username TEXT NOT NULL,
                    password_fingerprint BLOB NOT NULL,
                    expires_at INTEGER NOT NULL
                )
                """
            )
            connection.commit()
            connection.close()

            store = SessionStore(database, self.record)
            connection = sqlite3.connect(database)
            columns = {
                row[1] for row in connection.execute("PRAGMA table_info(sessions)")
            }
            attempt_table = connection.execute(
                """
                SELECT name FROM sqlite_master
                WHERE type = 'table' AND name = 'authentication_attempts'
                """
            ).fetchone()
            connection.close()
            store.close()

            self.assertIn("security_summary_shown_at", columns)
            self.assertEqual(attempt_table, ("authentication_attempts",))

    def test_security_summary_claim_is_atomic_and_due_every_twenty_four_hours(self):
        current_time = [10_000.0]
        store = SessionStore(
            ":memory:", self.record, current_time=lambda: current_time[0]
        )
        token, _expires_at = store.create(USERNAME, PERSISTENT_SESSION_DURATION_SECONDS)

        self.assertTrue(store.claim_security_summary(token))
        self.assertFalse(store.claim_security_summary(token))
        current_time[0] += SECURITY_SUMMARY_INTERVAL_SECONDS - 1
        self.assertFalse(store.claim_security_summary(token))
        current_time[0] += 1
        self.assertTrue(store.claim_security_summary(token))
        self.assertTrue(store.claim_security_summary(token, force=True))
        store.close()

    def test_security_summary_counts_failures_sources_and_active_sessions(self):
        current_time = [20_000_000.0]
        store = SessionStore(
            ":memory:", self.record, current_time=lambda: current_time[0]
        )
        store.create(USERNAME, SESSION_DURATION_SECONDS)
        store.record_authentication_attempt("203.0.113.10", "invalid_credentials")
        current_time[0] += 1
        store.record_authentication_attempt("203.0.113.10", "rate_limited")
        current_time[0] += 1
        store.record_authentication_attempt("2001:db8::1", "malformed")
        current_time[0] += 1
        store.record_authentication_attempt("2001:db8::1", "success")

        summary = store.security_summary()

        self.assertEqual(summary.retained.total, 4)
        self.assertEqual(summary.retained.successful, 1)
        self.assertEqual(summary.retained.failed, 3)
        self.assertEqual(summary.retained.invalid_credentials, 1)
        self.assertEqual(summary.retained.rate_limited, 1)
        self.assertEqual(summary.retained.malformed, 1)
        self.assertEqual(summary.retained.distinct_source_ips, 2)
        self.assertEqual(summary.last_24_hours, summary.retained)
        self.assertEqual(summary.active_sessions, 1)
        self.assertEqual(
            [source.source_ip for source in summary.sources],
            [
                "2001:db8::1",
                "203.0.113.10",
            ],
        )
        self.assertFalse(summary.source_ip_visibility_limited)

        current_time[0] += AUTHENTICATION_ATTEMPT_RETENTION_SECONDS + 1
        store.record_authentication_attempt("127.0.0.1", "success")
        summary = store.security_summary()
        self.assertEqual(summary.retained.total, 1)
        self.assertTrue(summary.source_ip_visibility_limited)
        store.close()

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

    def test_session_database_defaults_to_the_instance_state_directory(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            with patch.dict(os.environ, {"XDG_STATE_HOME": temp_dir}, clear=True):
                self.assertEqual(
                    default_session_database(7683),
                    Path(temp_dir)
                    / "my-opiniated-editor"
                    / "instances"
                    / "port-7683"
                    / "browser-sessions.sqlite3",
                )

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

    def test_login_page_has_an_unchecked_stay_connected_control(self):
        proxy = self.proxy_server(1)
        self.start(proxy)
        try:
            status, headers, body = self.request(proxy.server_port, "GET", "/login")
            decoded = body.decode()

            self.assertEqual(status, 200)
            self.assertEqual(headers["Cache-Control"], "no-store")
            self.assertIn('action="/auth/login"', decoded)
            self.assertIn('name="stay_connected" type="checkbox"', decoded)
            self.assertIn("Stay connected", decoded)
            self.assertIn("30 days when selected; otherwise 3 hours.", decoded)
            self.assertNotIn('type="checkbox" checked', decoded)
        finally:
            proxy.shutdown()
            proxy.server_close()

    def test_unchecked_login_expires_after_three_hours(self):
        current_time = [1_000.0]
        upstream = ThreadingHTTPServer(("127.0.0.1", 0), UpstreamHandler)
        proxy = self.proxy_server(
            upstream.server_port, wall_time=lambda: current_time[0]
        )
        self.start(upstream, proxy)
        try:
            status, headers, _body = self.request(proxy.server_port, "GET", "/health")
            self.assertEqual(status, 303)
            self.assertEqual(headers["Location"], "/login")
            self.assertIsNone(headers["WWW-Authenticate"])

            status, headers, _body = self.login(proxy.server_port)
            self.assertEqual(status, 303)
            self.assertEqual(headers["Location"], "/")
            set_cookie = headers["Set-Cookie"]
            self.assertIn(f"{SESSION_COOKIE_NAME}=", set_cookie)
            self.assertIn("Secure", set_cookie)
            self.assertIn("HttpOnly", set_cookie)
            self.assertIn("SameSite=Strict", set_cookie)
            self.assertNotIn("Max-Age", set_cookie)
            self.assertNotIn("Expires", set_cookie)
            cookie = self.cookie_header(headers)

            status, _headers, body = self.request(
                proxy.server_port,
                "GET",
                "/health",
                headers={"Cookie": cookie},
            )
            self.assertEqual(status, 200)
            self.assertEqual(body, b"upstream path=/health\n")
            self.assertEqual(UpstreamHandler.received_cookies, [None])

            current_time[0] += SESSION_DURATION_SECONDS - 1
            status, _headers, _body = self.request(
                proxy.server_port,
                "GET",
                "/auth/session",
                headers={"Cookie": cookie},
            )
            self.assertEqual(status, 204)

            current_time[0] += 1
            status, headers, _body = self.request(
                proxy.server_port,
                "GET",
                "/auth/session",
                headers={"Cookie": cookie},
            )
            self.assertEqual(status, 401)
            self.assertIn("Max-Age=0", headers["Set-Cookie"])
        finally:
            proxy.shutdown()
            upstream.shutdown()
            proxy.server_close()
            upstream.server_close()

    def test_new_session_shows_security_summary_once_then_again_after_one_day(self):
        current_time = [1_000_000.0]
        upstream = ThreadingHTTPServer(("127.0.0.1", 0), UpstreamHandler)
        proxy = self.proxy_server(
            upstream.server_port, wall_time=lambda: current_time[0]
        )
        self.start(upstream, proxy)
        try:
            status, headers, _body = self.request(
                proxy.server_port, "GET", "/auth/security"
            )
            self.assertEqual(status, 303)
            self.assertEqual(headers["Location"], "/login")

            status, headers, _body = self.login(proxy.server_port, stay_connected=True)
            self.assertEqual(status, 303)
            cookie = self.cookie_header(headers)

            status, headers, body = self.request(
                proxy.server_port, "GET", "/", headers={"Cookie": cookie}
            )
            decoded = body.decode()
            self.assertEqual(status, 200)
            self.assertEqual(headers["Cache-Control"], "no-store")
            self.assertIn("Connection security summary", decoded)
            self.assertIn("Last 24 hours", decoded)
            self.assertIn("Successful", decoded)
            self.assertIn("Failed", decoded)
            self.assertIn("Observed source IPs", decoded)
            self.assertIn("Active sessions", decoded)
            self.assertIn("End-user IP visibility is unavailable", decoded)
            self.assertIn('href="/">Continue to editor', decoded)
            self.assertEqual(UpstreamHandler.received_cookies, [])

            status, _headers, body = self.request(
                proxy.server_port, "GET", "/", headers={"Cookie": cookie}
            )
            self.assertEqual(status, 200)
            self.assertEqual(body, b"upstream path=/\n")

            current_time[0] += SECURITY_SUMMARY_INTERVAL_SECONDS - 1
            status, _headers, body = self.request(
                proxy.server_port, "GET", "/", headers={"Cookie": cookie}
            )
            self.assertEqual(body, b"upstream path=/\n")

            current_time[0] += 1
            status, _headers, body = self.request(
                proxy.server_port, "GET", "/", headers={"Cookie": cookie}
            )
            self.assertEqual(status, 200)
            self.assertIn(b"Connection security summary", body)
        finally:
            proxy.shutdown()
            upstream.shutdown()
            proxy.server_close()
            upstream.server_close()

    def test_security_summary_records_failed_and_rate_limited_logins(self):
        monotonic_time = [100.0]
        wall_time = [2_000_000.0]
        proxy = self.proxy_server(
            1,
            monotonic_time=lambda: monotonic_time[0],
            wall_time=lambda: wall_time[0],
        )
        self.start(proxy)
        try:
            status, _headers, _body = self.login(proxy.server_port, password="wrong")
            self.assertEqual(status, 401)
            status, _headers, _body = self.login(proxy.server_port)
            self.assertEqual(status, 429)
            monotonic_time[0] += 3
            wall_time[0] += 3
            status, headers, _body = self.login(proxy.server_port)
            self.assertEqual(status, 303)
            cookie = self.cookie_header(headers)

            status, _headers, body = self.request(
                proxy.server_port,
                "GET",
                "/auth/security",
                headers={"Cookie": cookie},
            )
            decoded = body.decode()
            self.assertEqual(status, 200)
            self.assertIn(
                "Failures: 1 incorrect credentials,\n        1 rate limited", decoded
            )
            self.assertIn("127.0.0.1", decoded)
            summary = proxy.session_store.security_summary()
            self.assertEqual(summary.retained.total, 3)
            self.assertEqual(summary.retained.successful, 1)
            self.assertEqual(summary.retained.failed, 2)
        finally:
            proxy.shutdown()
            proxy.server_close()

    def test_checked_login_persists_for_thirty_days(self):
        current_time = [1_000.0]
        proxy = self.proxy_server(1, wall_time=lambda: current_time[0])
        self.start(proxy)
        try:
            status, headers, _body = self.login(proxy.server_port, stay_connected=True)
            self.assertEqual(status, 303)
            self.assertIn(
                f"Max-Age={PERSISTENT_SESSION_DURATION_SECONDS}",
                headers["Set-Cookie"],
            )
            self.assertIn("Expires=", headers["Set-Cookie"])
            cookie = self.cookie_header(headers)

            current_time[0] += PERSISTENT_SESSION_DURATION_SECONDS - 1
            status, _headers, _body = self.request(
                proxy.server_port,
                "GET",
                "/auth/session",
                headers={"Cookie": cookie},
            )
            self.assertEqual(status, 204)

            current_time[0] += 1
            status, _headers, _body = self.request(
                proxy.server_port,
                "GET",
                "/auth/session",
                headers={"Cookie": cookie},
            )
            self.assertEqual(status, 401)
        finally:
            proxy.shutdown()
            proxy.server_close()

    def test_failed_login_globally_delays_the_next_attempt(self):
        current_time = [100.0]
        proxy = self.proxy_server(1, monotonic_time=lambda: current_time[0])
        self.start(proxy)
        try:
            status, _headers, body = self.login(proxy.server_port, password="wrong")
            self.assertEqual(status, 401)
            self.assertIn(b"Incorrect username or password", body)

            status, headers, _body = self.login(proxy.server_port)
            self.assertEqual(status, 429)
            self.assertEqual(headers["Retry-After"], "3")

            current_time[0] += 2
            status, headers, _body = self.login(proxy.server_port)
            self.assertEqual(status, 429)
            self.assertEqual(headers["Retry-After"], "1")

            current_time[0] += 1
            status, _headers, _body = self.login(proxy.server_port)
            self.assertEqual(status, 303)
        finally:
            proxy.shutdown()
            proxy.server_close()

    def test_login_is_independent_of_funnel_host_and_origin_rewrites(self):
        proxy = self.proxy_server(1, allowed_origin=ALLOWED_ORIGIN)
        self.start(proxy)
        try:
            body = urlencode({"username": USERNAME, "password": PASSWORD}).encode()
            status, _headers, _body = self.request(
                proxy.server_port,
                "POST",
                "/auth/login",
                body=body,
                headers={
                    "Content-Type": "application/x-www-form-urlencoded",
                    "Content-Length": str(len(body)),
                    "Host": "127.0.0.1:7683",
                    "Origin": "https://funnel-browser-origin.example",
                    "X-Forwarded-For": "203.0.113.99",
                },
            )
            self.assertEqual(status, 303)
            self.assertEqual(
                proxy.session_store.security_summary().sources[0].source_ip,
                "127.0.0.1",
            )
        finally:
            proxy.shutdown()
            proxy.server_close()

    def test_websocket_upgrade_is_authenticated_and_tunneled(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        upstream_requests = []

        def fake_websocket_bridge():
            connection, _address = listener.accept()
            with connection:
                request = bytearray()
                while b"\r\n\r\n" not in request:
                    request.extend(connection.recv(4096))
                upstream_requests.append(bytes(request))
                connection.sendall(
                    b"HTTP/1.1 101 Switching Protocols\r\n"
                    b"Upgrade: websocket\r\n"
                    b"Connection: Upgrade\r\n\r\n"
                )
                connection.sendall(b"bridge:" + connection.recv(4096))

        upstream_thread = threading.Thread(target=fake_websocket_bridge, daemon=True)
        upstream_thread.start()
        proxy = self.proxy_server(
            listener.getsockname()[1], allowed_origin=ALLOWED_ORIGIN
        )
        proxy_thread = self.start(proxy)[0]
        token, _expires_at = proxy.session_store.create(
            USERNAME, SESSION_DURATION_SECONDS
        )
        cookie = f"{SESSION_COOKIE_NAME}={token}"
        try:
            with socket.create_connection(("127.0.0.1", proxy.server_port)) as client:
                client.sendall(
                    (
                        "GET /ws HTTP/1.1\r\n"
                        "Host: example\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        f"Origin: {ALLOWED_ORIGIN}\r\n"
                        f"Cookie: {cookie}\r\n\r\n"
                    ).encode()
                )
                response = bytearray()
                while b"\r\n\r\n" not in response:
                    response.extend(client.recv(4096))
                self.assertTrue(response.startswith(b"HTTP/1.1 101 "))
                client.sendall(b"payload")
                self.assertEqual(client.recv(4096), b"bridge:payload")
            self.assertNotIn(b"Cookie:", upstream_requests[0])
        finally:
            proxy.shutdown()
            proxy.server_close()
            listener.close()
            upstream_thread.join(timeout=1)
            proxy_thread.join(timeout=1)

    def test_websocket_upgrade_rejects_missing_or_mismatched_origin(self):
        unused_upstream = socket.socket()
        unused_upstream.bind(("127.0.0.1", 0))
        unused_upstream_port = unused_upstream.getsockname()[1]
        unused_upstream.close()

        proxy = self.proxy_server(unused_upstream_port, allowed_origin=ALLOWED_ORIGIN)
        self.start(proxy)
        token, _expires_at = proxy.session_store.create(
            USERNAME, SESSION_DURATION_SECONDS
        )
        cookie = f"{SESSION_COOKIE_NAME}={token}"
        try:
            for origin in [None, "https://evil.example"]:
                with self.subTest(origin=origin):
                    with socket.create_connection(
                        ("127.0.0.1", proxy.server_port)
                    ) as client:
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
                                f"Cookie: {cookie}\r\n\r\n"
                            ).encode()
                        )
                        response = bytearray()
                        while b"\r\n\r\n" not in response:
                            response.extend(client.recv(4096))
                        self.assertTrue(response.startswith(b"HTTP/1.1 403 "))
        finally:
            proxy.shutdown()
            proxy.server_close()

    def test_active_websocket_closes_when_its_session_expires(self):
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
                while connection.recv(4096):
                    pass

        upstream_thread = threading.Thread(target=fake_websocket_bridge, daemon=True)
        upstream_thread.start()
        proxy = self.proxy_server(listener.getsockname()[1])
        self.start(proxy)
        token, _expires_at = proxy.session_store.create(USERNAME, 1)
        try:
            with socket.create_connection(("127.0.0.1", proxy.server_port)) as client:
                client.sendall(
                    (
                        "GET /ws HTTP/1.1\r\n"
                        "Host: example\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        f"Cookie: {SESSION_COOKIE_NAME}={token}\r\n\r\n"
                    ).encode()
                )
                response = bytearray()
                while b"\r\n\r\n" not in response:
                    response.extend(client.recv(4096))
                self.assertTrue(response.startswith(b"HTTP/1.1 101 "))
                client.settimeout(2)
                self.assertEqual(client.recv(1), b"")
        finally:
            proxy.shutdown()
            proxy.server_close()
            listener.close()
            upstream_thread.join(timeout=1)


if __name__ == "__main__":
    unittest.main()
