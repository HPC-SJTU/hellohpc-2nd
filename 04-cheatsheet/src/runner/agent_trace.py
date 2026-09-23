"""Shared, streaming OpenCode trace artifacts.

Both the one-shot debugger and the formal evaluator use this sink so that
tool/error classification, redaction, terminal progress, and persisted logs do
not drift apart.
"""

from __future__ import annotations

import json
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional, Sequence, TextIO

try:
    from . import opencode_agent
except ImportError:
    import opencode_agent  # type: ignore


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def clean_text(value: object, secrets: Sequence[str] = ()) -> str:
    text = ANSI_RE.sub("", str(value))
    for secret in secrets:
        if secret:
            text = text.replace(secret, "[REDACTED]")
    return "".join(ch for ch in text if ch in "\n\r\t" or ord(ch) >= 32)


def sanitize_value(value: object, secrets: Sequence[str]) -> object:
    if isinstance(value, str):
        return clean_text(value, secrets)
    if isinstance(value, dict):
        return {
            clean_text(key, secrets): sanitize_value(item, secrets)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [sanitize_value(item, secrets) for item in value]
    return value


def timestamp(value: object = None) -> str:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        seconds = float(value) / 1000.0 if float(value) > 10_000_000_000 else float(value)
        try:
            return datetime.fromtimestamp(seconds, tz=timezone.utc).isoformat()
        except (OverflowError, OSError, ValueError):
            pass
    return datetime.now(timezone.utc).isoformat()


def json_line(stream: TextIO, value: object) -> None:
    stream.write(json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n")
    stream.flush()


def open_code_error_text(value: object, secrets: Sequence[str] = ()) -> str:
    """Return one concise, sanitized provider/OpenCode error message."""
    if isinstance(value, dict):
        name = clean_text(value.get("name") or "OpenCodeError", secrets)
        data = value.get("data") if isinstance(value.get("data"), dict) else {}
        message = data.get("message") or value.get("message") or value.get("error")
        if message:
            return f"{name}: {clean_text(message, secrets)}"
        return f"{name}: {clean_text(json.dumps(value, ensure_ascii=False), secrets)}"
    return clean_text(value or "unknown OpenCode error", secrets)


def extract_process_errors(raw: str, secrets: Sequence[str] = ()) -> list[str]:
    errors: list[str] = []
    for obj in opencode_agent._iter_json_objects(raw):
        if not isinstance(obj, dict) or obj.get("type") != "error":
            continue
        message = open_code_error_text(obj.get("error") or obj, secrets)
        if message and message not in errors:
            errors.append(message)
    return errors


class AgentTrace:
    """Persist normalized OpenCode events while emitting a concise live trace."""

    def __init__(
        self,
        output: Path,
        *,
        events_path: Optional[Path] = None,
        secrets: Sequence[str] = (),
        terminal_output_limit: int = 2000,
        step_limit: Optional[int] = None,
        terminal: bool = True,
    ) -> None:
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.secrets = tuple(secret for secret in secrets if secret)
        self.terminal_output_limit = terminal_output_limit
        self.step_limit = step_limit
        self.terminal = terminal
        self._events = (events_path or (self.output / "events.jsonl")).open(
            "a", encoding="utf-8", newline=""
        )
        self._timeline = (self.output / "timeline.log").open(
            "a", encoding="utf-8", newline=""
        )
        self._tools = (self.output / "tool-calls.jsonl").open(
            "a", encoding="utf-8", newline=""
        )
        self._stderr = (self.output / "agent-stderr.log").open(
            "a", encoding="utf-8", newline=""
        )
        self._errors = (self.output / "agent-errors.log").open(
            "a", encoding="utf-8", newline=""
        )
        self._call_indices: dict[str, int] = {}
        self._announced_calls: set[str] = set()
        self._seen_events: dict[str, int] = {}
        self._seen_steps: set[tuple[object, object]] = set()
        self._step_count = 0
        self._raw_stdout: list[str] = []
        self._closed = False

    def _timeline_line(self, value: object, *, terminal: bool = True) -> None:
        cleaned = clean_text(value, self.secrets).rstrip("\r\n")
        if not cleaned:
            return
        line = f"{timestamp()} {cleaned}"
        self._timeline.write(line + "\n")
        self._timeline.flush()
        if terminal and self.terminal:
            shown = cleaned
            if len(shown) > self.terminal_output_limit:
                shown = shown[: self.terminal_output_limit].rstrip() + (
                    f"\n       [... truncated; full event: {self.output / 'events.jsonl'}]"
                )
            print(shown, flush=True)

    def feed(self, label: str, chunk: str) -> None:
        if label == "stdout":
            self.feed_stdout(chunk)
        else:
            self.feed_stderr(chunk)

    def feed_stderr(self, chunk: str) -> None:
        cleaned = clean_text(chunk, self.secrets)
        self._stderr.write(cleaned)
        self._stderr.flush()
        for line in cleaned.splitlines():
            self._timeline_line(f"[agent-stderr] {line}")

    def feed_stdout(self, chunk: str) -> None:
        self._raw_stdout.append(chunk)
        objects = list(opencode_agent._iter_json_objects(chunk))
        if not objects:
            for line in clean_text(chunk, self.secrets).splitlines():
                if line.strip():
                    self._timeline_line(f"[agent-stdout] {line}")
            return
        for obj in objects:
            self._record_event(obj)

    def _record_event(self, obj: object) -> None:
        safe = sanitize_value(obj, self.secrets)
        encoded = json.dumps(safe, ensure_ascii=False, separators=(",", ":"))
        self._seen_events[encoded] = self._seen_events.get(encoded, 0) + 1
        self._events.write(encoded + "\n")
        self._events.flush()

        record = opencode_agent._tool_event_record(safe)
        if record is not None:
            self._record_tool(record)
            return
        if not isinstance(safe, dict):
            return
        event_type = safe.get("type")
        part = safe.get("part") if isinstance(safe.get("part"), dict) else {}
        if event_type == "text" and isinstance(part.get("text"), str):
            text = part["text"].strip()
            if text:
                self._timeline_line(f"[agent] {text}")
        elif event_type == "error":
            error = safe.get("error") or part.get("error") or "unknown OpenCode error"
            message = open_code_error_text(error, self.secrets)
            self._errors.write(f"{timestamp(safe.get('timestamp'))} {message}\n")
            self._errors.flush()
            self._timeline_line(f"[agent] ERROR: {message}")
        elif event_type == "step_finish" and part.get("type") == "step-finish":
            step_id = part.get("id") or part.get("messageID")
            identity = (
                (safe.get("sessionID"), step_id)
                if step_id is not None
                else ("event", encoded)
            )
            if identity in self._seen_steps:
                return
            self._seen_steps.add(identity)
            self._step_count += 1
            tokens = part.get("tokens") if isinstance(part.get("tokens"), dict) else {}
            total = tokens.get("total")
            suffix = f" tokens={int(total)}" if isinstance(total, (int, float)) else ""
            position = (
                f"{self._step_count}/{self.step_limit}"
                if self.step_limit is not None
                else str(self._step_count)
            )
            self._timeline_line(f"[step {position}] finished{suffix}")

    def _record_tool(self, record: dict[str, object]) -> None:
        call_id = str(record.get("call_id") or f"event-{len(self._call_indices) + 1}")
        if call_id not in self._call_indices:
            self._call_indices[call_id] = len(self._call_indices) + 1
        normalized = dict(record)
        normalized["index"] = self._call_indices[call_id]
        normalized["timestamp"] = timestamp(record.get("timestamp"))
        normalized = sanitize_value(normalized, self.secrets)  # type: ignore[assignment]
        json_line(self._tools, normalized)

        tool = str(record.get("tool") or "tool")
        status = str(record.get("status") or "unknown")
        inputs = record.get("input") if isinstance(record.get("input"), dict) else {}
        output = clean_text(record.get("output") or "", self.secrets).rstrip()
        error = clean_text(record.get("error") or "", self.secrets).rstrip()
        rc = record.get("returncode")
        duration = record.get("duration_seconds")
        duration_text = (
            f" duration={float(duration):.2f}s"
            if isinstance(duration, (int, float))
            else ""
        )
        failed = status == "error" or (isinstance(rc, int) and rc != 0)
        result_word = "ERROR" if failed else "ok"

        if tool == "skill":
            name = inputs.get("name") if isinstance(inputs, dict) else None
            name = str(name or "<unknown>")
            if status in {"pending", "running"}:
                line = f"[skill] {name} -> loading"
            elif failed:
                line = f"[skill] {name} -> ERROR: {error or output or 'tool failed'}"
            else:
                line = f"[skill] {name} -> ok"
            self._timeline_line(line)
        elif tool == "read":
            target = inputs.get("filePath") or inputs.get("path") or inputs.get("file")
            self._timeline_line(f"[read] {target or '<unknown>'} -> {result_word}")
        elif tool in {"edit", "write", "apply_patch"}:
            target = inputs.get("filePath") or inputs.get("path") or inputs.get("file")
            state = "ERROR" if failed else ("completed" if status == "completed" else status)
            self._timeline_line(f"[edit] {target or '<unknown>'} -> {state}")
        elif tool == "bash":
            command = inputs.get("command") if isinstance(inputs, dict) else None
            if call_id not in self._announced_calls:
                self._timeline_line(f"[bash] $ {command or '<unknown>'}")
                self._announced_calls.add(call_id)
            if status not in {"pending", "running"}:
                rc_text = str(rc) if isinstance(rc, int) else ("error" if failed else "n/a")
                self._timeline_line(f"[bash] rc={rc_text}{duration_text}")
        else:
            self._timeline_line(f"[{tool}] -> {status}")

        detail = error or output
        if detail and status not in {"pending", "running"}:
            for line in detail.splitlines():
                self._timeline_line(f"       {line}")

    def finalize(self, raw_stdout: Optional[str] = None) -> None:
        """Parse the complete buffer once to recover non-JSONL/pretty JSON events."""
        raw = raw_stdout if raw_stdout is not None else "".join(self._raw_stdout)
        complete_counts: dict[str, int] = {}
        for obj in opencode_agent._iter_json_objects(raw):
            safe = sanitize_value(obj, self.secrets)
            encoded = json.dumps(safe, ensure_ascii=False, separators=(",", ":"))
            complete_counts[encoded] = complete_counts.get(encoded, 0) + 1
            if complete_counts[encoded] > self._seen_events.get(encoded, 0):
                self._record_event(obj)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        for stream in (
            self._events,
            self._timeline,
            self._tools,
            self._stderr,
            self._errors,
        ):
            stream.flush()
            stream.close()
