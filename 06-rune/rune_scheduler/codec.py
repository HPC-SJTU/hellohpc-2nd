"""Binary frame codec for the rune-cluster UDS data plane.

Wire contract (must stay in sync with ``rune-cluster/src/wire.rs``):

- Frame: ``u32 LE payload length | payload``.
- Payload: ``u8 format tag | encoded message`` (0 = postcard, 1 = JSON).
- postcard has no field names, so decode order == Rust struct declaration
  order. Messages are built as plain dicts shaped like the JSON contract,
  with enum/option/newtype wrappers encoded inline.
"""

from __future__ import annotations

import json
import struct
from collections.abc import Mapping, Sequence
from typing import Any

FORMAT_POSTCARD = 0
FORMAT_JSON = 1
MAX_FRAME_SIZE = 64 * 1024 * 1024

U32 = struct.Struct("<I")


class FrameError(RuntimeError):
    pass


def write_frame_sync(sock: Any, payload: bytes) -> None:
    sock.sendall(U32.pack(len(payload)) + payload)


def read_exact_sync(sock: Any, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise FrameError("connection closed mid-frame")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_frame_sync(sock: Any) -> bytes:
    (size,) = U32.unpack(read_exact_sync(sock, 4))
    if size > MAX_FRAME_SIZE:
        raise FrameError(f"frame of {size} bytes exceeds limit")
    return read_exact_sync(sock, size)


def encode_message(message: Mapping[str, Any], fmt: int) -> bytes:
    if fmt == FORMAT_JSON:
        return (
            bytes([fmt])
            + json.dumps(_serde_json_value(message), separators=(",", ":")).encode()
        )
    return bytes([fmt]) + _encode_postcard(message)


def _serde_json_value(value: Any) -> Any:
    """Convert an encoder descriptor tree into serde-compatible JSON."""
    if isinstance(value, Mapping):
        kind = value.get("__kind__")
        if kind == "enum":
            values = value.get("values") or {}
            payload = {k: _serde_json_value(v) for k, v in values.items()}
            if not payload:
                return value["variant"]
            return {value["variant"]: payload}
        if kind == "struct":
            return {
                field: _serde_json_value(value["values"][field])
                for field in value["fields"]
            }
        if kind == "option":
            inner = value["value"]
            return None if inner is None else _serde_json_value(inner)
        if kind == "newtype":
            return _serde_json_value(value["value"])
        return {k: _serde_json_value(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_serde_json_value(item) for item in value]
    return value


def decode_message(payload: bytes) -> Any:
    """Decode a self-describing JSON payload (FORMAT_JSON only).

    postcard payloads are not self-describing: callers decode them against
    a known schema via :class:`_Reader` (see ``wire.py``).
    """
    tag = payload[0]
    if tag == FORMAT_JSON:
        return json.loads(payload[1:])
    if tag == FORMAT_POSTCARD:
        raise FrameError("postcard payloads require schema-driven decoding")
    raise FrameError(f"unknown format tag {tag}")


def decode_welcome(payload: bytes) -> dict[str, Any]:
    """Decode ``Welcome { ok: bool, error: Option<String> }`` (postcard)."""
    tag = payload[0]
    if tag == FORMAT_JSON:
        return json.loads(payload[1:])
    if tag != FORMAT_POSTCARD:
        raise FrameError(f"unknown format tag {tag}")
    reader = _Reader(payload, 1)
    welcome: dict[str, Any] = {"ok": reader.read_bool()}
    if reader.read_u8() != 0:
        welcome["error"] = reader.read_str()
    else:
        welcome["error"] = None
    return welcome


# ---------------------------------------------------------------------------
# postcard primitives
# ---------------------------------------------------------------------------


class _Reader:
    __slots__ = ("_buf", "_pos")

    def __init__(self, buf: bytes, pos: int) -> None:
        self._buf = buf
        self._pos = pos

    def read_varint(self) -> int:
        buf = self._buf
        pos = self._pos
        result = 0
        shift = 0
        while True:
            byte = buf[pos]
            pos += 1
            result |= (byte & 0x7F) << shift
            if not byte & 0x80:
                break
            shift += 7
        self._pos = pos
        return result

    def read_len(self) -> int:
        # postcard seq sizes are varints with a trailing-continuation quirk:
        # stored as LEB128 but with the last byte never setting the high bit,
        # so a straight LEB128 decode gives the length directly.
        return self.read_varint()

    def read_bool(self) -> bool:
        value = self._buf[self._pos]
        self._pos += 1
        if value not in (0, 1):
            raise FrameError(f"invalid bool byte {value}")
        return value == 1

    def read_bytes(self) -> bytes:
        length = self.read_len()
        end = self._pos + length
        if end > len(self._buf):
            raise FrameError("string length overruns buffer")
        data = self._buf[self._pos : end]
        self._pos = end
        return data

    def read_str(self) -> str:
        return self.read_bytes().decode("utf-8")

    def read_u8(self) -> int:
        value = self._buf[self._pos]
        self._pos += 1
        return value

    def read_u32(self) -> int:
        value = int.from_bytes(self._buf[self._pos : self._pos + 4], "little")
        self._pos += 4
        return value

    def read_i16(self) -> int:
        value = int.from_bytes(
            self._buf[self._pos : self._pos + 2], "little", signed=True
        )
        self._pos += 2
        return value


def _encode_varint(value: int, out: bytearray) -> None:
    if value < 0:
        raise FrameError("negative varint")
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return


def _encode_len(length: int, out: bytearray) -> None:
    # Mirror postcard's seq encoding: lengths are varints, but postcard
    # guarantees the encoding never ends on a continuation-worthy boundary.
    # For lengths >= 0x80 the final byte keeps the high bit clear, which the
    # plain LEB128 loop above already produces.
    _encode_varint(length, out)


def _encode_postcard(value: Any) -> bytes:
    out = bytearray()
    _encode_value(value, out)
    return bytes(out)


def _encode_value(value: Any, out: bytearray) -> None:
    if isinstance(value, str):
        data = value.encode("utf-8")
        _encode_len(len(data), out)
        out += data
    elif isinstance(value, bool):
        out.append(1 if value else 0)
    elif isinstance(value, int):
        _encode_varint(value, out)
    elif isinstance(value, float):
        out += struct.pack("<d", value)
    elif isinstance(value, Sequence):
        _encode_len(len(value), out)
        for item in value:
            _encode_value(item, out)
    elif isinstance(value, Mapping):
        kind = value.get("__kind__")
        if kind == "enum":
            _encode_enum(value, out)
        elif kind == "newtype":
            _encode_value(value["value"], out)
        elif kind == "option":
            if value["value"] is None:
                out.append(0)
            else:
                out.append(1)
                _encode_value(value["value"], out)
        elif kind == "struct":
            for key in value["fields"]:
                _encode_value(value["values"][key], out)
        else:
            raise FrameError(f"unencodable mapping kind: {value.keys()}")
    elif value is None:
        out.append(0)
    else:
        raise FrameError(f"unencodable type: {type(value).__name__}")


def _encode_enum(value: Mapping[str, Any], out: bytearray) -> None:
    variants: Sequence[Mapping[str, Any]] = value["variants"]
    name = value["variant"]
    for index, variant in enumerate(variants):
        if variant["name"] == name:
            if index > 0x7F:
                raise FrameError("enum discriminant over 127 unsupported")
            out.append(index)
            values = value.get("values") or {}
            for field in values:
                _encode_value(values[field], out)
            return
    raise FrameError(f"unknown enum variant {name}")
