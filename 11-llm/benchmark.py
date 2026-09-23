#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import math
import os
import re
import socket
import tempfile
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict, dataclass
from decimal import Decimal, InvalidOperation
from fractions import Fraction
from pathlib import Path
from typing import Callable, Sequence


class BenchmarkError(RuntimeError):
    pass


class DatasetError(BenchmarkError):
    pass


class TrafficError(BenchmarkError):
    def __init__(self, message: str) -> None:
        super().__init__(message)
        self.partial_result: dict[str, object] = {}
        self.failed_phase: str | None = None
        self.failed_submission_index: int | None = None
        self.expected_formal_requests: int | None = None


@dataclass(frozen=True)
class DatasetRecord:
    record_index: int
    case: str
    kind: str
    prompt: str
    expected_answer: str | None
    component_source_ids: tuple[str, ...]
    builder_prompt_tokens: int
    prompt_token_ids: tuple[int, ...] | None


@dataclass(frozen=True)
class Observation:
    record_index: int
    component_source_ids: tuple[str, ...]
    kind: str
    expected_answer: str | None
    builder_prompt_tokens: int
    text: str
    prompt_tokens: int
    completion_tokens: int
    finish_reason: str
    batch_started: float
    request_started: float
    first_chunk_at: float
    completed_at: float

    @property
    def ttft(self) -> float:
        return self.first_chunk_at - self.request_started

    @property
    def tpot(self) -> float:
        return 1000.0 * (self.completed_at - self.first_chunk_at) / (self.completion_tokens - 1)


RECORD_FIELDS = {
    "schema_version",
    "case",
    "kind",
    "prompt",
    "expected_answer",
    "components",
    "builder_prompt_tokens",
    "prompt_token_ids",
}
COMPONENT_FIELDS = {"role", "source", "source_id"}
FINAL_PATTERN = re.compile(
    r"(?im)^[ \t]*FINAL:[ \t]*([+-]?(?:\d+[ \t]*/[ \t]*[+-]?\d+|(?:\d+(?:\.\d*)?|\.\d+)))[ \t]*\r?$"
)
NUMBER_PATTERN = re.compile(r"^[+-]?(?:\d+\s*/\s*[+-]?\d+|(?:\d+(?:\.\d*)?|\.\d+))$")
MAX_READINESS_RESPONSE_BYTES = 1024 * 1024
MAX_SSE_LINE_BYTES = 1024 * 1024
MAX_SSE_EVENTS = 65536
MAX_ACCUMULATED_TEXT_CHARS = 16 * 1024 * 1024
CLI_PROFILES = {
    "qwen-performance": {
        "concurrency": 1,
        "max_tokens": 1024,
        "warmup_requests": 1,
        "formal_requests": 5,
        "formal_math_requests": 1,
    },
}
DEFAULT_READINESS_TIMEOUT = 600.0
DEFAULT_REQUEST_TIMEOUTS = {"qwen-performance": 300.0}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _resolve_cli_timeouts(
    profile: str,
    readiness_timeout: float,
    request_timeout: float | None,
) -> tuple[float, float]:
    selected_request_timeout = DEFAULT_REQUEST_TIMEOUTS[profile] if request_timeout is None else request_timeout
    if not math.isfinite(readiness_timeout) or readiness_timeout <= 0:
        raise ValueError("--readiness-timeout must be finite and positive")
    if not math.isfinite(selected_request_timeout) or selected_request_timeout <= 0:
        raise ValueError("--request-timeout must be finite and positive")
    return readiness_timeout, selected_request_timeout


def _exact_number(value: str) -> Fraction | None:
    stripped = value.strip()
    if not NUMBER_PATTERN.fullmatch(stripped):
        return None
    try:
        if "/" in stripped:
            numerator, denominator = (part.strip() for part in stripped.split("/", 1))
            return Fraction(int(numerator), int(denominator))
        return Fraction(Decimal(stripped))
    except (InvalidOperation, ValueError, ZeroDivisionError):
        return None


def extract_final_answer(text: str) -> str | None:
    match = FINAL_PATTERN.search(text)
    return match.group(1).replace(" ", "").replace("\t", "") if match else None


