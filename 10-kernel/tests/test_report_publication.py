from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import os

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('publish_reports', ROOT / 'scripts/publish_reports.py')
reports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reports)


@pytest.fixture
def tree(tmp_path):
    root = tmp_path / 'repo'
    (root / 'artifacts').mkdir(parents=True)
    destination = tmp_path / 'published'
    destination.mkdir()
    return root, destination


def test_public_reports_preserve_partial_results_and_exclude_inputs(tree):
    root, destination = tree
    artifacts = root / 'artifacts'
    (artifacts / 'public_benchmark_raw').mkdir()
    (artifacts / 'public_benchmark_raw/round1.json').write_text('{"median_ms": 1}')
    (artifacts / 'public_correctness_results.json').write_text('{"passed": true}')
    (artifacts / 'generated').mkdir()
    (artifacts / 'generated/secret.bin').write_bytes(b'input')
    reports.publish(root, destination, 'public')
    assert (destination / 'public_benchmark_raw/round1.json').is_file()
    assert not (destination / 'public_benchmark.json').exists()
    assert not (destination / 'generated').exists()


@pytest.mark.parametrize('location', ['artifacts', 'public_benchmark_raw', 'nested'])
def test_source_links_are_rejected(tree, tmp_path, location):
    root, destination = tree
    outside = tmp_path / 'outside'
    outside.mkdir()
    (outside / 'private.json').write_text('PRIVATE')
    artifacts = root / 'artifacts'
    if location == 'artifacts':
        artifacts.rmdir()
        artifacts.symlink_to(outside, target_is_directory=True)
    elif location == 'public_benchmark_raw':
        (artifacts / location).symlink_to(outside, target_is_directory=True)
    else:
        (artifacts / 'public_benchmark_raw').mkdir()
        (artifacts / 'public_benchmark_raw/nested').symlink_to(outside / 'private.json')
    with pytest.raises(OSError):
        reports.publish(root, destination, 'public')
    assert all(b'PRIVATE' not in path.read_bytes() for path in destination.rglob('*') if path.is_file())


def test_raced_file_replacement_cannot_follow_link(tree, tmp_path, monkeypatch):
    root, destination = tree
    path = root / 'artifacts/public_benchmark.json'
    path.write_text('{}')
    private = tmp_path / 'private'
    private.write_text('PRIVATE')
    original = reports._open
    def replace(parent, name):
        path.unlink()
        path.symlink_to(private)
        return original(parent, name)
    monkeypatch.setattr(reports, '_open', replace)
    with pytest.raises(OSError):
        reports.publish(root, destination, 'public')
    assert not (destination / path.name).exists()


def test_fifo_is_rejected_without_blocking(tree):
    root, destination = tree
    os.mkfifo(root / 'artifacts/public_benchmark.json')
    with pytest.raises(ValueError, match='non-regular'):
        reports.publish(root, destination, 'public')


def test_official_exports_only_typed_summary(tree):
    root, destination = tree
    (root / 'artifacts/hellohpc_summary.json').write_text(json.dumps({
        'evaluation_category': 'accepted', 'submission_eligible': True, 'performance': 2.5,
        'performance_name': 'PRIVATE', 'reason': 'PRIVATE SHAPE', 'cases': ['PRIVATE']}))
    (root / 'artifacts/logs').mkdir()
    (root / 'artifacts/logs/secret').write_text('PRIVATE')
    reports.publish(root, destination, 'official')
    assert list(p.name for p in destination.iterdir()) == ['evaluation_summary.json']
    result = (destination / 'evaluation_summary.json').read_text()
    assert 'PRIVATE' not in result
    assert json.loads(result)['performance'] == 2.5


def test_outer_timeout_has_incomplete_summary(tree):
    root, destination = tree
    reports.publish(root, destination, 'official')
    summary = json.loads((destination / 'evaluation_summary.json').read_text())
    assert summary['evaluation_category'] == 'infrastructure_error'
    assert summary['submission_eligible'] is False


@pytest.mark.parametrize('profile', ['public', 'official'])
def test_cli_finally_publishes_after_outer_timeout(tmp_path, profile):
    # Exercise the merged CLI lifecycle rather than just calling the publisher.
    import shutil
    import yaml
    pytest.importorskip("hellohpc")
    from hellohpc.config import load_config
    from hellohpc.runner import run_workflow

    root = tmp_path / 'problem'
    root.mkdir()
    (root / 'scripts').mkdir()
    shutil.copyfile(ROOT / 'scripts/publish_reports.py', root / 'scripts/publish_reports.py')
    (root / 'solution.txt').write_text('editable')
    config = {
        'version': 'hellohpc/v0.4.0',
        'problem': {'id': 'export-timeout', 'title': 'export timeout'},
        'workspace': {'editable': ['solution.txt'], 'readonly': ['scripts/**']},
        'workflow': {
            'setup': [{'id': 'evaluate', 'run': 'mkdir -p artifacts; echo PARTIAL > artifacts/public_benchmark.json; sleep 5',
                       'timeout': '0.2s', 'secret': profile == 'official', 'timeout-status': 'infrastructure_error'}],
            'matrix': {'id': ['only'], 'score': [100]},
            'steps': [{'id': 'noop', 'run': 'true'}],
            'finally': [{'id': 'publish', 'run': f'python3 scripts/publish_reports.py --profile {profile}'}],
            'result': {'score': {'uses': 'builtin/score-ratio', 'with': {'value': 1, 'full-at': 1, 'zero-at': 2}}},
        },
    }
    path = root / 'problem.yaml'
    path.write_text(yaml.safe_dump(config))
    outcome = run_workflow(load_config(path), input_overrides=[], selected_cases=[], submission=None,
                           keep_workspace=False, artifacts_dir=tmp_path / 'exports')
    assert outcome.exit_code == 4
    destination = Path(outcome.result['run']['artifacts'])
    if profile == 'public':
        assert (destination / 'public_benchmark.json').read_text() == 'PARTIAL\n'
    else:
        assert not (destination / 'public_benchmark.json').exists()
        assert json.loads((destination / 'evaluation_summary.json').read_text())['submission_eligible'] is False
        assert all('PARTIAL' not in p.read_text() for p in destination.rglob('*.json'))


def test_summary_write_failure_does_not_erase_evaluation(tmp_path, monkeypatch):
    import argparse
    from scripts import hellohpc_adapter as adapter

    monkeypatch.setattr(adapter, "ROOT", tmp_path)
    evaluation = adapter.Evaluation("accepted", True, 2.0, "G", "completed", "accepted")
    if hasattr(adapter, "run_evaluation"):
        monkeypatch.setattr(adapter, "run_evaluation", lambda profile: evaluation)
    else:
        monkeypatch.setattr(adapter, "evaluate_public", lambda: evaluation)
    emitted = []
    monkeypatch.setattr(adapter, "_emit", emitted.append)
    def no_space(*args, **kwargs):
        raise OSError(28, "No space left on device")
    monkeypatch.setattr(adapter, "_atomic_json", no_space)
    assert adapter._evaluate_command(argparse.Namespace(profile="official")) == 0
    assert emitted == [evaluation.outputs()]
