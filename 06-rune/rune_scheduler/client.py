from __future__ import annotations

import asyncio
import concurrent.futures
import http.client
import json
import queue
import socket
import threading
from collections.abc import Callable, Mapping
from urllib.parse import urlparse

from . import wire
from .api.v1 import PublicScenarioModel
from .codec import (
    FORMAT_JSON,
    FORMAT_POSTCARD,
    FrameError,
    decode_welcome,
    read_frame_sync,
    write_frame_sync,
)
from .models import (
    DispatchBatch,
    StepResult,
    batch_to_json,
    model_from_json,
    step_result_from_json,
)


class ClusterClientError(RuntimeError):
    pass


class ClusterClient:
    """Cluster transport.

    Two modes:

    - **UDS binary frames** (``uds_path`` set): length-prefixed postcard/JSON
      frames over a Unix domain socket. Lowest per-step latency. All socket
      I/O runs on one dedicated IO thread; each call submits a job and awaits
      the result future (one thread handoff per round trip).
    - **HTTP keep-alive** (fallback): single ``http.client`` connection,
      matching the scoring evaluator's protocol.
    """

    def __init__(self, base_url: str, token: str, uds_path: str | None = None) -> None:
        self._base_url = base_url.removesuffix("/")
        self._token = token
        self._uds_path = uds_path
        parsed = urlparse(self._base_url)
        self._host = parsed.hostname or "127.0.0.1"
        self._port = parsed.port or (443 if parsed.scheme == "https" else 80)
        self._loop = asyncio.get_running_loop()
        self._conn: http.client.HTTPConnection | None = None
        self._sock: socket.socket | None = None
        self._format = FORMAT_POSTCARD if uds_path is not None else FORMAT_JSON
        if uds_path is not None:
            # Dedicated IO thread: avoids ThreadPoolExecutor scheduling overhead
            # and keeps socket affinity (a fresh UDS handshake per call would
            # otherwise be required on any retry).
            self._jobs: queue.SimpleQueue[
                tuple[Callable[[], object], concurrent.futures.Future] | None
            ] = queue.SimpleQueue()
            self._io_thread = threading.Thread(
                target=self._io_loop, name="rune-cluster-io", daemon=True
            )
            self._io_thread.start()
        else:
            self._executor = concurrent.futures.ThreadPoolExecutor(
                max_workers=1, thread_name_prefix="rune-cluster"
            )

    async def model(self) -> PublicScenarioModel:
        if self._uds_path is not None:
            response = await self._uds_request(
                wire.encode_request_model, model_from_json
            )
            assert isinstance(response, PublicScenarioModel)
            return response
        return model_from_json(await self._json("GET", "/v1/model"))

    async def state(self) -> StepResult:
        if self._uds_path is not None:
            response = await self._uds_request(
                wire.encode_request_state, step_result_from_json
            )
            assert isinstance(response, StepResult)
            return response
        return step_result_from_json(await self._json("GET", "/v1/state"))

    async def _uds_request(
        self,
        encode: Callable[[int], bytes],
        convert: Callable[[Mapping[str, object]], object],
    ) -> object:
        last_error: Exception | None = None
        for _ in range(3):
            try:
                response = await self._round_trip(encode(self._format))
            except (FrameError, OSError, ClusterClientError) as error:
                last_error = error
                await self._reconnect()
                continue
            if isinstance(response, PublicScenarioModel | StepResult):
                return response
            return convert(_response_body(response))
        assert last_error is not None
        raise last_error

    async def step(self, batch: DispatchBatch) -> StepResult:
        if self._uds_path is not None:
            last_error: Exception | None = None
            for _ in range(3):
                try:
                    encoded = wire.encode_request_step(
                        batch.decision_id,
                        str(batch.action),
                        [{"task_id": t, "worker_id": w} for t, w in batch.placements],
                        self._format,
                    )
                    response = await self._round_trip(encoded)
                    if isinstance(response, StepResult):
                        return response
                    return step_result_from_json(_response_body(response))
                except (FrameError, OSError, ClusterClientError) as error:
                    last_error = error
                    await self._reconnect()
            assert last_error is not None
            raise last_error
        payload = batch_to_json(batch)
        last_error = None
        for _ in range(3):
            try:
                return step_result_from_json(
                    await self._json("POST", "/v1/step", payload)
                )
            except (http.client.HTTPException, OSError) as error:
                last_error = error
                await self._reconnect()
        assert last_error is not None
        raise last_error

    async def close(self) -> None:
        try:
            if self._uds_path is not None:
                await self._submit(self._close_sync)
            else:
                await self._loop.run_in_executor(self._executor, self._close_sync)
        except (FrameError, OSError, ClusterClientError):
            pass
        if self._uds_path is None:
            self._executor.shutdown(wait=False)

    async def _reconnect(self) -> None:
        try:
            if self._uds_path is not None:
                await self._submit(self._close_sync)
            else:
                await self._loop.run_in_executor(self._executor, self._close_sync)
        except (FrameError, OSError, ClusterClientError):
            pass

    def _close_sync(self) -> None:
        if self._sock is not None:
            self._sock.close()
            self._sock = None
        if self._conn is not None:
            self._conn.close()
            self._conn = None

    def _connection(self) -> http.client.HTTPConnection:
        if self._conn is None:
            self._conn = http.client.HTTPConnection(self._host, self._port, timeout=30)
        return self._conn

    def _io_loop(self) -> None:
        while True:
            job = self._jobs.get()
            if job is None:
                return
            fn, future = job
            try:
                result = fn()
            except BaseException as error:  # noqa: BLE001 - propagate to future
                if future.set_running_or_notify_cancel():
                    future.set_exception(error)
            else:
                if future.set_running_or_notify_cancel():
                    future.set_result(result)

    # -- UDS frame transport -------------------------------------------------

    def _uds_socket(self) -> socket.socket:
        if self._sock is None:
            assert self._uds_path is not None
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(30)
            try:
                sock.connect(self._uds_path)
                write_frame_sync(sock, wire.encode_hello(self._token, self._format))
                welcome = decode_welcome(read_frame_sync(sock))
            except (OSError, FrameError) as error:
                sock.close()
                raise ClusterClientError(f"uds connect failed: {error}") from error
            if not isinstance(welcome, Mapping) or not welcome.get("ok"):
                sock.close()
                raise ClusterClientError("uds handshake rejected")
            self._sock = sock
        return self._sock

    async def _submit(self, fn: Callable[[], object]) -> object:
        future: concurrent.futures.Future = concurrent.futures.Future()
        self._jobs.put((fn, future))
        return await asyncio.wrap_future(future)

    async def _round_trip(self, payload: bytes):
        return await self._submit(lambda: self._round_trip_sync(payload))

    def _round_trip_sync(self, payload: bytes):
        sock = self._uds_socket()
        try:
            write_frame_sync(sock, payload)
            raw = read_frame_sync(sock)
        except (FrameError, OSError) as error:
            self._sock = None
            raise ClusterClientError(f"uds transport failure: {error}") from error
        if raw[0] == FORMAT_JSON:
            response = json.loads(raw[1:])
        else:
            response = wire.decode_response_message(raw[1:])
        if isinstance(response, Mapping) and response.get("kind") is None:
            response = _normalize_wire_envelope(response)
        if isinstance(response, Mapping) and response.get("kind") == "error":
            raise ClusterClientError(f"rejected: {response.get('error')}")
        return response

    # -- HTTP fallback -------------------------------------------------------

    async def _json(
        self, method: str, path: str, payload: object | None = None
    ) -> Mapping[str, object]:
        return await self._loop.run_in_executor(
            self._executor, self._json_sync, method, path, payload
        )

    def _json_sync(
        self, method: str, path: str, payload: object | None
    ) -> Mapping[str, object]:
        headers = {"Authorization": f"Bearer {self._token}"}
        body = None
        if payload is not None:
            body = json.dumps(payload, separators=(",", ":")).encode()
            headers["Content-Type"] = "application/json"
        conn = self._connection()
        try:
            conn.request(method, path, body=body, headers=headers)
            response = conn.getresponse()
            response_data = response.read()
        except (http.client.HTTPException, OSError) as error:
            self._conn = None
            raise ClusterClientError(
                f"{method} {path} transport failure: {error}"
            ) from error
        status = response.status
        if status != 200:
            message = response_data.decode("utf-8", errors="replace")
            raise ClusterClientError(
                f"{method} {path} failed with status {status}: {message}"
            )
        parsed = json.loads(response_data)
        if not isinstance(parsed, Mapping):
            raise TypeError(f"{path} must return an object")
        return parsed