def exact_math_match(text: str, expected_answer: str) -> bool:
    extracted = extract_final_answer(text)
    if extracted is None:
        return False
    expected = extract_final_answer(expected_answer) or expected_answer.strip()
    actual_value = _exact_number(extracted)
    expected_value = _exact_number(expected)
    return actual_value is not None and expected_value is not None and actual_value == expected_value


def load_dataset(path: Path, case: str) -> tuple[DatasetRecord, ...]:
    records: list[DatasetRecord] = []
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as exc:
        raise DatasetError(f"cannot read dataset: {path}") from exc
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except json.JSONDecodeError as exc:
            raise DatasetError(f"dataset line {line_number} is invalid JSON") from exc
        if isinstance(value, dict) and value.get("case") == case:
            record_index = len(records)
            record_label = f"dataset record index {record_index} for case {case}"
        else:
            record_label = None
        if not isinstance(value, dict) or set(value) != RECORD_FIELDS or value["schema_version"] != 3:
            if record_label is not None:
                raise DatasetError(f"{record_label} has invalid fields")
            raise DatasetError(f"dataset line {line_number} has invalid fields")
        if value["case"] != case:
            continue
        assert record_label is not None
        string_fields = ("prompt",)
        if any(not isinstance(value[field], str) or not value[field] for field in string_fields):
            raise DatasetError(f"{record_label} has invalid text fields")
        if value["kind"] not in {"traffic", "math"}:
            raise DatasetError(f"{record_label} has invalid kind")
        expected = value["expected_answer"]
        builder_prompt_tokens = value["builder_prompt_tokens"]
        if isinstance(builder_prompt_tokens, bool) or not isinstance(builder_prompt_tokens, int) or builder_prompt_tokens < 1:
            raise DatasetError(f"{record_label} has invalid builder prompt token count")
        prompt_token_ids = value["prompt_token_ids"]
        if prompt_token_ids is not None:
            if (
                not isinstance(prompt_token_ids, list)
                or not prompt_token_ids
                or any(isinstance(token_id, bool) or not isinstance(token_id, int) or token_id < 0 for token_id in prompt_token_ids)
                or prompt_token_ids[0] != 0
                or len(prompt_token_ids) != builder_prompt_tokens
                or value["kind"] != "math"
                or value["case"] not in {"d1", "d2"}
            ):
                raise DatasetError(f"{record_label} has invalid prompt token IDs")
            if value["case"] == "d1" and len(prompt_token_ids) != 8192:
                raise DatasetError(f"{record_label} does not have exactly 8192 D1 prompt token IDs")
            if value["case"] == "d2" and not 80 <= len(prompt_token_ids) <= 176:
                raise DatasetError(f"{record_label} has D2 prompt token IDs outside [80, 176]")
        elif value["case"] in {"d1", "d2"} and value["kind"] == "math":
            raise DatasetError(f"{record_label} is missing prompt token IDs")
        if value["kind"] == "math":
            if not isinstance(expected, str) or _exact_number(extract_final_answer(expected) or expected.strip()) is None:
                raise DatasetError(f"{record_label} has invalid exact answer")
        elif expected is not None:
            raise DatasetError(f"{record_label} gives an answer for non-math traffic")
        components = value["components"]
        if not isinstance(components, list) or not components:
            raise DatasetError(f"{record_label} has no provenance components")
        component_source_ids: list[str] = []
        for component in components:
            if not isinstance(component, dict) or set(component) != COMPONENT_FIELDS:
                raise DatasetError(f"{record_label} has invalid provenance fields")
            if any(not isinstance(component[field], str) or not component[field] for field in COMPONENT_FIELDS):
                raise DatasetError(f"{record_label} has invalid provenance values")
            component_source_ids.append(component["source_id"])
        if len(component_source_ids) != len(set(component_source_ids)):
            raise DatasetError(f"{record_label} repeats a provenance component")
        records.append(
            DatasetRecord(
                record_index=record_index,
                case=value["case"],
                kind=value["kind"],
                prompt=value["prompt"],
                expected_answer=expected,
                component_source_ids=tuple(component_source_ids),
                builder_prompt_tokens=builder_prompt_tokens,
                prompt_token_ids=tuple(prompt_token_ids) if prompt_token_ids is not None else None,
            )
        )
    if not records:
        raise DatasetError(f"dataset has no records for case {case}")
    return tuple(records)


