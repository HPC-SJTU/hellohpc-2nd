#!/usr/bin/env python3
"""Framework-neutral local DPSK Stage2 self-test.

Public API: run_stage(*, dataset=DEFAULT_DATASET, model, base_url=..., output=None,
calibration=None, stage_start_monotonic=None, ignore_eos=True)
-> report dict. execute_stage is an alias. Paths accept
Path or str. An explicit output must not exist; None chooses a unique filename.
An omitted calibration discovers calibration.json beside the selected manifest;
missing or pending calibration permits measurement, but leaves scores null.
The caller owns service startup and cleanup. No service launch, restart,
cache manipulation or tokenizer endpoint is used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import signal
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Sequence

import benchmark as legacy
import dpsk_contract as contract


DEFAULT_DATASET = Path("data/dpsk-final/manifest.json")
DEFAULT_CALIBRATION = DEFAULT_DATASET.with_name("calibration.json")
DEFAULT_BASE_URL = "http://127.0.0.1:8000"
MAX_JSON_BYTES = 4 * 1024 * 1024
MAX_ROW_BYTES = 16 * 1024 * 1024
MAX_RESPONSE_BYTES = 32 * 1024 * 1024
MAX_LINE_BYTES = 1024 * 1024
MAX_TEXT_CHARS = 16 * 1024 * 1024
MAX_EVENTS = 65536
CANCEL_POLL_SECONDS = 0.05
WORKER_DRAIN_SECONDS = 2.0


class DPSKError(RuntimeError):
    """Invalid input, expired deadline, or failed API contract."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise DPSKError(message)


def integer(value: Any, minimum: int = 0) -> bool:
    return type(value) is int and value >= minimum


