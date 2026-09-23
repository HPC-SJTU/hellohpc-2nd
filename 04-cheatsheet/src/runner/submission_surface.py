"""Canonical, fail-closed Skill submission snapshot and token accounting.

The scoring count is exact for the frozen public Cheatsheet tokenizer. It is called an
estimate only because the provider's deployed model tokenizer is not part of
the public evaluation contract. There is deliberately no word/character
fallback: a missing engine or changed asset is an
infrastructure error.
"""

from __future__ import annotations

import gzip
import hashlib
import os
import re
import stat
import struct
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Iterable

import yaml

try:
    from .task_registry import EXPECTED_PUBLIC_TASKS
except ImportError:
    from task_registry import EXPECTED_PUBLIC_TASKS


SCORING_TOKENIZER_ENGINE = "tokenizers"
SCORING_TOKENIZER_REVISION = "deepseek-ai/DeepSeek-V3@d990f04"
TOKENIZER_ASSET = Path(__file__).with_name("assets") / "cheatsheet-tokenizer.json.gz"
TOKENIZER_COMPRESSED_SHA256 = (
    "e827ef6c53a1e799693cb64d204315a3d4f754dae99f31ad1220b52662ac1df4"
)
TOKENIZER_SHA256 = (
    "621ac2e32d0dba658404412318818aaa8ce8cda492e59830109d8da6b517fb41"
)

SOFT_TOKEN_LIMIT = 800
HARD_TOKEN_LIMIT = 1_200
FULL_BONUS_TOKEN_LIMIT = 400
TOKENS_PER_MULTIPLIER_UNIT = 800
SUPPORTED_MODELS = ("qwen3.8-27b", "deepseek-reasoner")
REASONING_EFFORT = "medium"

# This is a denial-of-service guard, not a scoring limit. A normal submission
# reaches the 1,200-token hard limit below it, including the frozen vocabulary's
# longest whitespace merges. It prevents unbounded input before tokenization
# without becoming a second contestant-facing length rule.
TECHNICAL_TOTAL_BYTES_CAP = 4 * 1024 * 1024
# Internal traversal guard derived from the public token ceiling. It is not a
# reference-file quota: valid files are governed only by their tokenized
# records, while directory-only structures never enter the Agent snapshot.
TECHNICAL_REFERENCE_ENTRY_SCAN_CAP = HARD_TOKEN_LIMIT * 2

_RECORD_START = "<|cheatsheet_submission_file|>\n"
_CONTENT_START = "\n<|cheatsheet_submission_content|>\n"
_RECORD_END = "\n<|cheatsheet_submission_end|>\n"
_DIGEST_DOMAIN = b"cheatsheet-submission-surface\x00"


class SubmissionSurfaceError(ValueError):
    """The declared submission surface is missing, unsafe, or malformed."""


class SubmissionLengthError(SubmissionSurfaceError):
    """The running canonical token count crossed the public hard limit."""

    def __init__(self, estimated_tokens: int) -> None:
        self.estimated_tokens = estimated_tokens
        super().__init__(
            "estimated token count is at least %d and exceeds hard limit %d"
            % (estimated_tokens, HARD_TOKEN_LIMIT)
        )


class TokenizerInfrastructureError(RuntimeError):
    """The frozen scoring tokenizer cannot be loaded exactly."""


@dataclass(frozen=True)
class SurfaceFile:
    path: str
    text: str
    content: bytes
    source_size_bytes: int
    estimated_tokens: int
    sha256: str


@dataclass(frozen=True)
class LengthPolicy:
    estimated_tokens: int
    soft_limit: int
    hard_limit: int
    hard_limit_remaining: int
    adjustment: str
    length_multiplier: float
    penalized: bool
    rejected: bool


@dataclass(frozen=True)
class SubmissionSurface:
    root: Path
    config: "SubmissionConfig"
    files: tuple[SurfaceFile, ...]
    total_bytes: int
    estimated_tokens: int
    sha256: str
    tokenizer_sha256: str
    length: LengthPolicy

    @property
    def operator(self) -> str:
        return self.config.operator

    @property
    def model(self) -> str:
        return self.config.model


@dataclass(frozen=True)
class SubmissionConfig:
    operator: str
    model: str

    def content(self) -> bytes:
        return f"operator: {self.operator}\nmodel: {self.model}\n".encode("utf-8")