class OpenAIStreamingClient:
    def __init__(
        self,
        base_url: str,
        *,
        opener=urllib.request.urlopen,
        clock: Callable[[], float] = time.perf_counter,
    ) -> None:
        self.base_url = base_url.rstrip("/")
        self.opener = opener
        self.clock = clock

    def advertised_models(self, timeout: float) -> set[str]:
        request = urllib.request.Request(
            f"{self.base_url}/v1/models",
            method="GET",
            headers={"Accept": "application/json"},
        )
        try:
            with self.opener(request, timeout=timeout) as response:
                payload = response.read(MAX_READINESS_RESPONSE_BYTES + 1)
                if len(payload) > MAX_READINESS_RESPONSE_BYTES:
                    raise TrafficError("model readiness response is too large")
                value = json.loads(payload)
        except TrafficError:
            raise
        except (OSError, urllib.error.URLError, urllib.error.HTTPError, UnicodeError, json.JSONDecodeError, TypeError) as exc:
            raise TrafficError("model readiness request failed") from exc
        if not isinstance(value, dict) or not isinstance(value.get("data"), list):
            raise TrafficError("model readiness response has invalid fields")
        return {
            item["id"]
            for item in value["data"]
            if isinstance(item, dict) and isinstance(item.get("id"), str)
        }

    def complete(
        self,
        record: DatasetRecord,
        *,
        model: str,
        max_tokens: int,
        timeout: float,
        batch_started: float,
    ) -> Observation:
        if timeout <= 0:
            raise TrafficError("streaming request timeout must be positive")
        body = json.dumps(
            {
                "model": model,
                "prompt": record.prompt_token_ids if record.prompt_token_ids is not None else record.prompt,
                "max_tokens": max_tokens,
                "min_tokens": max_tokens,
                "temperature": 0,
                "stream": True,
                "stream_options": {"include_usage": True},
            },
            ensure_ascii=False,
            allow_nan=False,
            separators=(",", ":"),
        ).encode("utf-8")
        request = urllib.request.Request(
            f"{self.base_url}/v1/completions",
            data=body,
            method="POST",
            headers={"Accept": "text/event-stream", "Content-Type": "application/json"},
        )
        request_started = self.clock()
        deadline = request_started + timeout
        real_deadline = time.monotonic() + timeout
        finished = threading.Event()
        timed_out = threading.Event()
        state_lock = threading.Lock()
        state: dict[str, object] = {}

        def perform() -> None:
            try:
                with self.opener(request, timeout=timeout) as response:
                    with state_lock:
                        state["response"] = response
                    if timed_out.is_set():
                        return
                    if response.headers.get_content_type() != "text/event-stream":
                        raise TrafficError("completion response is not text/event-stream")
                    observation = self._consume_sse(
                        response,
                        record,
                        max_tokens=max_tokens,
                        batch_started=batch_started,
                        request_started=request_started,
                        deadline=deadline,
                    )
                    with state_lock:
                        state["observation"] = observation
            except TrafficError as exc:
                with state_lock:
                    state["traffic_error"] = exc
            except (
                OSError,
                http.client.HTTPException,
                urllib.error.URLError,
                urllib.error.HTTPError,
                UnicodeError,
                TypeError,
                ValueError,
            ) as exc:
                with state_lock:
                    state["stream_error"] = exc
            finally:
                finished.set()

        worker = threading.Thread(target=perform, name="benchmark-http-request", daemon=True)
        worker.start()
        remaining = max(0.0, real_deadline - time.monotonic())
        if not finished.wait(remaining):
            timed_out.set()
            with state_lock:
                response = state.get("response")
            if response is not None:
                self._interrupt_response(response)
            raise TrafficError(f"streaming request for record index {record.record_index} exceeded its timeout")
        with state_lock:
            traffic_error = state.get("traffic_error")
            stream_error = state.get("stream_error")
            observation = state.get("observation")
        if isinstance(traffic_error, TrafficError):
            raise TrafficError(f"streaming request failed for record index {record.record_index}: {traffic_error}") from traffic_error
        if isinstance(stream_error, BaseException):
            raise TrafficError(f"streaming request failed for record index {record.record_index}") from stream_error
        if not isinstance(observation, Observation):
            raise TrafficError(f"streaming request failed for record index {record.record_index}")
        return observation

    @staticmethod
    def _interrupt_response(response: object) -> None:
        pending = [response]
        seen: set[int] = set()
        while pending and len(seen) < 16:
            value = pending.pop()
            identity = id(value)
            if identity in seen:
                continue
            seen.add(identity)
            if isinstance(value, socket.socket):
                try:
                    value.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    value.close()
                except OSError:
                    pass
                return
            for attribute in ("_sock", "sock", "raw", "fp", "_fp"):
                try:
                    nested = getattr(value, attribute, None)
                except Exception:
                    nested = None
                if nested is not None:
                    pending.append(nested)

    def _consume_sse(
        self,
        response: object,
        record: DatasetRecord,
        *,
        max_tokens: int,
        batch_started: float,
        request_started: float,
        deadline: float,
    ) -> Observation:
        text_parts: list[str] = []
        first_chunk_at: float | None = None
        prompt_tokens: int | None = None
        completion_tokens: int | None = None
        finish_reason: str | None = None
        saw_done = False
        event_count = 0
        accumulated_text_chars = 0
        while True:
            if self.clock() > deadline:
                raise TrafficError("streaming request exceeded its timeout")
            raw_line = response.readline(MAX_SSE_LINE_BYTES + 1)
            observed_at = self.clock()
            if observed_at > deadline:
                raise TrafficError("streaming request exceeded its timeout")
            if not raw_line:
                break
            if not isinstance(raw_line, bytes):
                raise TrafficError("stream yielded non-byte content")
            if len(raw_line) > MAX_SSE_LINE_BYTES:
                raise TrafficError("stream event line is too large")
            line = raw_line.decode("utf-8").strip()
            if not line or line.startswith(":") or not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                saw_done = True
                break
            event_count += 1
            if event_count > MAX_SSE_EVENTS:
                raise TrafficError("stream produced too many events")
            try:
                event = json.loads(data)
            except json.JSONDecodeError as exc:
                raise TrafficError("stream event is invalid JSON") from exc
            if not isinstance(event, dict):
                raise TrafficError("stream event must be an object")
            choices = event.get("choices", [])
            if not isinstance(choices, list):
                raise TrafficError("stream choices must be an array")
            if choices:
                choice = choices[0]
                if not isinstance(choice, dict):
                    raise TrafficError("stream choice must be an object")
                chunk = choice.get("text", "")
                if not isinstance(chunk, str):
                    raise TrafficError("stream text must be a string")
                if chunk:
                    try:
                        chunk.encode("utf-8")
                    except UnicodeEncodeError as exc:
                        raise TrafficError("stream text is not valid UTF-8") from exc
                    accumulated_text_chars += len(chunk)
                    if accumulated_text_chars > MAX_ACCUMULATED_TEXT_CHARS:
                        raise TrafficError("stream text is too large")
                    if first_chunk_at is None:
                        first_chunk_at = observed_at
                    text_parts.append(chunk)
                reason = choice.get("finish_reason")
                if reason is not None:
                    if not isinstance(reason, str):
                        raise TrafficError("finish_reason must be a string or null")
                    finish_reason = reason
            usage = event.get("usage")
            if usage is not None:
                if not isinstance(usage, dict):
                    raise TrafficError("stream usage must be an object")
                prompt_tokens = usage.get("prompt_tokens")
                completion_tokens = usage.get("completion_tokens")
        completed_at = self.clock()
        if not saw_done:
            raise TrafficError("stream ended without [DONE]")
        if first_chunk_at is None:
            raise TrafficError("stream produced no nonempty text")
        if isinstance(prompt_tokens, bool) or not isinstance(prompt_tokens, int) or prompt_tokens < 0:
            raise TrafficError("stream did not report prompt token usage")
        if isinstance(completion_tokens, bool) or not isinstance(completion_tokens, int):
            raise TrafficError("stream did not report completion token usage")
        if completion_tokens != max_tokens:
            raise TrafficError("stream completion token usage does not match requested output length")
        if finish_reason != "length":
            raise TrafficError("stream did not finish at the requested output length")
        return Observation(
            record_index=record.record_index,
            component_source_ids=record.component_source_ids,
            kind=record.kind,
            expected_answer=record.expected_answer,
            builder_prompt_tokens=record.builder_prompt_tokens,
            text="".join(text_parts),
            prompt_tokens=prompt_tokens,
            completion_tokens=completion_tokens,
            finish_reason=finish_reason,
            batch_started=batch_started,
            request_started=request_started,
            first_chunk_at=first_chunk_at,
            completed_at=completed_at,
        )


