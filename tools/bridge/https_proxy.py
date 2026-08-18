#!/usr/bin/env python3
"""Small HTTPS/authentication reverse proxy for the loopback C++ bridge."""

from __future__ import annotations

import argparse
import base64
import binascii
from http.cookies import CookieError, SimpleCookie
from dataclasses import dataclass
from datetime import datetime, timezone
from email.utils import formatdate
import getpass
import hashlib
import hmac
import html
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import math
import os
from pathlib import Path
import secrets
import shutil
import socket
import sqlite3
import ssl
import subprocess
import tempfile
import threading
import time
from typing import Callable
from urllib.parse import parse_qs, urlsplit


SCRYPT_N = 16_384
SCRYPT_R = 8
SCRYPT_P = 1
PASSWORD_FORMAT = "moe-scrypt-v1"
AUTHENTICATION_RETRY_DELAY_SECONDS = 3
SESSION_COOKIE_NAME = "__Host-moe-session"
SESSION_DURATION_SECONDS = 3 * 60 * 60
PERSISTENT_SESSION_DURATION_SECONDS = 30 * 24 * 60 * 60
SECURITY_SUMMARY_INTERVAL_SECONDS = 24 * 60 * 60
AUTHENTICATION_ATTEMPT_RETENTION_SECONDS = 90 * 24 * 60 * 60
MAX_RETAINED_AUTHENTICATION_ATTEMPTS = 10_000
MAX_LOGIN_BODY_BYTES = 8 * 1024
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
class AuthenticatedSession:
    username: str
    expires_at: int


@dataclass(frozen=True)
class AttemptCounts:
    total: int
    successful: int
    failed: int
    invalid_credentials: int
    rate_limited: int
    malformed: int
    distinct_source_ips: int


@dataclass(frozen=True)
class SourceAttemptCounts:
    source_ip: str
    successful: int
    failed: int
    last_attempt_at: int


@dataclass(frozen=True)
class SecuritySummary:
    retained: AttemptCounts
    last_24_hours: AttemptCounts
    retained_since: int | None
    last_success_at: int | None
    last_failure_at: int | None
    active_sessions: int
    sources: tuple[SourceAttemptCounts, ...]
    source_ip_visibility_limited: bool


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


def verify_credentials(username: str, password: str, record: PasswordRecord) -> bool:
    candidate = _password_digest(password, record.salt)
    return hmac.compare_digest(username, record.username) and hmac.compare_digest(
        candidate, record.digest
    )


def _password_record_fingerprint(record: PasswordRecord) -> bytes:
    digest = hashlib.sha256()
    digest.update(record.username.encode("utf-8"))
    digest.update(b"\0")
    digest.update(record.salt)
    digest.update(record.digest)
    return digest.digest()


def _is_loopback_address(address: str) -> bool:
    try:
        return ipaddress.ip_address(address).is_loopback
    except ValueError:
        return False


def default_session_database(public_port: int) -> Path:
    configured = os.environ.get("MOE_BRIDGE_SESSION_DATABASE")
    if configured:
        return Path(configured)
    state_root = Path(
        os.environ.get("XDG_STATE_HOME", Path.home() / ".local" / "state")
    )
    return (
        state_root
        / "my-opiniated-editor"
        / "instances"
        / f"port-{public_port}"
        / "browser-sessions.sqlite3"
    )


