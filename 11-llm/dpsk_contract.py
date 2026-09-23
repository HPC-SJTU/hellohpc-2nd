"""Public Stage2 dataset schemas and fixed local measurement parameters.

All TypedDict fields are required. File hashes bind the public records and
calibration to the manifest. Tokenizer metadata describes offline prompt
rendering; no tokenizer endpoint is needed at measurement time.

D1 sends 20 warmups then 60 measured requests. D2 sends preset prompt prefixes
in six complete rounds, with one continuous measured interval. GSM uses all
1319 questions, natural EOS and a 1024-token cap; truncated answers are wrong.
Answers use the first standalone FINAL: line and exact numeric comparison.
Scoring formulas, accuracy thresholds and service rules are in README.md.
"""

from __future__ import annotations

from typing import Literal, TypedDict

SCHEMA_VERSION = 2
PROTOCOL = "dpsk-stage2-v2"
GSM_PROTOCOL = "gsm8k-test-nonthinking-final-v1"
D2_INPUT_LENGTHS = (2048, 32768, 65536, 98304, 99328, 100352)
D2_MATH_LOCATIONS = ((0, 1), (8, 3), (16, 5))  # zero-based (session, turn)
GSM_COUNT = 1319
PROFILES = {
    "d1": {"concurrency": 20, "input_tokens": 32768, "output_tokens": 4096,
           "warmup_requests": 20, "formal_requests": 60, "math_requests": 3,
           "max_score": 30},
    "d2": {"concurrency": 4, "sessions": 24, "turns": 6, "output_tokens": 128,
           "warmup_requests": 0, "formal_requests": 144, "math_requests": 3,
           "max_score": 40},
}
TIMEOUTS_SECONDS = {
    "stage": 4800, "readiness": 1800,
    "d1_request": 600, "d2_request": 600,
    "gsm_request": 300, "gsm_phase": 1200,
}


class FileSpec(TypedDict):
    path: str
    sha256: str
    records: int


class DatasetFiles(TypedDict):
    d1: FileSpec
    d2: FileSpec
    gsm: FileSpec
    provenance: FileSpec


class TokenizerSpec(TypedDict):
    id: str
    revision: str
    files_sha256: dict[str, str]
    digest: str
    chat_template_sha256: str
    gsm_template_verified: bool


class AccuracyReference(TypedDict):
    protocol: str
    status: Literal["pending", "validated"]
    correct: int | None
    total: int
    evidence: str | None


class Manifest(TypedDict):
    schema_version: int
    protocol: str
    split: Literal["public"]
    model_id: str
    model_revision: str
    tokenizer: TokenizerSpec
    files: DatasetFiles
    accuracy_reference: AccuracyReference


class PerformanceRecord(TypedDict):
    id: str
    phase: Literal["warmup", "formal"]
    kind: Literal["traffic", "math"]
    prompt_ids: list[int]
    expected_answer: str | None
    source_refs: list[str]


class MathProbe(TypedDict):
    turn: int
    expected_answer: str
    source_refs: list[str]


class SessionRecord(TypedDict):
    id: str
    session_index: int
    prompt_ids: list[int]
    math_probes: list[MathProbe]
    source_refs: list[str]


class GSMRecord(TypedDict):
    id: str
    prompt_ids: list[int]
    expected_answer: str
    source_refs: list[str]


class ProvenanceRecord(TypedDict):
    id: str
    source: str
    source_id: str
    details: dict[str, object]


class CaseCalibration(TypedDict):
    p0_output_tps: float | None
    pf_output_tps: float | None
    ttft_sla_seconds: float | None
    tpot_sla_milliseconds: float | None


class CalibrationCases(TypedDict):
    d1: CaseCalibration
    d2: CaseCalibration


class Calibration(TypedDict):
    schema_version: int
    protocol: str
    status: Literal["pending", "validated"]
    dataset_manifest_sha256: str
    evidence: str | None
    cases: CalibrationCases