def wait_for_model(
    client: OpenAIStreamingClient,
    model: str,
    *,
    timeout: float,
    poll_interval: float = 1.0,
) -> None:
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TrafficError("service readiness timed out")
        try:
            if model in client.advertised_models(min(remaining, 5.0)):
                return
        except TrafficError:
            pass
        time.sleep(min(poll_interval, max(deadline - time.monotonic(), 0.0)))


def nearest_rank_p99(values: Sequence[float]) -> float:
    if not values:
        raise ValueError("p99 requires at least one value")
    return sorted(values)[math.ceil(0.99 * len(values)) - 1]


def _request_log(
    observation: Observation,
    *,
    submission_index: int,
    worker_id: int,
    submitted_at: float,
    active_concurrency_at_start: int,
) -> dict[str, object]:
    value = asdict(observation)
    value["submission_index"] = submission_index
    value["worker_id"] = worker_id
    value["submitted_at"] = submitted_at
    value["active_concurrency_at_start"] = active_concurrency_at_start
    value["ttft"] = observation.ttft
    value["tpot"] = observation.tpot
    if observation.kind == "math":
        assert observation.expected_answer is not None
        value["extracted_answer"] = extract_final_answer(observation.text)
        value["math_correct"] = exact_math_match(observation.text, observation.expected_answer)
    else:
        value["extracted_answer"] = None
        value["math_correct"] = None
    return value