def parse_submission_config(text: str) -> SubmissionConfig:
    """Accept only two scalar choices; never pass raw YAML to the Agent."""
    try:
        node = yaml.compose(text)
    except yaml.YAMLError as exc:
        raise SubmissionSurfaceError(f"invalid submission.yaml: {exc}") from exc
    if not isinstance(node, yaml.MappingNode) or node.tag != "tag:yaml.org,2002:map":
        raise SubmissionSurfaceError("submission.yaml must be a YAML mapping")
    metadata = {}
    for key, value in node.value:
        if not all(isinstance(item, yaml.ScalarNode) and item.tag == "tag:yaml.org,2002:str"
                   for item in (key, value)):
            raise SubmissionSurfaceError("submission.yaml fields must be strings")
        if key.value in metadata:
            raise SubmissionSurfaceError("submission.yaml must not contain duplicate fields")
        metadata[key.value] = value.value
    if set(metadata) != {"operator", "model"}:
        raise SubmissionSurfaceError("submission.yaml requires exactly operator and model")
    if metadata["operator"] not in EXPECTED_PUBLIC_TASKS:
        raise SubmissionSurfaceError("submission.yaml operator must be 'bitmatrix' or 'fft'")
    if metadata["model"] not in SUPPORTED_MODELS:
        raise SubmissionSurfaceError("submission.yaml model must be one of: " + ", ".join(SUPPORTED_MODELS))
    return SubmissionConfig(**metadata)


def validate_skill_metadata(text: str) -> None:
    """Validate Skill metadata; evaluation choices belong in submission.yaml."""
    match = re.match(r"\A\ufeff?---[ \t]*\r?\n(.*?)\r?\n---[ \t]*(?:\r?\n|\Z)", text, re.DOTALL)
    if not match:
        raise SubmissionSurfaceError("missing YAML frontmatter at the start of SKILL.md")
    try:
        metadata = yaml.safe_load(match.group(1))
        node = yaml.compose(match.group(1))
    except yaml.YAMLError as exc:
        raise SubmissionSurfaceError(f"invalid YAML frontmatter: {exc}") from exc
    if not isinstance(metadata, dict):
        raise SubmissionSurfaceError("SKILL.md frontmatter must be a YAML mapping")
    keys = [key.value for key, _ in node.value]
    if len(keys) != len(set(keys)):
        raise SubmissionSurfaceError("SKILL.md frontmatter must not contain duplicate fields")
    if metadata.get("name") != "cpu-hpc-skill":
        raise SubmissionSurfaceError("frontmatter name must be exactly 'cpu-hpc-skill'")
    description = metadata.get("description")
    if not isinstance(description, str) or not description.strip():
        raise SubmissionSurfaceError("frontmatter description must be a non-empty string")
    if any(key in metadata for key in ("operator", "model", "reasoning_effort", "thinking")):
        raise SubmissionSurfaceError("put operator and model in submission.yaml; reasoning effort is fixed to medium")


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@lru_cache(maxsize=1)
def scoring_tokenizer():
    """Load the scoring tokenizer after verifying its asset bytes."""
    try:
        from tokenizers import Tokenizer
    except ImportError as exc:
        raise TokenizerInfrastructureError(
            "Cheatsheet scoring requires tokenizers; install requirements-scoring.txt"
        ) from exc
    try:
        compressed = TOKENIZER_ASSET.read_bytes()
    except OSError as exc:
        raise TokenizerInfrastructureError(
            "frozen Cheatsheet tokenizer asset is missing or unreadable"
        ) from exc
    if _sha256(compressed) != TOKENIZER_COMPRESSED_SHA256:
        raise TokenizerInfrastructureError(
            "frozen Cheatsheet tokenizer compressed hash mismatch"
        )
    try:
        raw = gzip.decompress(compressed)
    except (OSError, EOFError) as exc:
        raise TokenizerInfrastructureError(
            "frozen Cheatsheet tokenizer is not valid gzip"
        ) from exc
    if _sha256(raw) != TOKENIZER_SHA256:
        raise TokenizerInfrastructureError(
            "frozen Cheatsheet tokenizer source hash mismatch"
        )
    try:
        return Tokenizer.from_str(raw.decode("utf-8"))
    except (UnicodeError, ValueError) as exc:
        raise TokenizerInfrastructureError(
            "frozen Cheatsheet tokenizer cannot be loaded"
        ) from exc


def estimate_tokens(text: str) -> int:
    """Return an exact ID count under the frozen Cheatsheet scoring tokenizer."""
    return len(scoring_tokenizer().encode(text, add_special_tokens=False).ids)


