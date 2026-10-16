#!/usr/bin/env python3
"""Authenticated, bounded LAN companion for Claude Code PermissionRequest."""
from __future__ import annotations
import argparse
import hashlib
import hmac
import http.server
import ipaddress
import json
import secrets
import socket
import threading
import time
from dataclasses import dataclass
from typing import Any
from companion import Companion
from radio_relay import RadioRelay, RADIO_PATH

MAX_BODY = 4096
MAX_QUEUE = 8
DEFAULT_TIMEOUT = 120.0
LEASE_SECONDS = 5.0


def text(value: Any, limit: int) -> str:
    value = str(value or "")
    value = "".join(c if c >= " " and c != "\x7f" else " " for c in value)
    # Firmware buffers are byte bounded, always end at a UTF-8 boundary.
    return value.encode("utf-8")[:limit].decode("utf-8", errors="ignore")


def summarize_hook(payload: dict[str, Any]) -> dict[str, str]:
    tool_input = payload.get("tool_input") or {}
    if not isinstance(tool_input, dict):
        tool_input = {}
    # The executable command/path takes priority over a potentially misleading description.
    detail = next((tool_input[k] for k in ("command", "file_path", "path", "pattern", "url", "description") if tool_input.get(k)), "permission request")
    return {"tool": text(payload.get("tool_name") or "unknown", 32),
            "summary": text(detail, 256), "cwd": text(payload.get("cwd"), 160)}


def hook_output(decision: str) -> dict:
    value = {"behavior": decision}
    if decision == "deny":
        value["message"] = "Denied on Passport, disconnected, or bridge timeout"
    return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": value}}


@dataclass
class Pending:
    request_id: str
    tool: str
    summary: str
    cwd: str
    created: float
    deadline: float
    result: str | None = None
    delivered: bool = False
    challenge: str = ""