def _phase_report(
    phase: str,
    *,
    barrier_released_at: float,
    requests: Sequence[dict[str, object]],
    max_active_concurrency: int,
    active_concurrency_timeline: Sequence[dict[str, object]],
    expected_requests: int,
) -> dict[str, object]:
    ordered = sorted(requests, key=lambda item: int(item["submission_index"]))
    completed_at = max((float(item["completed_at"]) for item in ordered), default=barrier_released_at)
    return {
        "phase": phase,
        "barrier_released_at": barrier_released_at,
        "completed_at": completed_at,
        "duration": completed_at - barrier_released_at,
        "request_count": len(ordered),
        "expected_requests": expected_requests,
        "max_active_concurrency": max_active_concurrency,
        "active_concurrency_timeline": list(active_concurrency_timeline),
        "requests": ordered,
    }


def run_request_pool(
    client: OpenAIStreamingClient,
    records: Sequence[DatasetRecord],
    *,
    phase: str,
    concurrency: int,
    model: str,
    max_tokens: int,
    timeout: float,
    clock: Callable[[], float] = time.perf_counter,
) -> dict[str, object]:
    if not records:
        raise BenchmarkError(f"{phase} request pool is empty")
    worker_count = min(concurrency, len(records))
    timing: dict[str, float] = {}
    barrier = threading.Barrier(
        worker_count + 1,
        action=lambda: timing.setdefault("barrier_released_at", clock()),
    )
    lock = threading.Lock()
    stop = threading.Event()
    next_submission_index = worker_count
    active = 0
    max_active = 0
    completed_requests: list[tuple[Observation, int, int, float, int]] = []
    active_timeline: list[dict[str, object]] = []
    failures: list[tuple[int, TrafficError]] = []

    def invoke(worker_id: int) -> None:
        nonlocal active, max_active, next_submission_index
        submission_index = worker_id
        barrier.wait()
        while submission_index < len(records) and not stop.is_set():
            submitted_at = clock()
            with lock:
                active += 1
                max_active = max(max_active, active)
                active_at_start = active
                active_timeline.append({"at": submitted_at, "active": active})
            try:
                observation = client.complete(
                    records[submission_index],
                    model=model,
                    max_tokens=max_tokens,
                    timeout=timeout,
                    batch_started=timing["barrier_released_at"],
                )
            except TrafficError as exc:
                with lock:
                    failures.append((submission_index, exc))
                stop.set()
                return
            finally:
                completed_for_timeline = clock()
                with lock:
                    active -= 1
                    active_timeline.append({"at": completed_for_timeline, "active": active})
            with lock:
                completed_requests.append((observation, submission_index, worker_id, submitted_at, active_at_start))
                if stop.is_set() or next_submission_index >= len(records):
                    return
                submission_index = next_submission_index
                next_submission_index += 1

    with ThreadPoolExecutor(max_workers=worker_count, thread_name_prefix=f"benchmark-{phase}") as executor:
        futures = [executor.submit(invoke, worker_id) for worker_id in range(worker_count)]
        barrier.wait()
        for future in futures:
            future.result()
    request_logs = [
        _request_log(
            observation,
            submission_index=submission_index,
            worker_id=worker_id,
            submitted_at=submitted_at,
            active_concurrency_at_start=active_at_start,
        )
        for observation, submission_index, worker_id, submitted_at, active_at_start in completed_requests
    ]
    report = _phase_report(
        phase,
        barrier_released_at=timing["barrier_released_at"],
        requests=request_logs,
        max_active_concurrency=max_active,
        active_concurrency_timeline=active_timeline,
        expected_requests=len(records),
    )
    if failures:
        failed_submission_index, error = min(failures, key=lambda item: item[0])
        error.partial_result = {phase: report}
        error.failed_phase = phase
        error.failed_submission_index = failed_submission_index
        raise error
    if len(request_logs) != len(records):
        raise TrafficError(f"{phase} request pool stopped before queue exhaustion")
    return report