def _record_text(relative_path: str, text: str) -> str:
    # Both the normalized relative path and every content byte are inside the
    # tokenized record. Calling encode once per record creates an unambiguous
    # fixed boundary and prevents merges across files.
    return _RECORD_START + relative_path + _CONTENT_START + text + _RECORD_END


def length_policy(estimated_tokens: int) -> LengthPolicy:
    if (
        isinstance(estimated_tokens, bool)
        or not isinstance(estimated_tokens, int)
        or estimated_tokens < 0
    ):
        raise ValueError("estimated_tokens must be a non-negative integer")
    remaining = max(0, HARD_TOKEN_LIMIT - estimated_tokens)
    if estimated_tokens > HARD_TOKEN_LIMIT:
        return LengthPolicy(
            estimated_tokens,
            SOFT_TOKEN_LIMIT,
            HARD_TOKEN_LIMIT,
            remaining,
            "rejected",
            0.0,
            False,
            True,
        )
    multiplier = 1.0 + (
        SOFT_TOKEN_LIMIT - max(estimated_tokens, FULL_BONUS_TOKEN_LIMIT)
    ) / TOKENS_PER_MULTIPLIER_UNIT
    adjustment = (
        "bonus" if estimated_tokens < SOFT_TOKEN_LIMIT
        else "penalty" if estimated_tokens > SOFT_TOKEN_LIMIT
        else "neutral"
    )
    return LengthPolicy(
        estimated_tokens,
        SOFT_TOKEN_LIMIT,
        HARD_TOKEN_LIMIT,
        remaining,
        adjustment,
        multiplier,
        estimated_tokens > SOFT_TOKEN_LIMIT,
        False,
    )


def _absolute(path: Path) -> Path:
    return Path(os.path.abspath(os.fspath(path)))


def _require_no_symlink_components(path: Path, label: str) -> None:
    """Reject aliases in every existing component of an absolute path."""
    absolute = _absolute(path)
    current = Path(absolute.anchor)
    parts = absolute.parts[1:] if absolute.anchor else absolute.parts
    for part in parts:
        current = current / part
        try:
            mode = os.lstat(current).st_mode
        except FileNotFoundError:
            # Once an ancestor is missing, no later lexical component can
            # already exist. Callers separately decide whether absence is OK.
            return
        except OSError as exc:
            raise SubmissionSurfaceError(
                f"{label} cannot be inspected"
            ) from exc
        if stat.S_ISLNK(mode):
            raise SubmissionSurfaceError(
                f"symlink not allowed in {label}: {current}"
            )


def _require_directory(path: Path, label: str) -> None:
    try:
        mode = os.lstat(path).st_mode
    except OSError as exc:
        raise SubmissionSurfaceError(f"{label} is missing") from exc
    if stat.S_ISLNK(mode):
        raise SubmissionSurfaceError(f"symlink not allowed: {label}")
    if not stat.S_ISDIR(mode):
        raise SubmissionSurfaceError(f"{label} must be a directory")


def _relative_posix(path: Path, root: Path) -> str:
    try:
        relative = path.relative_to(root)
    except ValueError as exc:
        raise SubmissionSurfaceError("submission path escapes its root") from exc
    if not relative.parts or any(
        part in ("", ".", "..") for part in relative.parts
    ):
        raise SubmissionSurfaceError(
            "submission contains an invalid relative path"
        )
    if any(part.startswith(".") for part in relative.parts):
        raise SubmissionSurfaceError(
            "hidden file/dir not allowed: " + relative.as_posix()
        )
    value = relative.as_posix()
    try:
        value.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise SubmissionSurfaceError(
            "submission path is not valid UTF-8"
        ) from exc
    return value