def _response_body(response: object) -> Mapping[str, object]:
    """Strip the wire ``Response`` envelope down to the HTTP-JSON shape."""
    if not isinstance(response, Mapping):
        raise ClusterClientError(f"unexpected response kind: {type(response).__name__}")
    kind = response.get("kind")
    if kind == "model":
        body = response.get("model")
    elif kind in ("observation", "terminal"):
        body = response.get(kind)
    else:
        raise ClusterClientError(f"unexpected response kind: {kind!r}")
    if not isinstance(body, Mapping):
        raise ClusterClientError(f"response body missing for kind {kind!r}")
    return body


def _normalize_wire_envelope(
    response: Mapping[str, object],
) -> Mapping[str, object]:
    """Normalize a serde-JSON wire ``Response`` into the ``kind``-shaped dict.

    Serde serializes the externally-tagged ``Response`` enum as
    ``{"model": {...}}``/``{"state": {...}}``/``{"step": {...}}``/
    ``{"error": {...}}``; postcard decoding now yields typed objects, so
    only the JSON path needs this translation.
    """
    if "error" in response:
        return {"kind": "error", "error": response["error"]}
    for kind in ("model", "state", "step"):
        if kind in response:
            inner = response[kind]
            if not isinstance(inner, Mapping):
                raise ClusterClientError(f"response body missing for {kind}")
            if kind == "model":
                model = inner.get("model")
                if not isinstance(model, Mapping):
                    raise ClusterClientError("response body missing for model")
                return {"kind": "model", "model": model}
            result_env = inner.get("result")
            if not isinstance(result_env, Mapping):
                raise ClusterClientError(f"response result missing for {kind}")
            variant = "observation" if "observation" in result_env else "terminal"
            body_env = result_env.get(variant)
            if not isinstance(body_env, Mapping):
                raise ClusterClientError(f"result body missing for {kind}")
            result = body_env.get(variant)
            if not isinstance(result, Mapping):
                raise ClusterClientError(f"result body missing for {kind}")
            _normalize_recent_events(result)
            return {"kind": variant, variant: result}
    raise ClusterClientError(f"unrecognized response envelope: {list(response)}")


def _normalize_recent_events(result: Mapping[str, object]) -> None:
    """Rewrite serde externally-tagged ``WireRecentEvent`` JSON in place.

    ``{"worker_unavailable": {...fields}}`` ->
    ``{"kind": "worker_unavailable", ...fields}`` to match the domain's
    internally-tagged shape that ``models.py`` consumes.
    """
    events = result.get("recent_events")
    if not isinstance(events, list):
        return
    for index, event in enumerate(events):
        if not isinstance(event, Mapping) or "kind" in event:
            continue
        if len(event) != 1:
            raise ClusterClientError(
                f"unrecognized recent event envelope: {list(event)}"
            )
        kind, fields = next(iter(event.items()))
        if not isinstance(fields, Mapping):
            raise ClusterClientError(f"recent event body missing: {kind!r}")
        events[index] = {"kind": kind, **fields}