def summarize_formal_phase(formal: dict[str, object]) -> dict[str, object]:
    requests = formal.get("requests")
    if not isinstance(requests, list) or not requests:
        raise TrafficError("formal request report is empty")
    duration = float(formal["duration"])
    if duration <= 0:
        raise TrafficError("formal interval duration is invalid")
    ttft_samples = [float(item["ttft"]) for item in requests]
    tpot_samples = [float(item["tpot"]) for item in requests]
    throughput = sum(int(item["completion_tokens"]) for item in requests) / duration
    math_results = [bool(item["math_correct"]) for item in requests if item["kind"] == "math"]
    math_correct = sum(math_results)
    return {
        "output_throughput": throughput,
        "p99_ttft": nearest_rank_p99(ttft_samples),
        "p99_tpot": nearest_rank_p99(tpot_samples),
        "throughput_samples": [throughput],
        "ttft_samples": ttft_samples,
        "tpot_samples": tpot_samples,
        "math_total": len(math_results),
        "math_correct": math_correct,
        "math_accuracy": math_correct / len(math_results) if math_results else None,
        "formal_interval": {
            "started_at": formal["barrier_released_at"],
            "completed_at": formal["completed_at"],
            "duration": duration,
        },
    }


def add_correctness_gate(
    summary: dict[str, object],
    *,
    required_math_requests: int,
) -> dict[str, object]:
    """Attach the all-or-nothing formal math gate to a benchmark summary."""
    math_total = int(summary["math_total"])
    math_correct = int(summary["math_correct"])
    passed = math_total == required_math_requests and math_correct == required_math_requests
    summary["correctness_gate"] = {
        "policy": "all-formal-math-probes-must-pass",
        "required_math_requests": required_math_requests,
        "math_total": math_total,
        "math_correct": math_correct,
        "passed": passed,
    }
    return summary