def _walk_references(root: Path, references: Path) -> Iterable[Path]:
    """Yield safe Markdown files without inspecting later siblings early."""
    _require_directory(references, "references")
    scanned_entries = 0

    def names_in(directory: Path) -> list[str]:
        nonlocal scanned_entries
        names: list[str] = []
        try:
            entries = os.scandir(directory)
        except OSError as exc:
            raise SubmissionSurfaceError(
                "references directory is unreadable"
            ) from exc
        with entries:
            for entry in entries:
                scanned_entries += 1
                if scanned_entries > TECHNICAL_REFERENCE_ENTRY_SCAN_CAP:
                    raise SubmissionSurfaceError(
                        "reference tree exceeds technical traversal safety budget"
                    )
                names.append(entry.name)
        if not names:
            if directory == references:
                return names
            relative = _relative_posix(directory, root)
            raise SubmissionSurfaceError(
                f"empty reference directory not allowed: {relative}"
            )
        # os.fsencode is deterministic and does not fail on undecodable names.
        # _relative_posix rejects those names before they can be materialized.
        names.sort(key=os.fsencode)
        return names

    stack: list[tuple[Path, object]] = [
        (references, iter(names_in(references)))
    ]
    while stack:
        directory, iterator = stack[-1]
        try:
            name = next(iterator)
        except StopIteration:
            stack.pop()
            continue
        path = directory / name
        relative = _relative_posix(path, root)
        try:
            mode = os.lstat(path).st_mode
        except OSError as exc:
            raise SubmissionSurfaceError(
                f"unreadable submission entry: {relative}"
            ) from exc
        if stat.S_ISLNK(mode):
            raise SubmissionSurfaceError(
                f"symlink not allowed: {relative}"
            )
        if stat.S_ISDIR(mode):
            stack.append((path, iter(names_in(path))))
            continue
        if not stat.S_ISREG(mode):
            raise SubmissionSurfaceError(
                f"non-regular file not allowed: {relative}"
            )
        if path.suffix.lower() != ".md":
            raise SubmissionSurfaceError(
                f"non-markdown file not allowed: {relative}"
            )
        yield path


def _check_standalone_root(root: Path) -> None:
    try:
        entries = os.scandir(root)
    except OSError as exc:
        raise SubmissionSurfaceError(
            "submission root is unreadable"
        ) from exc
    with entries:
        for entry in entries:
            if entry.name in {"submission.yaml", "SKILL.md", "references"}:
                continue
            relative = _relative_posix(Path(entry.path), root)
            raise SubmissionSurfaceError(
                f"unexpected entry outside submission.yaml/SKILL.md/references: {relative}"
            )


def _read_regular_utf8(
    path: Path, root: Path, bytes_left: int
) -> tuple[str, bytes, int]:
    relative = _relative_posix(path, root)
    try:
        before = os.lstat(path)
    except OSError as exc:
        raise SubmissionSurfaceError(f"unreadable: {relative}") from exc
    if stat.S_ISLNK(before.st_mode):
        raise SubmissionSurfaceError(f"symlink not allowed: {relative}")
    if not stat.S_ISREG(before.st_mode):
        raise SubmissionSurfaceError(
            f"non-regular file not allowed: {relative}"
        )
    if before.st_size > bytes_left:
        raise SubmissionSurfaceError(
            f"submission exceeds {TECHNICAL_TOTAL_BYTES_CAP} byte "
            "technical safety cap"
        )
    flags = os.O_RDONLY
    # Preserve CRLF bytes on Windows for size checks before normalization.
    flags |= getattr(os, "O_BINARY", 0)
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise SubmissionSurfaceError(f"unreadable: {relative}") from exc
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            raise SubmissionSurfaceError(
                f"non-regular file not allowed: {relative}"
            )
        if (before.st_dev, before.st_ino) != (
            opened.st_dev,
            opened.st_ino,
        ):
            raise SubmissionSurfaceError(
                f"submission file changed while opening: {relative}"
            )
        chunks: list[bytes] = []
        remaining = bytes_left + 1
        while remaining > 0:
            chunk = os.read(descriptor, min(64 * 1024, remaining))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        raw = b"".join(chunks)
        after = os.fstat(descriptor)
    finally:
        os.close(descriptor)
    if len(raw) > bytes_left:
        raise SubmissionSurfaceError(
            f"submission exceeds {TECHNICAL_TOTAL_BYTES_CAP} byte "
            "technical safety cap"
        )
    if (
        opened.st_size != after.st_size
        or opened.st_mtime_ns != after.st_mtime_ns
        or len(raw) != after.st_size
    ):
        raise SubmissionSurfaceError(
            f"submission file changed while reading: {relative}"
        )
    if b"\x00" in raw:
        raise SubmissionSurfaceError(
            f"binary content (NUL byte): {relative}"
        )
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise SubmissionSurfaceError(
            f"invalid UTF-8 at byte {exc.start}: {relative}"
        ) from exc
    normalized_text = text.replace("\r\n", "\n")
    return normalized_text, normalized_text.encode("utf-8"), len(raw)


def _surface_digest(files: Iterable[SurfaceFile], config: SubmissionConfig) -> str:
    digest = hashlib.sha256(_DIGEST_DOMAIN)
    content = config.content()
    digest.update(struct.pack(">Q", len(content)))
    digest.update(content)
    for item in files:
        path = item.path.encode("utf-8")
        digest.update(struct.pack(">Q", len(path)))
        digest.update(path)
        digest.update(struct.pack(">Q", len(item.content)))
        digest.update(item.content)
    return digest.hexdigest()