class SessionStore:
    def __init__(
        self,
        database: Path | str,
        password_record: PasswordRecord,
        current_time: Callable[[], float] = time.time,
    ) -> None:
        self._current_time = current_time
        self._password_fingerprint = _password_record_fingerprint(password_record)
        self._lock = threading.Lock()
        if database != ":memory:":
            database_path = Path(database)
            database_path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
            database_path.parent.chmod(0o700)
            try:
                descriptor = os.open(
                    database_path,
                    os.O_CREAT | os.O_EXCL | os.O_WRONLY,
                    0o600,
                )
            except FileExistsError:
                pass
            else:
                os.close(descriptor)
        self._connection = sqlite3.connect(database, check_same_thread=False)
        if database != ":memory:":
            Path(database).chmod(0o600)
        with self._connection:
            self._connection.execute(
                """
                CREATE TABLE IF NOT EXISTS sessions (
                    token_digest BLOB PRIMARY KEY,
                    username TEXT NOT NULL,
                    password_fingerprint BLOB NOT NULL,
                    expires_at INTEGER NOT NULL,
                    security_summary_shown_at INTEGER
                )
                """
            )
            session_columns = {
                row[1]
                for row in self._connection.execute("PRAGMA table_info(sessions)")
            }
            if "security_summary_shown_at" not in session_columns:
                self._connection.execute(
                    "ALTER TABLE sessions ADD COLUMN security_summary_shown_at INTEGER"
                )
            self._connection.execute(
                """
                CREATE TABLE IF NOT EXISTS authentication_attempts (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    occurred_at INTEGER NOT NULL,
                    source_ip TEXT NOT NULL,
                    outcome TEXT NOT NULL
                )
                """
            )
            self._connection.execute(
                "DELETE FROM sessions WHERE password_fingerprint != ? OR expires_at <= ?",
                (self._password_fingerprint, self._now()),
            )
            self._connection.execute(
                "DELETE FROM authentication_attempts WHERE occurred_at < ?",
                (self._now() - AUTHENTICATION_ATTEMPT_RETENTION_SECONDS,),
            )

    def _now(self) -> int:
        return int(self._current_time())

    @staticmethod
    def _token_digest(token: str) -> bytes:
        return hashlib.sha256(token.encode("ascii")).digest()

    def create(self, username: str, duration_seconds: int) -> tuple[str, int]:
        token = secrets.token_urlsafe(32)
        expires_at = self._now() + duration_seconds
        with self._lock, self._connection:
            self._connection.execute(
                "DELETE FROM sessions WHERE expires_at <= ?",
                (self._now(),),
            )
            self._connection.execute(
                """
                INSERT INTO sessions (
                    token_digest, username, password_fingerprint, expires_at
                ) VALUES (?, ?, ?, ?)
                """,
                (
                    self._token_digest(token),
                    username,
                    self._password_fingerprint,
                    expires_at,
                ),
            )
        return token, expires_at

    def authenticate(self, token: str | None) -> AuthenticatedSession | None:
        if token is None or not token.isascii() or len(token) > 512:
            return None
        token_digest = self._token_digest(token)
        with self._lock:
            row = self._connection.execute(
                """
                SELECT username, expires_at
                FROM sessions
                WHERE token_digest = ? AND password_fingerprint = ?
                """,
                (token_digest, self._password_fingerprint),
            ).fetchone()
            if row is None:
                return None
            username, expires_at = row
            if expires_at <= self._now():
                with self._connection:
                    self._connection.execute(
                        "DELETE FROM sessions WHERE token_digest = ?",
                        (token_digest,),
                    )
                return None
        return AuthenticatedSession(username=username, expires_at=expires_at)

    def claim_security_summary(self, token: str | None, *, force: bool = False) -> bool:
        if token is None or not token.isascii() or len(token) > 512:
            return False
        now = self._now()
        due_before = now - SECURITY_SUMMARY_INTERVAL_SECONDS
        conditions = (
            ""
            if force
            else (
                "AND (security_summary_shown_at IS NULL "
                "OR security_summary_shown_at <= ?)"
            )
        )
        parameters: tuple[object, ...] = (
            now,
            self._token_digest(token),
            self._password_fingerprint,
            now,
        )
        if not force:
            parameters += (due_before,)
        with self._lock, self._connection:
            cursor = self._connection.execute(
                f"""
                UPDATE sessions
                SET security_summary_shown_at = ?
                WHERE token_digest = ?
                  AND password_fingerprint = ?
                  AND expires_at > ?
                  {conditions}
                """,
                parameters,
            )
        return cursor.rowcount == 1

    def record_authentication_attempt(self, source_ip: str, outcome: str) -> None:
        if outcome not in {
            "success",
            "invalid_credentials",
            "rate_limited",
            "malformed",
        }:
            raise ValueError("unsupported authentication outcome")
        now = self._now()
        with self._lock, self._connection:
            self._connection.execute(
                """
                INSERT INTO authentication_attempts (occurred_at, source_ip, outcome)
                VALUES (?, ?, ?)
                """,
                (now, source_ip[:255], outcome),
            )
            self._connection.execute(
                "DELETE FROM authentication_attempts WHERE occurred_at < ?",
                (now - AUTHENTICATION_ATTEMPT_RETENTION_SECONDS,),
            )
            self._connection.execute(
                """
                DELETE FROM authentication_attempts
                WHERE id NOT IN (
                    SELECT id FROM authentication_attempts
                    ORDER BY id DESC
                    LIMIT ?
                )
                """,
                (MAX_RETAINED_AUTHENTICATION_ATTEMPTS,),
            )

    @staticmethod
    def _attempt_counts(row: tuple[object, ...]) -> AttemptCounts:
        return AttemptCounts(*(int(value) for value in row))

    def security_summary(self) -> SecuritySummary:
        now = self._now()

        def counts(since: int) -> AttemptCounts:
            row = self._connection.execute(
                """
                SELECT
                    COUNT(*),
                    COALESCE(SUM(outcome = 'success'), 0),
                    COALESCE(SUM(outcome != 'success'), 0),
                    COALESCE(SUM(outcome = 'invalid_credentials'), 0),
                    COALESCE(SUM(outcome = 'rate_limited'), 0),
                    COALESCE(SUM(outcome = 'malformed'), 0),
                    COUNT(DISTINCT source_ip)
                FROM authentication_attempts
                WHERE occurred_at >= ?
                """,
                (since,),
            ).fetchone()
            assert row is not None
            return self._attempt_counts(row)

        retention_start = now - AUTHENTICATION_ATTEMPT_RETENTION_SECONDS
        with self._lock:
            retained = counts(retention_start)
            last_24_hours = counts(now - SECURITY_SUMMARY_INTERVAL_SECONDS)
            retained_since, last_success_at, last_failure_at = self._connection.execute(
                """
                SELECT
                    MIN(occurred_at),
                    MAX(CASE WHEN outcome = 'success' THEN occurred_at END),
                    MAX(CASE WHEN outcome != 'success' THEN occurred_at END)
                FROM authentication_attempts
                WHERE occurred_at >= ?
                """,
                (retention_start,),
            ).fetchone()
            active_sessions = self._connection.execute(
                """
                SELECT COUNT(*) FROM sessions
                WHERE password_fingerprint = ? AND expires_at > ?
                """,
                (self._password_fingerprint, now),
            ).fetchone()[0]
            source_rows = self._connection.execute(
                """
                SELECT
                    source_ip,
                    COALESCE(SUM(outcome = 'success'), 0),
                    COALESCE(SUM(outcome != 'success'), 0),
                    MAX(occurred_at)
                FROM authentication_attempts
                WHERE occurred_at >= ?
                GROUP BY source_ip
                ORDER BY MAX(occurred_at) DESC
                LIMIT 10
                """,
                (retention_start,),
            ).fetchall()
        sources = tuple(
            SourceAttemptCounts(
                source_ip=str(row[0]),
                successful=int(row[1]),
                failed=int(row[2]),
                last_attempt_at=int(row[3]),
            )
            for row in source_rows
        )
        visibility_limited = bool(sources) and all(
            _is_loopback_address(source.source_ip) for source in sources
        )
        return SecuritySummary(
            retained=retained,
            last_24_hours=last_24_hours,
            retained_since=None if retained_since is None else int(retained_since),
            last_success_at=None if last_success_at is None else int(last_success_at),
            last_failure_at=None if last_failure_at is None else int(last_failure_at),
            active_sessions=int(active_sessions),
            sources=sources,
            source_ip_visibility_limited=visibility_limited,
        )

    def close(self) -> None:
        with self._lock:
            self._connection.close()


