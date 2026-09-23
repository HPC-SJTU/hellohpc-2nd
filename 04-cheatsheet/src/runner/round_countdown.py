"""Evaluator-owned streaming adapter; inject budget notices without enabling plugins.

Only the configured provider's chat-completions endpoint is reachable. Neither
credentials nor request/response bodies are written to evidence. Identical
request retries retain their number; auxiliary OpenCode requests are untouched.
"""
from __future__ import annotations
import copy
import hashlib
import hmac
import json
import secrets
import threading
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SYSTEM_MARKER = "You are optimizing one CPU kernel inside a restricted evaluator."


BUDGET_ERROR = "CHEATSHEET_PHASE_BUDGET_EXHAUSTED"


class RoundBudgetExceeded(Exception):
    pass


class Countdown:
    def __init__(self, limit: int):
        self.lock = threading.Lock()
        self.records = []
        self.blocks = []
        self.begin_phase("optimization", limit)

    def begin_phase(self, phase: str, limit: int):
        if phase not in ("optimization", "finalization") or type(limit) is not int or limit < 1:
            raise ValueError("invalid countdown phase or limit")
        with self.lock:
            self.phase, self.limit, self.seen = phase, limit, {}

    @property
    def used(self):
        with self.lock:
            return len(self.seen)

    @property
    def exhausted_marker(self):
        with self.lock:
            return f"{BUDGET_ERROR}:{self.phase}:{self.limit}"

    def inject(self, body: dict) -> dict:
        messages = body.get("messages", [])
        if not any(m.get("role") == "system" and SYSTEM_MARKER in str(m.get("content", "")) for m in messages):
            return body
        fingerprint = hashlib.sha256(json.dumps(
            {"model": body.get("model"), "messages": messages},
            sort_keys=True, ensure_ascii=False, separators=(",", ":"),
        ).encode()).hexdigest()
        with self.lock:
            retry = fingerprint in self.seen
            if not retry and len(self.seen) >= self.limit:
                marker = f"{BUDGET_ERROR}:{self.phase}:{self.limit}"
                self.blocks.append(dict(phase=self.phase, limit=self.limit,
                                        used=len(self.seen), marker=marker))
                raise RoundBudgetExceeded(marker)
            number = self.seen.setdefault(fingerprint, len(self.seen) + 1)
            phase, limit = self.phase, self.limit
            remaining = max(0, limit - number)
            self.records.append(dict(phase=phase, round=number, limit=limit,
                                     remaining_after_response=remaining, retry=retry))
        notice = (f"EVALUATOR ROUND BUDGET: {phase.capitalize()} round {number}/{limit}. "
                  f"After this response, at most {remaining} {phase} rounds remain. "
                  "This response consumes one round, including reads and rejected tool calls. ")
        if phase == "optimization":
            notice += "A separate finalization phase follows."
        else:
            notice += "Restore and validate the best tested correct files now; do not start a new optimization."
        if remaining == 0:
            notice += " This is the last round of this phase; OpenCode may require a tool-free summary."
        result = copy.deepcopy(body)
        # Place one fresh notice in the system prompt; never accumulate notices
        # in the persisted conversation or modify tool schemas/results.
        # Qwen's chat template accepts only one leading system message. Merge
        # OpenCode's leading system blocks and the notice into that message;
        # leave user/assistant/tool history and structured text parts untouched.
        leading = 0
        while leading < len(messages) and messages[leading].get("role") == "system":
            leading += 1
        if not leading or any(m.get("role") == "system" for m in messages[leading:]):
            raise ValueError("system instructions must precede conversation history")
        contents = [notice] + [m["content"] for m in result["messages"][:leading]]
        if all(isinstance(content, str) for content in contents):
            combined = "\n\n".join(contents)
        else:
            combined = []
            for content in contents:
                if isinstance(content, str):
                    combined.append({"type": "text", "text": content})
                elif isinstance(content, list):
                    combined.extend(content)
                else:
                    raise ValueError("unsupported system content")
        result["messages"] = [dict(result["messages"][0], content=combined)] + result["messages"][leading:]
        return result


class CountdownRelay:
    def __init__(self, upstream: str, api_key: str, limit: int, *, proxy: str | None = None, timeout: float = 900):
        parsed = urllib.parse.urlsplit(upstream)
        if parsed.scheme not in ("http", "https") or not parsed.netloc or parsed.query or parsed.fragment or parsed.username:
            raise ValueError("invalid model endpoint")
        self.url = upstream.rstrip("/") + "/chat/completions"
        self.api_key, self.timeout = api_key, timeout
        self.token = secrets.token_urlsafe(32)
        self.countdown = Countdown(limit)
        # Honor only the evaluator's explicit proxy, not ambient process settings.
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({"https": proxy} if proxy else {}),
            _NoRedirect(),
        )
        relay = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def do_POST(self):
                self.close_connection = True
                if self.path != "/v1/chat/completions":
                    self.send_error(404); return
                if not hmac.compare_digest(self.headers.get("Authorization", ""), "Bearer " + relay.token):
                    self.send_error(403); return
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                    if not 0 < length <= 64 * 1024 * 1024:
                        raise ValueError("invalid length")
                    body = json.loads(self.rfile.read(length))
                    payload = json.dumps(relay.countdown.inject(body), ensure_ascii=False).encode()
                except RoundBudgetExceeded as error:
                    # A non-retryable local control error: do not call the provider
                    # or invent an assistant response/usage record.
                    payload = json.dumps({"error": {"message": str(error),
                        "type": "evaluator_round_budget", "code": BUDGET_ERROR}}).encode()
                    self.send_response(400)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)
                    return
                except (ValueError, TypeError, AttributeError):
                    self.send_error(400); return
                request = urllib.request.Request(relay.url, data=payload, headers={
                    "Authorization": "Bearer " + relay.api_key,
                    "Content-Type": "application/json", "Accept": self.headers.get("Accept", "text/event-stream"),
                })
                try:
                    response = relay.opener.open(request, timeout=relay.timeout)
                except urllib.error.HTTPError as error:
                    response = error
                except (OSError, urllib.error.URLError):
                    self.send_error(502, "Model endpoint unavailable"); return
                try:
                    with response:
                        self.send_response(response.status)
                        for name in ("Content-Type", "Retry-After"):
                            if response.headers.get(name):
                                self.send_header(name, response.headers[name])
                        self.send_header("Connection", "close")
                        self.end_headers()
                        while chunk := response.read1(65536):
                            self.wfile.write(chunk)
                            self.wfile.flush()
                except (OSError, ValueError):
                    # A disconnected/timed-out OpenCode client must not log keys
                    # or keep a streaming handler alive indefinitely.
                    pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.daemon_threads = True
        self.base_url = f"http://127.0.0.1:{self.server.server_port}/v1"

    def __enter__(self):
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None