def snapshot_submission_surface(
    submission: Path, *, repository_layout: bool = False
) -> SubmissionSurface:
    """Freeze validated evaluation choices and the Agent-visible Skill files."""
    supplied = _absolute(Path(submission))
    _require_no_symlink_components(supplied, "submission path")
    try:
        supplied_mode = os.lstat(supplied).st_mode
    except OSError as exc:
        raise SubmissionSurfaceError(
            f"submission path is missing: {supplied}"
        ) from exc
    if stat.S_ISLNK(supplied_mode):
        raise SubmissionSurfaceError(
            "submission root/SKILL.md must not be a symlink"
        )
    if stat.S_ISREG(supplied_mode):
        if supplied.name != "SKILL.md":
            raise SubmissionSurfaceError(
                "submission file must be named SKILL.md"
            )
        root = supplied.parent
        _require_directory(root, "submission root")
        skill = supplied
    elif stat.S_ISDIR(supplied_mode):
        root = supplied
        skill = root / "SKILL.md"
    else:
        raise SubmissionSurfaceError(
            "submission path must be a directory or SKILL.md"
        )

    if not repository_layout:
        _check_standalone_root(root)
    config_text, _, config_bytes = _read_regular_utf8(
        root / "submission.yaml", root, TECHNICAL_TOTAL_BYTES_CAP
    )
    config = parse_submission_config(config_text)
    references = root / "references"
    reference_paths: list[Path] = []
    try:
        references_mode = os.lstat(references).st_mode
    except FileNotFoundError:
        pass
    except OSError as exc:
        raise SubmissionSurfaceError(
            "references cannot be inspected"
        ) from exc
    else:
        if stat.S_ISLNK(references_mode):
            raise SubmissionSurfaceError(
                "symlink not allowed: references"
            )
        if not stat.S_ISDIR(references_mode):
            raise SubmissionSurfaceError(
                "references must be a directory"
            )
        reference_paths = _walk_references(root, references)

    records: list[SurfaceFile] = []
    total_source_bytes = config_bytes
    total_tokens = 0
    paths: Iterable[Path] = (skill,)
    if reference_paths:
        paths = (path for group in ((skill,), reference_paths) for path in group)
    for path in paths:
        relative = _relative_posix(path, root)
        text, content, source_size = _read_regular_utf8(
            path,
            root,
            TECHNICAL_TOTAL_BYTES_CAP - total_source_bytes,
        )
        total_source_bytes += source_size
        record_tokens = estimate_tokens(_record_text(relative, text))
        total_tokens += record_tokens
        if total_tokens > HARD_TOKEN_LIMIT:
            raise SubmissionLengthError(total_tokens)
        records.append(
            SurfaceFile(
                path=relative,
                text=text,
                content=content,
                source_size_bytes=source_size,
                estimated_tokens=record_tokens,
                sha256=_sha256(content),
            )
        )
    frozen = tuple(records)
    validate_skill_metadata(frozen[0].text)
    return SubmissionSurface(
        root=root,
        config=config,
        files=frozen,
        total_bytes=total_source_bytes,
        estimated_tokens=total_tokens,
        sha256=_surface_digest(frozen, config),
        tokenizer_sha256=TOKENIZER_SHA256,
        length=length_policy(total_tokens),
    )


def require_eligible_surface(
    submission: Path, *, repository_layout: bool = False
) -> SubmissionSurface:
    surface = snapshot_submission_surface(
        submission, repository_layout=repository_layout
    )
    if surface.length.rejected:
        raise SubmissionSurfaceError(
            "estimated token count %d exceeds hard limit %d"
            % (surface.estimated_tokens, HARD_TOKEN_LIMIT)
        )
    return surface


def materialize_surface(
    surface: SubmissionSurface, target: Path, *, include_config: bool = True
) -> Path:
    """Write only captured normalized bytes; never re-read contestant paths."""
    target = _absolute(Path(target))
    _require_no_symlink_components(target, "snapshot target path")
    if target.exists() or target.is_symlink():
        raise FileExistsError(
            f"refusing to overwrite submission snapshot: {target}"
        )
    target.mkdir(parents=True)
    if include_config:
        (target / "submission.yaml").write_bytes(surface.config.content())
    for item in surface.files:
        destination = target / Path(item.path)
        try:
            destination.relative_to(target)
        except ValueError as exc:
            raise SubmissionSurfaceError(
                "captured path escapes snapshot root"
            ) from exc
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as stream:
            stream.write(item.content)
    return target