def validate_allowed_origin(origin: str) -> str:
    parsed = urlsplit(origin)
    if (
        parsed.scheme != "https"
        or not parsed.netloc
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError(
            "allowed origin must be an HTTPS origin without a path, query, or fragment"
        )
    return origin


def validate_allowed_origins(origins: str | None) -> frozenset[str]:
    if origins is None:
        return frozenset()
    values = [origin.strip() for origin in origins.split(",")]
    if not values or any(not origin for origin in values):
        raise ValueError("allowed origins must be a comma-separated list")
    return frozenset(validate_allowed_origin(origin) for origin in values)


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


def login_page(error: str | None = None) -> bytes:
    error_markup = ""
    if error is not None:
        error_markup = f'<p class="error" role="alert">{html.escape(error)}</p>'
    return f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Sign in | my-opiniated-editor</title>
    <style>
      :root {{ color-scheme: dark; font-family: system-ui, sans-serif; }}
      * {{ box-sizing: border-box; }}
      body {{
        align-items: center;
        background: #0b0d0e;
        color: #d9e2df;
        display: flex;
        justify-content: center;
        margin: 0;
        min-height: 100vh;
        padding: 24px;
      }}
      main {{
        background: #121715;
        border: 1px solid #27302d;
        border-radius: 10px;
        max-width: 380px;
        padding: 28px;
        width: 100%;
      }}
      h1 {{ font-size: 1.35rem; margin: 0 0 24px; }}
      label {{ display: block; font-size: 0.9rem; margin: 16px 0 6px; }}
      input[type="text"], input[type="password"] {{
        background: #0b0d0e;
        border: 1px solid #3b4743;
        border-radius: 5px;
        color: inherit;
        font: inherit;
        padding: 10px;
        width: 100%;
      }}
      .stay-connected {{ align-items: center; display: flex; gap: 8px; }}
      .stay-connected input {{ margin: 0; }}
      .hint {{ color: #9aa7a2; font-size: 0.8rem; margin: -2px 0 0 22px; }}
      button {{
        background: #d9e2df;
        border: 0;
        border-radius: 5px;
        color: #0b0d0e;
        cursor: pointer;
        font: inherit;
        font-weight: 600;
        margin-top: 20px;
        padding: 10px 14px;
        width: 100%;
      }}
      .error {{ color: #ff9b91; margin: 0 0 16px; }}
    </style>
  </head>
  <body>
    <main>
      <h1>Connect to the editor</h1>
      {error_markup}
      <form method="post" action="/auth/login">
        <label for="username">Username</label>
        <input id="username" name="username" type="text" autocomplete="username" required autofocus>
        <label for="password">Password</label>
        <input id="password" name="password" type="password" autocomplete="current-password" required>
        <label class="stay-connected">
          <input name="stay_connected" type="checkbox">
          Stay connected
        </label>
        <p class="hint">30 days when selected; otherwise 3 hours.</p>
        <button type="submit">Connect</button>
      </form>
    </main>
  </body>
</html>
""".encode("utf-8")


def _format_utc(timestamp: int | None) -> str:
    if timestamp is None:
        return "None recorded"
    return datetime.fromtimestamp(timestamp, timezone.utc).strftime(
        "%Y-%m-%d %H:%M:%S UTC"
    )


def _attempt_window_markup(title: str, counts: AttemptCounts) -> str:
    return f"""
      <section class="window">
        <h2>{html.escape(title)}</h2>
        <div class="metrics">
          <div><strong>{counts.total}</strong><span>Total attempts</span></div>
          <div><strong>{counts.successful}</strong><span>Successful</span></div>
          <div><strong>{counts.failed}</strong><span>Failed</span></div>
          <div><strong>{counts.distinct_source_ips}</strong><span>Observed source IPs</span></div>
        </div>
        <p class="detail">Failures: {counts.invalid_credentials} incorrect credentials,
        {counts.rate_limited} rate limited, {counts.malformed} malformed.</p>
      </section>
    """


def security_summary_page(
    summary: SecuritySummary, session: AuthenticatedSession
) -> bytes:
    source_rows = "".join(
        f"""
          <tr>
            <td><code>{html.escape(source.source_ip)}</code></td>
            <td>{source.successful}</td>
            <td>{source.failed}</td>
            <td>{_format_utc(source.last_attempt_at)}</td>
          </tr>
        """
        for source in summary.sources
    )
    if not source_rows:
        source_rows = '<tr><td colspan="4">No attempts recorded.</td></tr>'
    visibility_note = ""
    if summary.source_ip_visibility_limited:
        visibility_note = """
          <p class="notice" role="note"><strong>End-user IP visibility is unavailable.</strong>
          The current reverse proxy is presenting only loopback source addresses. The
          distinct-IP figures therefore describe proxy peers, not necessarily distinct
          people or devices.</p>
        """
    retained_title = "Recorded history"
    if summary.retained_since is not None:
        retained_title += f" since {_format_utc(summary.retained_since)}"
    return f"""<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Connection security | my-opiniated-editor</title>
    <style>
      :root {{ color-scheme: dark; font-family: system-ui, sans-serif; }}
      * {{ box-sizing: border-box; }}
      body {{ background: #0b0d0e; color: #d9e2df; margin: 0; padding: 24px; }}
      main {{ margin: 0 auto; max-width: 920px; }}
      h1 {{ font-size: 1.6rem; margin: 0 0 6px; }}
      h2 {{ font-size: 1rem; margin: 0 0 18px; }}
      .lede, .detail, .timestamps {{ color: #9aa7a2; }}
      .lede {{ margin: 0 0 24px; }}
      .window, .session, .sources {{
        background: #121715; border: 1px solid #27302d; border-radius: 10px;
        margin: 16px 0; padding: 20px;
      }}
      .metrics {{ display: grid; gap: 12px; grid-template-columns: repeat(4, 1fr); }}
      .metrics div {{ background: #0b0d0e; border-radius: 6px; padding: 14px; }}
      .metrics strong {{ display: block; font-size: 1.5rem; }}
      .metrics span {{ color: #9aa7a2; font-size: 0.82rem; }}
      .detail, .timestamps {{ font-size: 0.86rem; margin: 16px 0 0; }}
      .notice {{
        background: #282311; border: 1px solid #594d20; border-radius: 6px;
        color: #eadb9b; padding: 12px;
      }}
      .table-wrap {{ overflow-x: auto; }}
      table {{ border-collapse: collapse; width: 100%; }}
      th, td {{ border-bottom: 1px solid #27302d; padding: 10px; text-align: left; }}
      th {{ color: #9aa7a2; font-size: 0.8rem; }}
      code {{ color: #c8d5d1; }}
      .continue {{
        background: #d9e2df; border-radius: 5px; color: #0b0d0e;
        display: inline-block; font-weight: 600; margin-top: 8px;
        padding: 10px 14px; text-decoration: none;
      }}
      @media (max-width: 680px) {{
        .metrics {{ grid-template-columns: repeat(2, 1fr); }}
      }}
    </style>
  </head>
  <body>
    <main>
      <h1>Connection security summary</h1>
      <p class="lede">Shown after sign-in and at most once every 24 hours for this session.
      History retains up to 90 days or the 10,000 most recent attempts.</p>
      {_attempt_window_markup("Last 24 hours", summary.last_24_hours)}
      {_attempt_window_markup(retained_title, summary.retained)}
      <section class="session">
        <h2>Session status</h2>
        <div class="metrics">
          <div><strong>{summary.active_sessions}</strong><span>Active sessions</span></div>
          <div><strong>{summary.retained.distinct_source_ips}</strong><span>Observed source IPs</span></div>
        </div>
        <p class="timestamps">Current session expires: {_format_utc(session.expires_at)}<br>
        Last successful sign-in: {_format_utc(summary.last_success_at)}<br>
        Last failed attempt: {_format_utc(summary.last_failure_at)}</p>
      </section>
      <section class="sources">
        <h2>Most recently active source addresses</h2>
        {visibility_note}
        <div class="table-wrap">
          <table>
            <thead><tr><th>Observed source</th><th>Successful</th><th>Failed</th><th>Last attempt</th></tr></thead>
            <tbody>{source_rows}</tbody>
          </table>
        </div>
      </section>
      <a class="continue" href="/">Continue to editor</a>
    </main>
  </body>
</html>
""".encode("utf-8")


def session_cookie(token: str, expires_at: int | None = None) -> str:
    cookie = f"{SESSION_COOKIE_NAME}={token}; Path=/; Secure; HttpOnly; SameSite=Strict"
    if expires_at is not None:
        cookie += (
            f"; Max-Age={PERSISTENT_SESSION_DURATION_SECONDS}"
            f"; Expires={formatdate(expires_at, usegmt=True)}"
        )
    return cookie


def expired_session_cookie() -> str:
    return (
        f"{SESSION_COOKIE_NAME}=; Path=/; Secure; HttpOnly; SameSite=Strict"
        "; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT"
    )


class ProxyServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self,
        address: tuple[str, int],
        upstream_port: int,
        password_record: PasswordRecord,
        session_store: SessionStore,
        allowed_origin: str | None = None,
        monotonic_time: Callable[[], float] = time.monotonic,
        wall_time: Callable[[], float] = time.time,
    ) -> None:
        allowed_origins = validate_allowed_origins(allowed_origin)
        super().__init__(address, ProxyHandler)
        self.upstream_port = upstream_port
        self.password_record = password_record
        self.session_store = session_store
        self.allowed_origins = allowed_origins
        self._monotonic_time = monotonic_time
        self.wall_time = wall_time
        self._authentication_lock = threading.Lock()
        self._authentication_retry_deadline: float | None = None

    def authenticate_credentials(
        self, username: str, password: str
    ) -> tuple[bool, int | None]:
        with self._authentication_lock:
            now = self._monotonic_time()
            retry_deadline = self._authentication_retry_deadline
            if retry_deadline is not None and now < retry_deadline:
                return False, max(1, math.ceil(retry_deadline - now))
            if verify_credentials(username, password, self.password_record):
                self._authentication_retry_deadline = None
                return True, None
            self._authentication_retry_deadline = (
                self._monotonic_time() + AUTHENTICATION_RETRY_DELAY_SECONDS
            )
            return False, None

    def server_close(self) -> None:
        super().server_close()
        self.session_store.close()


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

    def do_POST(self) -> None:
        self.close_connection = True
        request_path = urlsplit(self.path).path
        if request_path == "/auth/login":
            self._login()
            return
        self._send_body(405, b"method not allowed\n", "text/plain; charset=utf-8")

    def _session_token(self) -> str | None:
        encoded_cookie = self.headers.get("Cookie")
        if encoded_cookie is None:
            return None
        cookies = SimpleCookie()
        try:
            cookies.load(encoded_cookie)
        except CookieError:
            return None
        session = cookies.get(SESSION_COOKIE_NAME)
        return None if session is None else session.value

    def _authenticated_session(self) -> AuthenticatedSession | None:
        return self.proxy_server.session_store.authenticate(self._session_token())

    def _record_authentication_attempt(self, outcome: str) -> None:
        self.proxy_server.session_store.record_authentication_attempt(
            str(self.client_address[0]), outcome
        )

    def _send_body(
        self,
        status: int,
        body: bytes,
        content_type: str,
        headers: tuple[tuple[str, str], ...] = (),
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        if self.close_connection:
            self.send_header("Connection", "close")
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _redirect(self, location: str, cookie: str | None = None) -> None:
        self.send_response(303)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.send_header("Cache-Control", "no-store")
        if self.close_connection:
            self.send_header("Connection", "close")
        if cookie is not None:
            self.send_header("Set-Cookie", cookie)
        self.end_headers()

    def _send_login_page(
        self,
        status: int = 200,
        error: str | None = None,
        retry_after: int | None = None,
        clear_cookie: bool = False,
    ) -> None:
        headers = [
            (
                "Content-Security-Policy",
                "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; "
                "base-uri 'none'; frame-ancestors 'none'",
            ),
            ("Referrer-Policy", "no-referrer"),
            ("X-Content-Type-Options", "nosniff"),
        ]
        if retry_after is not None:
            headers.append(("Retry-After", str(retry_after)))
        if clear_cookie:
            headers.append(("Set-Cookie", expired_session_cookie()))
        self._send_body(
            status,
            login_page(error),
            "text/html; charset=utf-8",
            tuple(headers),
        )

    def _send_security_summary(self, session: AuthenticatedSession) -> None:
        headers = (
            (
                "Content-Security-Policy",
                "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; "
                "frame-ancestors 'none'",
            ),
            ("Referrer-Policy", "no-referrer"),
            ("X-Content-Type-Options", "nosniff"),
        )
        self._send_body(
            200,
            security_summary_page(
                self.proxy_server.session_store.security_summary(), session
            ),
            "text/html; charset=utf-8",
            headers,
        )

    def _login(self) -> None:
        content_type = self.headers.get("Content-Type", "").partition(";")[0]
        try:
            content_length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            content_length = -1
        if (
            content_type != "application/x-www-form-urlencoded"
            or content_length < 0
            or content_length > MAX_LOGIN_BODY_BYTES
        ):
            self._record_authentication_attempt("malformed")
            self._send_login_page(400, "Invalid login request.")
            return
        try:
            fields = parse_qs(
                self.rfile.read(content_length).decode("utf-8"),
                keep_blank_values=True,
            )
        except UnicodeDecodeError:
            self._record_authentication_attempt("malformed")
            self._send_login_page(400, "Invalid login request.")
            return
        usernames = fields.get("username", [])
        passwords = fields.get("password", [])
        if len(usernames) != 1 or len(passwords) != 1:
            self._record_authentication_attempt("malformed")
            self._send_login_page(400, "Invalid login request.")
            return
        authenticated, retry_after = self.proxy_server.authenticate_credentials(
            usernames[0], passwords[0]
        )
        if retry_after is not None:
            self._record_authentication_attempt("rate_limited")
            self._send_login_page(
                429,
                f"Try again in {retry_after} seconds.",
                retry_after=retry_after,
            )
            return
        if not authenticated:
            self._record_authentication_attempt("invalid_credentials")
            self._send_login_page(401, "Incorrect username or password.")
            return
        stay_connected = fields.get("stay_connected") == ["on"]
        duration = (
            PERSISTENT_SESSION_DURATION_SECONDS
            if stay_connected
            else SESSION_DURATION_SECONDS
        )
        token, expires_at = self.proxy_server.session_store.create(
            self.proxy_server.password_record.username, duration
        )
        self._record_authentication_attempt("success")
        cookie_expiration = expires_at if stay_connected else None
        self._redirect("/", session_cookie(token, cookie_expiration))

    def _proxy_request(self) -> None:
        request_path = urlsplit(self.path).path
        if request_path == "/login":
            if self._authenticated_session() is not None:
                self._redirect("/")
                return
            self._send_login_page(clear_cookie=self._session_token() is not None)
            return
        if request_path == "/auth/session":
            if self._authenticated_session() is not None:
                self._send_body(204, b"", "text/plain; charset=utf-8")
                return
            headers = (("Set-Cookie", expired_session_cookie()),)
            self._send_body(
                401,
                b"authentication required\n",
                "text/plain; charset=utf-8",
                headers,
            )
            return

        websocket_upgrade = self.headers.get("Upgrade", "").lower() == "websocket"
        if (
            websocket_upgrade
            and self.proxy_server.allowed_origins
            and self.headers.get("Origin") not in self.proxy_server.allowed_origins
        ):
            body = b"websocket origin not allowed\n"
            self.send_response(403)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)
            self.close_connection = True
            return

        session = self._authenticated_session()
        if session is None:
            if websocket_upgrade:
                self._send_body(
                    401,
                    b"authentication required\n",
                    "text/plain; charset=utf-8",
                )
            else:
                cookie = (
                    expired_session_cookie()
                    if self._session_token() is not None
                    else None
                )
                self._redirect("/login", cookie)
            return
        if request_path == "/auth/security":
            if self.command == "GET":
                self.proxy_server.session_store.claim_security_summary(
                    self._session_token(), force=True
                )
            self._send_security_summary(session)
            return
        if (
            self.command == "GET"
            and request_path == "/"
            and self.proxy_server.session_store.claim_security_summary(
                self._session_token()
            )
        ):
            self._send_security_summary(session)
            return
        if websocket_upgrade:
            print(
                f"https-proxy websocket client={self.client_address[0]} authenticated",
                flush=True,
            )
            self._proxy_websocket(session)
            return
        self._proxy_http()

    def _proxy_http(self) -> None:
        headers = {
            name: value
            for name, value in self.headers.items()
            if name.lower() not in HOP_BY_HOP_HEADERS
            and name.lower() not in {"authorization", "cookie"}
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
            if name.lower() in {
                "authorization",
                "cookie",
                "proxy-authorization",
                "host",
            }:
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

    @staticmethod
    def _shutdown_sockets(*sockets: socket.socket) -> None:
        for current in sockets:
            try:
                current.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    def _proxy_websocket(self, session: AuthenticatedSession) -> None:
        self.close_connection = True
        try:
            upstream = socket.create_connection(
                ("127.0.0.1", self.proxy_server.upstream_port), timeout=10
            )
            upstream.sendall(self._upstream_websocket_request())
            response = self._read_response_headers(upstream)
            status_line = response.partition(b"\r\n")[0].decode(
                "latin-1", errors="replace"
            )
            print(
                f"https-proxy websocket client={self.client_address[0]} "
                f"upstream={status_line}",
                flush=True,
            )
            self.connection.sendall(response)
        except OSError as error:
            self.send_error(502, f"bridge unavailable: {error}")
            return
        if not response.startswith(b"HTTP/1.1 101 "):
            upstream.close()
            return
        upstream.settimeout(None)
        expiration_delay = max(0.0, session.expires_at - self.proxy_server.wall_time())
        expiration_timer = threading.Timer(
            expiration_delay,
            self._shutdown_sockets,
            args=(self.connection, upstream),
        )
        expiration_timer.daemon = True
        expiration_timer.start()
        browser_to_bridge = threading.Thread(
            target=self._pump, args=(self.connection, upstream), daemon=True
        )
        browser_to_bridge.start()
        self._pump(upstream, self.connection)
        expiration_timer.cancel()
        browser_to_bridge.join(timeout=1)
        upstream.close()
        print(
            f"https-proxy websocket client={self.client_address[0]} closed",
            flush=True,
        )

    def log_message(self, format_string: str, *args: object) -> None:
        print(f"https-proxy client={self.client_address[0]} {format_string % args}")


def serve_https(
    interface: str,
    public_port: int,
    upstream_port: int,
    paths: SecretPaths,
    session_database: Path,
    allowed_origin: str | None,
) -> None:
    password_record = read_password_record(paths.password_file)
    session_store = SessionStore(session_database, password_record)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(paths.certificate, paths.certificate_key)
    server = ProxyServer(
        (interface, public_port),
        upstream_port,
        password_record,
        session_store,
        allowed_origin=allowed_origin,
    )
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
        default=os.environ.get("MOE_BRIDGE_HTTPS_INTERFACE", "127.0.0.1"),
    )
    serve.add_argument(
        "--allowed-origin",
        default=os.environ.get("MOE_BRIDGE_ALLOWED_ORIGIN"),
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
        default_session_database(args.https_port),
        args.allowed_origin,
    )


if __name__ == "__main__":
    main()