class Bridge:
    def __init__(self, token: bytes, timeout: float = DEFAULT_TIMEOUT, companion=None):
        self.token = token
        self.timeout = max(0.05, min(120.0, timeout))
        self.cv = threading.Condition()
        self.pending: list[Pending] = []
        self.by_id: dict[str, Pending] = {}
        self.nonces: dict[str, float] = {}
        self.last_seen = 0.0
        self.companion = companion or Companion({})

    def expire_locked(self):
        now = time.monotonic()
        for item in list(self.pending):
            if item.result is None and (item.deadline <= now or
                    (item.delivered and now - self.last_seen > LEASE_SECONDS)):
                item.result = "deny"
            if item.result is not None:
                self.remove_locked(item)
                self.cv.notify_all()

    def enqueue(self, payload):
        with self.cv:
            self.expire_locked()
            if len(self.pending) >= MAX_QUEUE:
                return None
            value = summarize_hook(payload)
            now = time.monotonic()
            item = Pending("r-" + secrets.token_hex(16), **value, created=now, deadline=now+self.timeout)
            self.pending.append(item)
            self.by_id[item.request_id] = item
            self.cv.notify_all()
            return item

    def wait_result(self, item):
        with self.cv:
            while item.result is None:
                self.expire_locked()
                if item.result is None:
                    self.cv.wait(timeout=0.25)
            self.remove_locked(item)
            return item.result

    def next_for_device(self):
        with self.cv:
            self.expire_locked()
            self.last_seen = time.monotonic()
            item = self.pending[0] if self.pending else None
            if item:
                item.delivered = True
                if not item.challenge:
                    item.challenge = secrets.token_hex(16)
            return item

    def decide(self, request_id, decision, challenge=None):
        if decision not in ("allow", "deny"):
            return False
        with self.cv:
            self.expire_locked()
            item = self.by_id.get(request_id)
            if not item or not item.delivered or item.result is not None or not challenge or not hmac.compare_digest(item.challenge, challenge):
                return False
            item.result = decision
            self.remove_locked(item)
            self.cv.notify_all()
            return True

    def remove_locked(self, item):
        self.by_id.pop(item.request_id, None)
        if item in self.pending:
            self.pending.remove(item)

    def signature(self, method, path, body, nonce=""):
        message = method.encode() + b"\n" + path.encode() + b"\n" + nonce.encode() + b"\n" + body
        return hmac.new(self.token, message, hashlib.sha256).hexdigest()

    def authorize(self, method, path, body, nonce, supplied):
        if len(nonce) != 32 or any(c not in "0123456789abcdef" for c in nonce):
            return False
        if not hmac.compare_digest(supplied, self.signature(method, path, body, nonce)):
            return False
        with self.cv:
            now = time.monotonic()
            self.nonces = {k: v for k, v in self.nonces.items() if now-v < 300}
            if nonce in self.nonces or len(self.nonces) >= 4096:
                return False
            self.nonces[nonce] = now
            return True

    def response_signature(self, nonce, status, body):
        message = nonce.encode()+b"\n"+str(status).encode()+b"\n"+body
        return hmac.new(self.token, message, hashlib.sha256).hexdigest()

    def shutdown(self):
        with self.cv:
            for item in list(self.pending):
                item.result = "deny"
                self.remove_locked(item)
            self.cv.notify_all()


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "ClaudeLanBridge/2.0"

    @property
    def bridge(self):
        return self.server.bridge

    def setup(self):
        super().setup()
        self.connection.settimeout(8)
        self.response_nonce = ""

    def log_message(self, fmt, *args):
        # Do not log tool inputs, credentials, or request paths containing queries.
        pass

    def read_json(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > MAX_BODY or self.headers.get("Transfer-Encoding"):
                return b"", None
            body = self.rfile.read(length)
            value = json.loads(body.decode("utf-8"))
            return body, value if isinstance(value, dict) else None
        except (ValueError, OSError):
            return b"", None

    def authorized(self, body):
        nonce = self.headers.get("X-Passport-Nonce", "")
        ok = self.bridge.authorize(self.command, self.path, body, nonce,
                                 self.headers.get("X-Claude-Bridge-Signature", ""))
        if ok:
            self.response_nonce = nonce
        return ok

    def reply(self, status, value=None):
        data = b"" if value is None else json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        if self.response_nonce:
            self.send_header("X-Passport-Response", self.bridge.response_signature(self.response_nonce, status, data))
        self.end_headers()
        try:
            if data:
                self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        body, payload = self.read_json()
        if self.path == "/hook":
            # No browser Origin accepted; a LAN caller cannot enqueue fake prompts.
            if (not ipaddress.ip_address(self.client_address[0]).is_loopback or
                self.headers.get("Origin") or not self.authorized(body)):
                self.reply(403, {"error": "local authenticated hook required"})
                return
            if payload is None or payload.get("hook_event_name") != "PermissionRequest":
                self.reply(400, hook_output("deny"))
                return
            item = self.bridge.enqueue(payload)
            if item is None:
                self.reply(200, hook_output("deny"))
                return
            self.reply(200, hook_output(self.bridge.wait_result(item)))
            return
        if self.path == "/v1/decision":
            if not self.authorized(body) or payload is None:
                self.reply(401, {"error": "unauthorized"})
                return
            ok = self.bridge.decide(text(payload.get("id"), 40), text(payload.get("decision"), 8), text(payload.get("challenge"), 64))
            self.reply(200 if ok else 409, {"accepted": ok})
            return
        self.reply(404)

    def do_GET(self):
        if self.path == RADIO_PATH:
            self.server.radio.serve(self)
            return
        if self.path == "/health":
            self.reply(200, {"ok": True, "protocol": 2})
            return
        if self.path not in ("/v1/pending", "/v1/idle"):
            self.reply(404)
            return
        if not self.authorized(b""):
            self.reply(401, {"error": "unauthorized"})
            return
        if self.path == "/v1/idle":
            self.reply(200, self.bridge.companion.snapshot())
            return
        item = self.bridge.next_for_device()
        if item is None:
            self.reply(204)
            return
        self.reply(200, {"id": item.request_id, "challenge": item.challenge, "tool": item.tool,
                        "summary": item.summary, "cwd": item.cwd,
                        "deadline_ms": int(max(0, item.deadline-time.monotonic())*1000)})


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 16
    # Bound slow clients and hook waiters. Invalid clients cannot consume unbounded threads.
    def __init__(self, address, bridge):
        self.slots = threading.BoundedSemaphore(20)
        self.bridge = bridge
        self.radio = RadioRelay()
        super().__init__(address, Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, help="private JSON created by setup.py")
    args = parser.parse_args()
    with open(args.config, encoding="utf-8") as file:
        config = json.load(file)
    companion = Companion(config)
    bridge = Bridge(config["token"].encode(), config.get("timeout", DEFAULT_TIMEOUT), companion)
    server = Server((config.get("bind", "0.0.0.0"), config.get("port", 8765)), bridge)
    companion.start()
    print(f"Passport bridge ready on port {server.server_port}; Ctrl+C stops it", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        bridge.shutdown()
        companion.stop()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