def text(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def digest_ok(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def encoded(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, allow_nan=False,
                      sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def strict_json(data: bytes | str) -> Any:
    def pairs(items: list[tuple[str, Any]]) -> dict:
        result: dict = {}
        for key, value in items:
            require(key not in result, f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def bad_constant(value: str) -> None:
        raise DPSKError(f"invalid JSON number: {value}")

    try:
        return json.loads(data.decode("utf-8") if isinstance(data, bytes) else data,
                          object_pairs_hook=pairs, parse_constant=bad_constant)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise DPSKError("invalid UTF-8 JSON") from exc


def fields(value: Any, shape: Any, label: str) -> None:
    require(isinstance(value, dict) and set(value) == set(shape.__required_keys__),
            f"{label}: invalid fields")


def remaining(deadline: float, cancellation: Cancellation | None = None) -> float:
    if cancellation is not None:
        cancellation.check()
    seconds = deadline - time.monotonic()
    require(seconds > 0, "absolute deadline exceeded")
    return seconds


class Cancellation:
    """Stage-wide cooperative stop; signal handlers only assign a Python flag."""

    def __init__(self):
        self.event = threading.Event()
        self.signum: int | None = None

    def is_set(self) -> bool:
        return self.signum is not None or self.event.is_set()

    def set(self) -> None:
        self.event.set()

    def check(self) -> None:
        require(not self.is_set(), f"stage cancelled ({signal.Signals(self.signum).name})"
                if self.signum is not None else "stage cancelled")

    @contextmanager
    def signals(self):
        previous = {}

        def handler(signum, frame):
            # No locks, I/O, exceptions, or socket operations in a signal handler.
            self.signum = signum

        try:
            if threading.current_thread() is threading.main_thread():
                for signum in (signal.SIGINT, signal.SIGTERM):
                    previous[signum] = signal.signal(signum, handler)
            yield
        finally:
            for signum, old_handler in previous.items():
                signal.signal(signum, old_handler)


class Evidence:
    """Exclusive artifacts; report and evidence directory are never overwritten."""

    def __init__(self, output: Path | str | None):
        self.output = Path(output) if output is not None else Path(
            f"dpsk-stage2-{time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())}-{uuid.uuid4().hex[:12]}.json")
        self.output = self.output.absolute()
        require(self.output.parent.is_dir(), "output parent directory must exist")
        self.root = self.output.with_name(self.output.name + ".evidence")
        require(not self.root.exists(), f"evidence directory already exists: {self.root}")
        # Reserve first; do not overwrite an earlier self-test.
        fd = os.open(self.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        os.close(fd)
        try:
            self.root.mkdir(mode=0o700)
        except BaseException:
            self.output.unlink()
            raise
        self.checkpoint_index = 0

    def put(self, name: str, data: bytes) -> dict:
        path = self.root / name
        with path.open("xb") as stream:
            stream.write(data)
        return {"path": name, "sha256": sha(data), "bytes": len(data)}

    def finish(self, report: dict) -> None:
        # Reserved output belongs to this run; atomically replace only our file.
        legacy.write_json(self.output, report)
        os.chmod(self.output, 0o600)

    def checkpoint(self, report: dict, boundary: str) -> None:
        """Append-only durable snapshots, including every pending request slot."""
        name = f"checkpoint-{self.checkpoint_index:04d}.json"
        self.checkpoint_index += 1
        with (self.root / name).open("xb") as stream:
            stream.write(encoded({"boundary": boundary, "report": report}))
            stream.flush()
            os.fsync(stream.fileno())
        fd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


@dataclass
class Dataset:
    manifest: dict
    manifest_sha256: str
    rows: dict[str, list[dict]]
    artifacts: dict


def snapshot_json(path: Path, evidence: Evidence, name: str) -> tuple[Any, dict]:
    with path.open("rb") as stream:
        data = stream.read(MAX_JSON_BYTES + 1)
    require(len(data) <= MAX_JSON_BYTES, f"JSON too large: {path}")
    artifact = evidence.put(name, data)
    return strict_json(data), artifact


def load_dataset(path: Path | str, evidence: Evidence, deadline: float,
                 cancellation: Cancellation | None = None) -> Dataset:
    """Hash and parse the SAME byte stream, preserving a full evidence snapshot."""
    path = Path(path).resolve()
    remaining(deadline, cancellation)
    manifest, manifest_artifact = snapshot_json(path, evidence, "manifest.json")
    fields(manifest, contract.Manifest, "manifest")
    require(type(manifest["schema_version"]) is int and manifest["schema_version"] == contract.SCHEMA_VERSION
            and manifest["protocol"] == contract.PROTOCOL, "unsupported dataset protocol")
    require(manifest["split"] == "public", "expected public dataset split")
    for key in ("model_id", "model_revision"):
        require(text(manifest[key]), f"invalid {key}")
    require(manifest["model_revision"].lower() not in ("main", "master", "latest"),
            "model revision must be pinned")
    tokenizer = manifest["tokenizer"]
    fields(tokenizer, contract.TokenizerSpec, "tokenizer")
    require(text(tokenizer["id"]) and text(tokenizer["revision"]), "invalid tokenizer identity")
    require(tokenizer["revision"].lower() not in ("main", "master", "latest"),
            "tokenizer revision must be pinned")
    hashes = tokenizer["files_sha256"]
    require(isinstance(hashes, dict) and bool(hashes)
            and all(text(k) and digest_ok(v) for k, v in hashes.items()), "invalid tokenizer hashes")
    require(tokenizer["digest"] == sha(encoded(hashes)), "tokenizer digest mismatch")
    require(digest_ok(tokenizer["chat_template_sha256"])
            and type(tokenizer["gsm_template_verified"]) is bool, "invalid template metadata")
    reference = manifest["accuracy_reference"]
    fields(reference, contract.AccuracyReference, "accuracy_reference")
    require(reference["protocol"] == contract.GSM_PROTOCOL
            and type(reference["total"]) is int and reference["total"] == contract.GSM_COUNT,
            "invalid accuracy reference protocol/count")
    require(reference["status"] in ("pending", "validated"), "invalid accuracy reference status")
    if reference["status"] == "validated":
        require(integer(reference["correct"], 1) and reference["correct"] <= contract.GSM_COUNT
                and text(reference["evidence"]), "invalid validated accuracy reference")
    else:
        require(reference["correct"] is None and reference["evidence"] is None,
                "pending accuracy reference must not claim a baseline")
    files = manifest["files"]
    fields(files, contract.DatasetFiles, "files")
    counts = {name: contract.PROFILES[name]["warmup_requests"] + contract.PROFILES[name]["formal_requests"]
              for name in ("d1",)}
    counts.update(d2=contract.PROFILES["d2"]["sessions"], gsm=contract.GSM_COUNT)
    rows: dict[str, list[dict]] = {}
    artifacts = {"manifest": manifest_artifact}
    manifest_stat = path.stat()
    identities: set[tuple[int, int]] = {(manifest_stat.st_dev, manifest_stat.st_ino)}
    for name, spec in files.items():
        fields(spec, contract.FileSpec, f"files.{name}")
        require(text(spec["path"]) and not Path(spec["path"]).is_absolute(), "invalid relative file path")
        source = (path.parent / spec["path"]).resolve()
        require(source.is_relative_to(path.parent) and source != path, "dataset path escapes/aliases manifest")
        stat = source.stat()
        identity = (stat.st_dev, stat.st_ino)
        require(identity not in identities and source.is_file(), "dataset files alias or are not regular files")
        identities.add(identity)
        require(digest_ok(spec["sha256"]) and integer(spec["records"], 1), "invalid file hash/count")
        if name in counts:
            require(spec["records"] == counts[name], f"{name}: incorrect declared count")
        digest = hashlib.sha256()
        records: list[dict] = []
        target = evidence.root / f"dataset-{name}.jsonl"
        with source.open("rb") as stream, target.open("xb") as snapshot:
            while True:
                remaining(deadline, cancellation)
                line = stream.readline(MAX_ROW_BYTES + 1)
                if not line:
                    break
                require(len(line) <= MAX_ROW_BYTES, f"{name}: JSONL row too large")
                snapshot.write(line)
                digest.update(line)
                if line.strip():
                    records.append(strict_json(line))
                    require(len(records) <= spec["records"], f"{name}: too many records")
        require(digest.hexdigest() == spec["sha256"], f"{name}: dataset SHA256 mismatch")
        require(len(records) == spec["records"], f"{name}: record count mismatch")
        rows[name] = records
        artifacts[name] = {"path": target.name, "sha256": digest.hexdigest(), "bytes": target.stat().st_size}
    validate_records(rows, deadline, cancellation)
    return Dataset(manifest, manifest_artifact["sha256"], rows, artifacts)


def validate_records(rows: dict[str, list[dict]], deadline: float,
                     cancellation: Cancellation | None = None) -> None:
    shapes = {"d1": contract.PerformanceRecord,
              "d2": contract.SessionRecord, "gsm": contract.GSMRecord,
              "provenance": contract.ProvenanceRecord}
    for name, records in rows.items():
        ids: set[str] = set()
        for record in records:
            remaining(deadline, cancellation)
            fields(record, shapes[name], name)
            require(text(record["id"]) and record["id"] not in ids, f"{name}: invalid/duplicate id")
            ids.add(record["id"])
    provenance = {r["id"] for r in rows["provenance"]}
    for record in rows["provenance"]:
        require(text(record["source"]) and text(record["source_id"])
                and isinstance(record["details"], dict), "invalid provenance")

    def refs(values: Any) -> None:
        require(isinstance(values, list) and bool(values)
                and all(isinstance(v, str) and v in provenance for v in values)
                and len(set(values)) == len(values), "invalid/unresolved source_refs")

    def answer(value: Any) -> None:
        require(text(value) and legacy._exact_number(value) is not None, "invalid exact expected_answer")

    for name in ("d1", "d2", "gsm"):
        gsm_prompts: set[str] = set()
        for index, record in enumerate(rows[name]):
            remaining(deadline, cancellation)
            refs(record["source_refs"])
            tokens = record["prompt_ids"]
            require(isinstance(tokens, list) and bool(tokens)
                    and all(integer(t) for t in tokens), f"{name}: invalid prompt_ids")
            if name == "d1":
                profile = contract.PROFILES[name]
                require(len(tokens) == profile["input_tokens"], f"{name}: incorrect input length")
                warm = index < profile["warmup_requests"]
                require(record["phase"] == ("warmup" if warm else "formal"), f"{name}: incorrect phase order")
                require(record["kind"] in ("traffic", "math") and (not warm or record["kind"] == "traffic"),
                        f"{name}: invalid kind/warmup math")
                if record["kind"] == "math":
                    answer(record["expected_answer"])
                else:
                    require(record["expected_answer"] is None, "traffic must not have an answer")
            elif name == "d2":
                require(type(record["session_index"]) is int and record["session_index"] == index,
                        "D2 session order mismatch")
                require(len(tokens) == contract.D2_INPUT_LENGTHS[-1], "D2 final sequence length mismatch")
                probes = record["math_probes"]
                require(isinstance(probes, list), "invalid D2 math_probes")
                wanted = [turn for session, turn in contract.D2_MATH_LOCATIONS if session == index]
                require(len(probes) == len(wanted), "D2 math probe count mismatch")
                for probe, turn in zip(probes, wanted):
                    fields(probe, contract.MathProbe, "D2 math probe")
                    require(type(probe["turn"]) is int and probe["turn"] == turn, "D2 math probe location mismatch")
                    refs(probe["source_refs"])
                    answer(probe["expected_answer"])
            else:
                answer(record["expected_answer"])
                identity = sha(encoded(tokens))
                require(identity not in gsm_prompts, "duplicate GSM prompt")
                gsm_prompts.add(identity)
        if name == "d1":
            require(sum(r["kind"] == "math" for r in rows[name]) == contract.PROFILES[name]["math_requests"],
                    f"{name}: formal math count mismatch")


def resolve_calibration(dataset: Path | str, calibration: Path | str | None) -> Path | None:
    """Explicit paths must validate; only an absent discovered file means pending."""
    if calibration is not None:
        return Path(calibration)
    sibling = Path(dataset).resolve().with_name("calibration.json")
    return sibling if sibling.exists() or sibling.is_symlink() else None


def load_calibration(path: Path | str | None, dataset: Dataset, evidence: Evidence) -> tuple[dict | None, dict | None]:
    if path is None:
        return None, None
    value, artifact = snapshot_json(Path(path), evidence, "calibration.json")
    fields(value, contract.Calibration, "calibration")
    require(type(value["schema_version"]) is int and value["schema_version"] == contract.SCHEMA_VERSION
            and value["protocol"] == contract.PROTOCOL, "invalid calibration protocol")
    require(value["dataset_manifest_sha256"] == dataset.manifest_sha256, "calibration dataset binding mismatch")
    require(value["status"] in ("pending", "validated"), "invalid calibration status")
    require(value["evidence"] is None or text(value["evidence"]), "invalid calibration evidence")
    fields(value["cases"], contract.CalibrationCases, "calibration cases")
    for case, constants in value["cases"].items():
        fields(constants, contract.CaseCalibration, f"calibration {case}")
        for number in constants.values():
            require(number is None or (type(number) in (int, float) and math.isfinite(number) and number > 0),
                    "calibration constants must be null or finite positive numbers")
        if value["status"] == "validated":
            require(text(value["evidence"]) and all(v is not None for v in constants.values()),
                    "validated calibration is incomplete")
            require(constants["pf_output_tps"] > constants["p0_output_tps"], "calibration Pf must exceed P0")
    return value, artifact


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Returning None makes urllib expose HTTPError without following it;
        # exchange() then retains the redirect's original headers/body.
        return None


class Exchange:
    """A sealed per-request journal: timeout cannot race with later artifact writes."""

    def __init__(self, evidence: Evidence, name: str, request: urllib.request.Request):
        self.lock = threading.Lock()
        self.sealed = False
        self.response: Any = None
        self.raw = (evidence.root / f"{name}.response.bin").open("xb")
        self.digest = hashlib.sha256()
        self.size = 0
        self.metadata: dict = {"url": request.full_url, "method": request.get_method(),
                               "headers": dict(request.header_items())}
        self.request_artifact = evidence.put(f"{name}.request.json", encoded(self.metadata))
        self.body_artifact = evidence.put(f"{name}.request.body", request.data or b"")
        self.evidence = evidence
        self.name = name

    def opened(self, response: Any) -> None:
        with self.lock:
            if self.sealed:
                self.interrupt(response)
            require(not self.sealed, "request already timed out")
            self.response = response
            self.metadata["response_status"] = response.status
            self.metadata["response_headers"] = list(response.headers.items())

    def append(self, raw: bytes) -> None:
        with self.lock:
            require(not self.sealed, "request already timed out")
            require(self.size + len(raw) <= MAX_RESPONSE_BYTES, "response exceeds byte limit")
            self.raw.write(raw)
            self.digest.update(raw)
            self.size += len(raw)

    def finish(self, timed_out: bool) -> dict:
        with self.lock:
            if self.sealed:
                return self.result
            self.sealed = True
            self.raw.close()
            response = self.response
            headers = self.evidence.put(f"{self.name}.exchange.json", encoded(self.metadata))
            result = {"request": self.request_artifact, "request_body": self.body_artifact,
                      "exchange": headers,
                      "response": {"path": f"{self.name}.response.bin", "sha256": self.digest.hexdigest(),
                                    "bytes": self.size, "partial": timed_out}}
            self.result = result
        if timed_out and response is not None:
            self.interrupt(response)
        return result

    @staticmethod
    def interrupt(response: Any) -> None:
        threading.Thread(target=legacy.OpenAIStreamingClient._interrupt_response,
                         args=(response,), name="dpsk-abort", daemon=True).start()


class StreamingClient:
    def __init__(self, base_url: str, model: str, evidence: Evidence, ignore_eos: bool = True,
                 cancellation: Cancellation | None = None):
        parsed = urllib.parse.urlsplit(base_url)
        require(parsed.scheme in ("http", "https") and bool(parsed.hostname)
                and not parsed.username and not parsed.password and not parsed.query and not parsed.fragment
                and parsed.path in ("", "/"), "base URL must be an HTTP(S) origin without credentials")
        require(text(model), "model is required")
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.evidence = evidence
        self.ignore_eos = ignore_eos
        self.cancellation = cancellation or Cancellation()
        self.journal_lock = threading.Lock()
        self.journals: set[Exchange] = set()
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())

    def abort(self) -> None:
        self.cancellation.set()
        with self.journal_lock:
            for journal in self.journals:
                journal.finish(True)

    def exchange(self, name: str, request: urllib.request.Request, deadline: float,
                 consume: Callable[[Any, Exchange, float], dict]) -> dict:
        started = time.monotonic()
        self.cancellation.check()
        # Make implicit urllib headers explicit in the recorded request metadata.
        request.add_header("Host", request.host)
        request.add_header("User-Agent", contract.PROTOCOL)
        request.add_header("Connection", "close")
        if request.data is not None:
            request.add_header("Content-Length", str(len(request.data)))
        with self.journal_lock:
            self.cancellation.check()
            journal = Exchange(self.evidence, name, request)
            self.journals.add(journal)
        done = threading.Event()
        state: dict = {}

        def perform() -> None:
            try:
                try:
                    response = self.opener.open(request, timeout=remaining(deadline))
                except urllib.error.HTTPError as exc:
                    response = exc  # Preserve error status/body, not just its exception string.
                with response:
                    journal.opened(response)
                    if response.status != 200:
                        raw = response.read(MAX_JSON_BYTES + 1)
                        journal.append(raw)
                        raise DPSKError(f"HTTP status {response.status}")
                    state["result"] = consume(response, journal, deadline)
            except Exception as exc:
                state["error"] = f"{type(exc).__name__}: {exc}"
            finally:
                done.set()

        worker = threading.Thread(target=perform, name=f"dpsk-{name}", daemon=True)
        completed = False
        try:
            worker.start()
            while not self.cancellation.is_set() and time.monotonic() < deadline:
                if done.wait(min(CANCEL_POLL_SECONDS, max(0.0, deadline - time.monotonic()))):
                    completed = True
                    break
        finally:
            timed_out = not completed or time.monotonic() > deadline or self.cancellation.is_set()
            artifacts = journal.finish(timed_out)
            with self.journal_lock:
                self.journals.discard(journal)
        result = dict(state.get("result", {})) if not timed_out else {}
        result.update(request_started=started, evidence=artifacts)
        result.setdefault("completed_at", time.monotonic())
        if timed_out or "error" in state:
            result.update(status="failed", error="absolute HTTP deadline exceeded" if timed_out else state["error"])
            if self.cancellation.is_set():
                result["error"] = "stage cancelled"
            result["evidence"]["response"]["partial"] = True
        else:
            result["status"] = "ok"
        return result

    def readiness(self, stage_deadline: float, log: list[dict]) -> None:
        deadline = min(stage_deadline, time.monotonic() + contract.TIMEOUTS_SECONDS["readiness"])

        def consume(response: Any, journal: Exchange, deadline: float) -> dict:
            raw = response.read(MAX_JSON_BYTES + 1)
            journal.append(raw)
            remaining(deadline)
            require(len(raw) <= MAX_JSON_BYTES, "readiness body too large")
            value = strict_json(raw)
            require(isinstance(value, dict) and isinstance(value.get("data"), list), "invalid models response")
            models = [item.get("id") for item in value["data"] if isinstance(item, dict)]
            return {"model_advertised": self.model in models, "completed_at": time.monotonic()}

        while True:
            self.cancellation.check()
            remaining(deadline)
            request = urllib.request.Request(self.base_url + "/v1/models", headers={"Accept": "application/json"})
            result = self.exchange(f"readiness-{len(log):04d}", request,
                                   min(deadline, time.monotonic() + 5), consume)
            log.append(result)
            if result["status"] == "ok" and result["model_advertised"]:
                return
            retry_at = time.monotonic() + min(1.0, remaining(deadline))
            while time.monotonic() < retry_at:
                self.cancellation.check()
                self.cancellation.event.wait(min(CANCEL_POLL_SECONDS, retry_at - time.monotonic()))

    def complete(self, slot: dict, record: dict, deadline: float) -> dict:
        length = slot["input_tokens"]
        tokens = record["prompt_ids"]
        prompt = tokens if length == len(tokens) else tokens[:length]
        natural = slot["case"] == "gsm"
        maximum = slot["output_tokens"]
        body = {"model": self.model, "prompt": prompt, "max_tokens": maximum,
                "temperature": 0, "stream": True, "stream_options": {"include_usage": True}}
        if not natural:
            body["min_tokens"] = maximum
            if self.ignore_eos:
                body["ignore_eos"] = True
        request = urllib.request.Request(self.base_url + "/v1/completions", data=encoded(body),
                                         headers={"Accept": "text/event-stream", "Content-Type": "application/json"})
        request_deadline = min(deadline, time.monotonic() + contract.TIMEOUTS_SECONDS[slot["case"] + "_request"])
        result = self.exchange(slot["slot_id"], request, request_deadline,
                               lambda r, j, d: consume_sse(r, j, d, self.model, length, maximum, natural))
        if result["status"] == "ok":
            result["ttft"] = result["first_chunk_at"] - result["request_started"]
            count = result["completion_tokens"]
            result["tpot"] = (1000 * (result["completed_at"] - result["first_chunk_at"]) / (count - 1)
                              if count > 1 else None)
            result["extracted_answer"] = legacy.extract_final_answer(result["text"])
            expected = slot["expected_answer"]
            result["math_correct"] = (legacy.exact_math_match(result["text"], expected)
                                      and not result["truncated"]) if expected is not None else None
        return result


def consume_sse(response: Any, journal: Exchange, deadline: float, model: str,
                input_tokens: int, output_tokens: int, natural: bool) -> dict:
    require(response.headers.get_content_type() == "text/event-stream", "response is not SSE")
    pieces: list[str] = []
    size = events = 0
    first: float | None = None
    finish: str | None = None
    usage: tuple[int, int] | None = None
    data_lines: list[str] = []
    event_bytes = 0

    def event(data: str) -> bool:
        nonlocal size, events, first, finish, usage
        if data == "[DONE]":
            return True
        events += 1
        require(events <= MAX_EVENTS, "too many SSE events")
        value = strict_json(data)
        require(isinstance(value, dict) and "error" not in value, "invalid/error SSE event")
        if "model" in value:
            require(value["model"] == model, "stream model does not match requested API model")
        choices = value.get("choices")
        require(isinstance(choices, list) and len(choices) <= 1, "SSE must contain at most one choice")
        if choices:
            choice = choices[0]
            require(isinstance(choice, dict), "invalid SSE choice")
            require(type(choice.get("index", 0)) is int and choice.get("index", 0) == 0, "invalid choice index")
            chunk = choice.get("text", "")
            require(isinstance(chunk, str), "invalid SSE text")
            if chunk:
                require(finish is None, "text received after finish_reason")
                chunk.encode("utf-8")
                first = first if first is not None else time.monotonic()
                size += len(chunk)
                require(size <= MAX_TEXT_CHARS, "SSE text too large")
                pieces.append(chunk)
            reason = choice.get("finish_reason")
            if reason is not None:
                require(isinstance(reason, str) and finish is None, "invalid/duplicate finish_reason")
                finish = reason
        value_usage = value.get("usage")
        if value_usage is not None:
            require(isinstance(value_usage, dict), "invalid SSE usage")
            p, c = value_usage.get("prompt_tokens"), value_usage.get("completion_tokens")
            require(integer(p) and integer(c, 1), "missing/invalid token usage")
            if "total_tokens" in value_usage:
                require(integer(value_usage["total_tokens"]) and value_usage["total_tokens"] == p + c,
                        "inconsistent total token usage")
            require(usage is None or usage == (p, c), "conflicting SSE usage")
            usage = (p, c)
        return False

    done = False
    while not done:
        remaining(deadline)
        raw = response.readline(MAX_LINE_BYTES + 1)
        journal.append(raw)
        remaining(deadline)
        require(len(raw) <= MAX_LINE_BYTES, "SSE line too large")
        if not raw:
            if data_lines:
                done = event("\n".join(data_lines))
            break
        line = raw.decode("utf-8").rstrip("\r\n")
        if not line:
            if data_lines:
                done = event("\n".join(data_lines))
                data_lines = []
                event_bytes = 0
        elif line.startswith("data:"):
            fragment = line[5:]
            if fragment.startswith(" "):
                fragment = fragment[1:]
            data_lines.append(fragment)
            event_bytes += len(raw)
            require(event_bytes <= MAX_LINE_BYTES, "SSE event too large")
        # Comments and non-data SSE fields are permitted and still byte-capped.
    completed = time.monotonic()
    require(done and first is not None, "SSE missing DONE or nonempty text")
    require(usage is not None and usage[0] == input_tokens, "prompt usage mismatch")
    count = usage[1]
    if natural:
        require(1 <= count <= output_tokens and finish in ("stop", "length"), "invalid natural-EOS completion")
        require(finish != "length" or count == output_tokens, "premature length finish")
    else:
        require(count == output_tokens and finish == "length", "fixed output length contract failed")
    return {"text": "".join(pieces), "prompt_tokens": usage[0], "completion_tokens": count,
            "finish_reason": finish, "truncated": natural and finish == "length",
            "first_chunk_at": first, "completed_at": completed}


def planned_slots() -> list[dict]:
    slots = []
    for case in ("d1", "d2", "gsm"):
        profile = contract.PROFILES.get(case, {})
        if case == "d1":
            layout = [(phase, i, None, None) for phase in ("warmup", "formal")
                      for i in range(profile[phase + "_requests"])]
        elif case == "d2":
            layout = [("formal", turn * profile["sessions"] + session, session, turn)
                      for turn in range(profile["turns"]) for session in range(profile["sessions"])]
        else:
            layout = [("accuracy", i, None, None) for i in range(contract.GSM_COUNT)]
        for phase, index, session, turn in layout:
            slots.append({"slot_id": f"{case}-{phase}-{index:04d}", "case": case, "phase": phase,
                          "submission_index": index, "session_index": session, "turn": turn,
                          "record_id": None, "status": "not_started", "expected_answer": None,
                          "kind": "gsm" if case == "gsm" else "traffic",
                          "output_tokens": 1024 if case == "gsm" else profile["output_tokens"]})
    return slots


def bind_slots(slots: list[dict], dataset: Dataset) -> dict[str, dict]:
    records = {}
    for slot in slots:
        case, phase, index = slot["case"], slot["phase"], slot["submission_index"]
        if case == "d2":
            record = dataset.rows[case][slot["session_index"]]
            probe = next((p for p in record["math_probes"] if p["turn"] == slot["turn"]), None)
            slot.update(input_tokens=contract.D2_INPUT_LENGTHS[slot["turn"]],
                        expected_answer=probe["expected_answer"] if probe else None,
                        kind="math" if probe else "traffic",
                        source_refs=probe["source_refs"] if probe else record["source_refs"])
        else:
            offset = contract.PROFILES[case]["warmup_requests"] if phase == "formal" else 0
            record = dataset.rows[case][index + offset]
            slot.update(input_tokens=len(record["prompt_ids"]), expected_answer=record["expected_answer"],
                        kind=record.get("kind", "gsm"), source_refs=record["source_refs"])
        slot.update(record_id=record["id"], dataset_file_sha256=dataset.manifest["files"][case]["sha256"])
        records[slot["slot_id"]] = record
    return records


def run_pool(slots: list[dict], records: dict[str, dict], client: StreamingClient,
             concurrency: int, deadline: float, timeline: list[dict]) -> None:
    lock = threading.Lock()
    next_index = active = 0
    stopped = getattr(client, "cancellation", Cancellation())
    sealed = False

    def worker(worker_id: int) -> None:
        nonlocal next_index, active
        while True:
            with lock:
                if sealed or stopped.is_set() or next_index >= len(slots):
                    return
                slot = slots[next_index]
                next_index += 1
                if time.monotonic() >= deadline:
                    slot.update(status="failed", error="absolute phase/stage deadline exceeded")
                    stopped.set()
                    return
                active += 1
                slot.update(status="running", submitted_at=time.monotonic(), worker_id=worker_id,
                            active_concurrency_at_start=active)
                timeline.append({"at": slot["submitted_at"], "active": active})
            try:
                result = client.complete(slot, records[slot["slot_id"]], deadline)
            except BaseException as exc:
                result = {"status": "failed", "error": f"{type(exc).__name__}: {exc}",
                          "completed_at": time.monotonic()}
            if result["status"] != "ok":
                stopped.set()
            with lock:
                if not sealed:
                    slot.update(result)
                    active -= 1
                    timeline.append({"at": time.monotonic(), "active": active})

    workers = []
    try:
        for i in range(min(concurrency, len(slots))):
            thread = threading.Thread(target=worker, args=(i,), name=f"dpsk-pool-{i}", daemon=True)
            thread.start()
            workers.append(thread)
        while any(thread.is_alive() for thread in workers):
            if time.monotonic() >= deadline:
                stopped.set()
            if stopped.is_set():
                break
            for thread in workers:
                thread.join(CANCEL_POLL_SECONDS)
                if stopped.is_set():
                    break
    finally:
        if any(thread.is_alive() for thread in workers):
            stopped.set()
        if stopped.is_set() and hasattr(client, "abort"):
            client.abort()
        drain_deadline = time.monotonic() + WORKER_DRAIN_SECONDS
        for thread in workers:
            thread.join(max(0.0, drain_deadline - time.monotonic()))
        with lock:
            sealed = True
            for slot in slots:
                if slot["status"] == "running":
                    slot.update(status="interrupted", error="stage cancelled; worker drain ended")
    stopped.check()
    require(all(s["status"] == "ok" for s in slots), "traffic/absolute deadline failure in request pool")


def summarize(slots: list[dict], started: float) -> dict:
    successful = [s for s in slots if s["status"] == "ok"]
    completed = max((s.get("completed_at", started) for s in slots), default=started)
    duration = completed - started
    math_slots = [s for s in slots if s["kind"] == "math"]
    correct = sum(s.get("math_correct") is True for s in math_slots)
    return {"expected_requests": len(slots), "completed_requests": len(successful),
            "partial": len(successful) != len(slots),
            "formal_interval": {"started_at": started, "completed_at": completed, "duration": duration},
            "output_throughput": sum(s["completion_tokens"] for s in successful) / duration if duration > 0 else None,
            "p99_ttft": legacy.nearest_rank_p99([s["ttft"] for s in successful]) if successful else None,
            "p99_tpot": legacy.nearest_rank_p99([s["tpot"] for s in successful if s["tpot"] is not None])
            if any(s["tpot"] is not None for s in successful) else None,
            "math_total": len(math_slots), "math_correct": correct,
            "correctness_gate": {"required_math_requests": 3, "math_total": len(math_slots),
                                 "math_correct": correct, "passed": len(math_slots) == correct == 3}}


def score_stage(report: dict, dataset: Dataset, calibration: dict | None) -> dict:
    """Compute the local self-test score from measured gates and calibration."""
    reference = dataset.manifest["accuracy_reference"]
    gsm = [s for s in report["requests"] if s["case"] == "gsm"]
    completed = sum(s["status"] == "ok" for s in gsm)
    correct = sum(s.get("math_correct") is True for s in gsm)
    accuracy_passed = (reference["status"] == "validated" and completed == contract.GSM_COUNT
                       and correct * 20 > reference["correct"] * 19)
    report["accuracy"] = {"protocol": contract.GSM_PROTOCOL, "total": contract.GSM_COUNT,
                          "completed": completed, "correct": correct, "accuracy": correct / contract.GSM_COUNT,
                          "truncated": sum(s.get("truncated") is True for s in gsm),
                          "reference": reference, "passed": accuracy_passed,
                          "status": "calibration-pending" if reference["status"] != "validated" else
                          ("passed" if accuracy_passed else "failed")}
    pending = calibration is None or calibration["status"] != "validated" or reference["status"] != "validated"
    reasons = []
    if report["status"] != "ok" or any(s["status"] != "ok" for s in report["requests"]):
        reasons.append("traffic-failure")
    def math_passes(case: str) -> bool:
        probes = [s for s in report["requests"] if s["case"] == case and s["phase"] == "formal" and s["kind"] == "math"]
        summary = report["cases"].get(case, {}).get("summary", {})
        return (len(probes) == 3 and all(s.get("math_correct") is True and s["status"] == "ok" for s in probes)
                and summary.get("math_total") == summary.get("math_correct") == 3
                and summary.get("correctness_gate", {}).get("passed") is True)

    failed_math = [case for case in contract.PROFILES if not math_passes(case)]
    if pending:
        reasons.append("calibration-pending")
    if not dataset.manifest["tokenizer"]["gsm_template_verified"]:
        reasons.append("gsm-template-unverified")
    if reference["status"] == "validated" and not accuracy_passed:
        reasons.append("gsm-accuracy")
    cases = {}
    for case, profile in contract.PROFILES.items():
        case_reasons = list(reasons)
        if case in failed_math:
            case_reasons.append("math-accuracy")
        constants = calibration["cases"][case] if calibration is not None else None
        summary = report["cases"].get(case, {}).get("summary")
        progress = 0.0
        if calibration is not None and calibration["status"] == "validated" and summary and not summary["partial"]:
            ttft, tpot = constants["ttft_sla_seconds"], constants["tpot_sla_milliseconds"]
            formal = [s for s in report["requests"] if s["case"] == case and s["phase"] == "formal"]
            if any(s["ttft"] > 2 * ttft or s["tpot"] > 2 * tpot for s in formal):
                case_reasons.append("hard-sla")
            elif summary["p99_ttft"] > ttft or summary["p99_tpot"] > tpot:
                case_reasons.append("sla")
            if not case_reasons:
                progress = min(1.0, max(0.0, (summary["output_throughput"] - constants["p0_output_tps"]) /
                                        (constants["pf_output_tps"] - constants["p0_output_tps"])))
        cases[case] = {"max_score": profile["max_score"], "score": None if pending else
                       profile["max_score"] * progress ** 1.5, "normalized_progress": progress,
                       "passed_gates": not case_reasons, "reasons": case_reasons, "calibration": constants}
    score = None if pending else sum(c["score"] for c in cases.values())
    return {"max_score": 70, "score": score,
            "score_coordinate": None if score is None else 2 / (1 + score / 70),
            "calibration_pending": pending,
            "measurements_valid": "traffic-failure" not in reasons,
            "passed_gates": all(c["passed_gates"] for c in cases.values()),
            "reasons": reasons + (["math-accuracy"] if failed_math else []), "cases": cases}


def run_stage(*, dataset: Path | str = DEFAULT_DATASET, model: str,
              base_url: str = DEFAULT_BASE_URL, output: Path | str | None = None,
              calibration: Path | str | None = None, stage_start_monotonic: float | None = None,
              ignore_eos: bool = True) -> dict:
    """Run once with temporary main-thread signal handlers; preserve failed reports."""
    cancellation = Cancellation()
    with cancellation.signals():
        return _run_stage(dataset=dataset, model=model, base_url=base_url, output=output,
                          calibration=calibration, stage_start_monotonic=stage_start_monotonic,
                          ignore_eos=ignore_eos,
                          cancellation=cancellation)


def _run_stage(*, dataset, model, base_url, output, calibration, stage_start_monotonic,
                ignore_eos, cancellation: Cancellation) -> dict:
    """Run the local stage once; failures after output reservation yield a report."""
    entered = time.monotonic()
    start = entered if stage_start_monotonic is None else stage_start_monotonic
    require(type(start) in (int, float) and math.isfinite(start) and 0 <= start <= entered,
            "stage start must be a finite past timestamp from this host's monotonic clock")
    require(type(ignore_eos) is bool and text(model), "invalid model/ignore_eos")
    deadline = start + contract.TIMEOUTS_SECONDS["stage"]
    evidence = Evidence(output)
    report: dict = {"schema_version": contract.SCHEMA_VERSION, "protocol": contract.PROTOCOL, "status": "running",
                    "output": str(evidence.output), "evidence_directory": str(evidence.root),
                    "model": model, "base_url": base_url, "requests": planned_slots(),
                    "parameters": {"profiles": contract.PROFILES, "timeouts_seconds": contract.TIMEOUTS_SECONDS,
                                   "ignore_eos": ignore_eos, "gsm_max_tokens": 1024, "gsm_concurrency": 20},
                    "readiness": [], "cases": {}, "artifacts": {},
                    "lifecycle": {"stage_start_monotonic": start, "runner_entered_at": entered,
                                  "absolute_deadline": deadline,
                                   "startup_accounted_by_caller": stage_start_monotonic is not None},
                    "evaluation": {"score": None, "max_score": 70,
                                   "calibration_pending": True, "passed_gates": False}}
    loaded = None
    selected_calibration = None
    client = None
    try:
        evidence.checkpoint(report, "reserved")
        cancellation.check()
        remaining(deadline)
        calibration = resolve_calibration(dataset, calibration)
        report["artifacts"]["invocation"] = evidence.put("invocation.json", encoded(
            {"dataset": str(dataset), "model": model, "base_url": base_url,
             "calibration": str(calibration) if calibration is not None else None,
             "stage_start_monotonic": stage_start_monotonic, "ignore_eos": ignore_eos,
             "profiles": contract.PROFILES, "timeouts_seconds": contract.TIMEOUTS_SECONDS}))
        loaded = load_dataset(dataset, evidence, deadline, cancellation)
        report["dataset"] = {"manifest_sha256": loaded.manifest_sha256, "manifest": loaded.manifest,
                             "artifacts": loaded.artifacts}
        records = bind_slots(report["requests"], loaded)
        selected_calibration, artifact = load_calibration(calibration, loaded, evidence)
        report["artifacts"]["calibration"] = artifact
        cancellation.check()
        evidence.checkpoint(report, "dataset-bound")
        client = StreamingClient(base_url, model, evidence, ignore_eos, cancellation)
        client.readiness(deadline, report["readiness"])
        evidence.checkpoint(report, "ready")
        for case in ("d1", "d2", "gsm"):
            profile = contract.PROFILES.get(case, {})
            phases = ("warmup", "formal") if case == "d1" else (("formal",) if case == "d2" else ("accuracy",))
            report["cases"][case] = {}
            for phase in phases:
                cancellation.check()
                slots = [s for s in report["requests"] if s["case"] == case and s["phase"] == phase]
                evidence.checkpoint(report, f"{case}-{phase}-start")
                started = time.monotonic()
                phase_deadline = min(deadline, started + contract.TIMEOUTS_SECONDS["gsm_phase"]) if case == "gsm" else deadline
                phase_report = {"started_at": started, "absolute_deadline": phase_deadline,
                                "active_concurrency_timeline": []}
                report["cases"][case][phase] = phase_report
                try:
                    groups = [[s for s in slots if s["turn"] == turn] for turn in range(profile["turns"])] if case == "d2" else [slots]
                    for group in groups:
                        run_pool(group, records, client, 20 if case == "gsm" else profile["concurrency"],
                                 phase_deadline, phase_report["active_concurrency_timeline"])
                finally:
                    phase_report["completed_at"] = time.monotonic()
                    phase_report["duration"] = phase_report["completed_at"] - started
                    if phase == "formal":
                        report["cases"][case]["summary"] = summarize(slots, started)
                    evidence.checkpoint(report, f"{case}-{phase}-end")
        cancellation.check()
        remaining(deadline)
        report["status"] = "ok"
    except (Exception, KeyboardInterrupt) as exc:
        cancellation.set()
        report.update(status="failed", error=f"{type(exc).__name__}: {exc}")
    finally:
        if cancellation.is_set():
            report["status"] = "failed"
            if cancellation.signum is not None:
                report["error"] = f"stage cancelled ({signal.Signals(cancellation.signum).name})"
            if client is not None:
                client.abort()
        for slot in report["requests"]:
            if slot["status"] == "running":
                slot.update(status="interrupted", error="stage interrupted")
            elif slot["status"] == "not_started":
                slot["not_started_reason"] = "stage stopped before this slot"
        if loaded is not None:
            report["evaluation"] = score_stage(report, loaded, selected_calibration)
        report["lifecycle"]["completed_at"] = time.monotonic()
        report["lifecycle"]["elapsed_seconds"] = report["lifecycle"]["completed_at"] - start
        try:
            evidence.checkpoint(report, "final")
        finally:
            evidence.finish(report)
    return report


execute_stage = run_stage


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run unified DPSK Stage 2 once; calibration-pending still measures all workloads")
    parser.add_argument("--dataset", type=Path, default=DEFAULT_DATASET)
    parser.add_argument("--model", default=os.environ.get("HELLOHPC_MODEL_ID"))
    parser.add_argument("--base-url", default=os.environ.get("HELLOHPC_SERVICE_BASE_URL", DEFAULT_BASE_URL))
    parser.add_argument("--output", type=Path, help="new report file; default is a unique timestamp/UUID filename")
    parser.add_argument("--calibration", type=Path,
                        help="default: calibration.json beside --dataset; missing/pending means scores stay null")
    parser.add_argument("--stage-start-monotonic", type=float)
    parser.add_argument("--no-ignore-eos", action="store_true")
    args = parser.parse_args(argv)
    if not args.model:
        parser.error("--model or HELLOHPC_MODEL_ID is required")
    try:
        # Preserve startup-accounting truth: absent timestamp is not caller evidence.
        result = run_stage(dataset=args.dataset, model=args.model, base_url=args.base_url,
                           output=args.output, calibration=args.calibration,
                           stage_start_monotonic=args.stage_start_monotonic,
                           ignore_eos=not args.no_ignore_eos)
    except (DPSKError, OSError, ValueError) as exc:
        print(f"DPSK stage failed: {exc}", file=os.sys.stderr)
        return 2
    print(json.dumps({"output": result["output"], "status": result["status"],
                      "error": result.get("error"), "evaluation": result["evaluation"],
                      "summaries": {case: result["cases"].get(case, {}).get("summary") for case in contract.PROFILES},
                      "accuracy": result.get("accuracy")}, indent=2))
    return 0 if result["status"] == "ok" else 2


if __name__ == "__main__":
    raise SystemExit(main())
