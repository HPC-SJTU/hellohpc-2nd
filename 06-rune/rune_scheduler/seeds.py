from __future__ import annotations

from hashlib import sha256

SCENARIO_SEED_NAMESPACE = "rune-scheduling-game/seed-set"
SCENARIO_SEED_VERSION = "v1"


def scenario_seed(seed_key: str, profile: str, ordinal: int) -> str:
    """Derive one Scenario seed using the evaluation seed-set contract."""
    if len(seed_key) != 64:
        raise ValueError("seed key must be exactly 64 hexadecimal characters")
    try:
        key_bytes = bytes.fromhex(seed_key)
    except ValueError as error:
        raise ValueError("seed key must be hexadecimal") from error
    if not 0 <= ordinal < 2**32:
        raise ValueError("seed ordinal must fit an unsigned 32-bit integer")
    payload = b"\0".join(
        (
            SCENARIO_SEED_NAMESPACE.encode(),
            SCENARIO_SEED_VERSION.encode(),
            key_bytes,
            profile.encode(),
            ordinal.to_bytes(4, "big"),
        )
    )
    return sha256(payload).hexdigest()
