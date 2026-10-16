#!/usr/bin/env python3
"""Fail-closed Claude hook. Only bounded fields are sent to the local bridge."""
from __future__ import annotations
import argparse
import hashlib
import hmac
import json
import secrets
import sys
import urllib.request
from bridge import hook_output, summarize_hook


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    args = parser.parse_args()
    result = hook_output("deny")
    try:
        with open(args.config, encoding="utf-8") as file:
            config = json.load(file)
        raw = sys.stdin.buffer.read(1024*1024 + 1)
        if len(raw) > 1024*1024:
            raise ValueError("hook input exceeds limit")
        payload = json.loads(raw)
        if payload.get("hook_event_name") != "PermissionRequest":
            raise ValueError("wrong event")
        summary = summarize_hook(payload)
        data = {"hook_event_name": "PermissionRequest", "tool_name": summary["tool"],
                "cwd": summary["cwd"], "tool_input": {"command": summary["summary"]}}
        body = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode()
        nonce = secrets.token_hex(16)
        token = config["token"].encode()
        signature = hmac.new(token, b"POST\n/hook\n"+nonce.encode()+b"\n"+body, hashlib.sha256).hexdigest()
        request = urllib.request.Request(f"http://127.0.0.1:{config.get('port',8765)}/hook", data=body,
                 headers={"Content-Type": "application/json", "X-Passport-Nonce": nonce,
                          "X-Claude-Bridge-Signature": signature}, method="POST")
        # Disable system proxies for this loopback-only exchange.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open(request, timeout=130) as response:
            answer = response.read(4096)
            expected = hmac.new(token, nonce.encode()+b"\n200\n"+answer, hashlib.sha256).hexdigest()
            if not hmac.compare_digest(expected, response.headers.get("X-Passport-Response", "")):
                raise ValueError("response authentication failed")
        candidate = json.loads(answer)
        behavior = candidate["hookSpecificOutput"]["decision"]["behavior"]
        if candidate["hookSpecificOutput"]["hookEventName"] != "PermissionRequest" or behavior not in ("allow", "deny"):
            raise ValueError("invalid decision")
        result = hook_output(behavior)
    except Exception:
        print("Passport permission bridge unavailable: denying this request", file=sys.stderr)
    sys.stdout.write(json.dumps(result, ensure_ascii=False, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