def run_benchmark(
    *,
    dataset: Path,
    case: str,
    base_url: str,
    model: str,
    concurrency: int,
    max_tokens: int,
    warmup_requests: int,
    formal_requests: int,
    formal_math_requests: int,
    readiness_timeout: float,
    request_timeout: float,
) -> dict[str, object]:
    if (
        concurrency < 1
        or max_tokens < 2
        or warmup_requests < 0
        or formal_requests < 1
        or formal_math_requests < 0
        or formal_math_requests > formal_requests
    ):
        raise BenchmarkError("benchmark counts are invalid")
    records = load_dataset(dataset, case)
    if len(records) != warmup_requests + formal_requests:
        raise DatasetError(
            f"case {case} needs exactly {warmup_requests + formal_requests} records in workload order"
        )
    warmup_records = tuple(records[:warmup_requests])
    formal_records = tuple(records[warmup_requests:])
    if any(record.kind != "traffic" for record in warmup_records):
        raise DatasetError(f"case {case} warmup must contain only traffic records")
    if sum(record.kind == "math" for record in formal_records) != formal_math_requests:
        raise DatasetError(f"case {case} formal record order has an invalid math count")
    client = OpenAIStreamingClient(base_url)
    wait_for_model(client, model, timeout=readiness_timeout)
    warmup: dict[str, object] | None = None
    if warmup_records:
        try:
            warmup = run_request_pool(
                client,
                warmup_records,
                phase="warmup",
                concurrency=concurrency,
                model=model,
                max_tokens=max_tokens,
                timeout=request_timeout,
            )
        except TrafficError as exc:
            exc.expected_formal_requests = formal_requests
            raise
    try:
        formal = run_request_pool(
            client,
            formal_records,
            phase="formal",
            concurrency=concurrency,
            model=model,
            max_tokens=max_tokens,
            timeout=request_timeout,
        )
    except TrafficError as exc:
        partial_formal = exc.partial_result.get("formal")
        exc.partial_result = {"warmup": warmup, "formal": partial_formal}
        exc.expected_formal_requests = formal_requests
        raise
    summary = add_correctness_gate(
        summarize_formal_phase(formal),
        required_math_requests=formal_math_requests,
    )
    return {
        "schema_version": 3,
        "status": "ok",
        "case": case,
        "model": model,
        "dataset_sha256": sha256_file(dataset),
        "parameters": {
            "concurrency": concurrency,
            "max_tokens": max_tokens,
            "warmup_requests": warmup_requests,
            "formal_requests": formal_requests,
            "formal_math_requests": formal_math_requests,
        },
        "summary": summary,
        "warmup": warmup,
        "formal": formal,
    }


def write_json(path: Path, value: dict[str, object]) -> None:
    data = (json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
    path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def execute(output: Path, **kwargs: object) -> dict[str, object]:
    try:
        result = run_benchmark(**kwargs)
    except BenchmarkError as exc:
        failure: dict[str, object] = {"schema_version": 3, "status": "failed", "error": str(exc)}
        if isinstance(exc, TrafficError):
            failure.update(exc.partial_result)
            failure["failed_phase"] = exc.failed_phase
            failure["failed_submission_index"] = exc.failed_submission_index
            failure["expected_formal_requests"] = exc.expected_formal_requests
        write_json(output, failure)
        raise
    write_json(output, result)
    return result


def main(argv: Sequence[str] | None = None) -> int:
    dispatch_argv = list(os.sys.argv[1:] if argv is None else argv)
    if dispatch_argv and dispatch_argv[0] == "dpsk-stage2":
        import dpsk_benchmark
        return dpsk_benchmark.main(dispatch_argv[1:])
    parser = argparse.ArgumentParser(description="Run a built-in public serving benchmark profile")
    parser.add_argument("profile", choices=tuple(CLI_PROFILES))
    parser.add_argument("--dataset", type=Path, default=Path("data/public-dataset.jsonl"))
    parser.add_argument(
        "--base-url",
        default=os.environ.get("HELLOHPC_SERVICE_BASE_URL", "http://127.0.0.1:8000"),
    )
    parser.add_argument("--model", default=os.environ.get("HELLOHPC_MODEL_ID"))
    parser.add_argument("--readiness-timeout", type=float, default=DEFAULT_READINESS_TIMEOUT)
    parser.add_argument("--request-timeout", type=float)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args(argv)
    if not arguments.model:
        parser.error("--model or HELLOHPC_MODEL_ID is required")
    try:
        readiness_timeout, request_timeout = _resolve_cli_timeouts(
            arguments.profile,
            arguments.readiness_timeout,
            arguments.request_timeout,
        )
    except ValueError as exc:
        parser.error(str(exc))
    profile = CLI_PROFILES[arguments.profile]
    output = arguments.output or Path(f"benchmark-{arguments.profile}-result.json")
    try:
        result = execute(
            output,
            dataset=arguments.dataset,
            case=arguments.profile,
            base_url=arguments.base_url,
            model=arguments.model,
            concurrency=profile["concurrency"],
            max_tokens=profile["max_tokens"],
            warmup_requests=profile["warmup_requests"],
            formal_requests=profile["formal_requests"],
            formal_math_requests=profile["formal_math_requests"],
            readiness_timeout=readiness_timeout,
            request_timeout=request_timeout,
        )
    except BenchmarkError as exc:
        print(f"benchmark failed: {exc}", file=os.sys.stderr)
        return 2
    print(
        json.dumps(
            {"output": str(output), "profile": arguments.profile, "summary": result["summary"]},
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
