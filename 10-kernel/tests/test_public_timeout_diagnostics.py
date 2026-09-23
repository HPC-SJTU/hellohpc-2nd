from __future__ import annotations

import json
import subprocess

from tools.benchmark import player_benchmark
from tools.correctness import run_correctness
import pytest


def timeout(command, **kwargs):
    raise subprocess.TimeoutExpired(command, kwargs['timeout'], output=b'partial\n', stderr=b'error\n')


def test_correctness_timeout_preserves_bytes_and_context(tmp_path, monkeypatch, capsys):
    case = tmp_path / 'public_case'
    case.mkdir()
    (case / 'meta.json').write_text(json.dumps({'N': 1, 'D': 32, 'S': 1}))
    monkeypatch.setattr(run_correctness.subprocess, 'run', timeout)
    result = run_correctness._run_variant(tmp_path, 'solution', 'public', case,
                                          tmp_path / 'out', tmp_path / 'logs', 120)
    assert result['timed_out'] is True
    log = next((tmp_path / 'logs').glob('*.log')).read_text()
    assert 'partial\nerror\n' in log
    assert 'elapsed_seconds' in log and 'command' in log
    assert 'Correctness timeout:' in capsys.readouterr().out


def test_benchmark_timeout_has_round_and_variant(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(player_benchmark.subprocess, 'run', timeout)
    raw = tmp_path / 'raw/public_case.round2.json'
    with pytest.raises(subprocess.TimeoutExpired):
        player_benchmark.run_case(tmp_path, 'baseline', {'case_id': 'public_case', 'N': 1, 'D': 32, 'S': 1},
                                  tmp_path / 'input', tmp_path / 'output', raw, 5, 20)
    report = json.loads(raw.with_suffix('.timeout.json').read_text())
    assert report['round'] == 2
    assert report['variant'] == 'baseline'
    assert report['timeout_seconds'] == 300
    assert report['elapsed_seconds'] >= 0
    assert report['command'][0] == 'bash'
    assert 'Benchmark timeout:' in capsys.readouterr().out


def test_missing_build_outputs_have_actionable_error(tmp_path, monkeypatch):
    monkeypatch.setattr(player_benchmark, '_source_fingerprint', lambda *args: 'source')
    monkeypatch.setattr(player_benchmark.subprocess, 'run', lambda *args, **kwargs: None)
    with pytest.raises(RuntimeError, match='without installing both the runner'):
        player_benchmark.build_if_changed(tmp_path, 'solution', True, None)


def test_robustness_timeout_is_published_as_json(tmp_path, monkeypatch):
    from tools.correctness import runtime_robustness
    monkeypatch.setattr(runtime_robustness.subprocess, 'run', timeout)
    with pytest.raises(subprocess.TimeoutExpired):
        runtime_robustness._run_process(['bash', 'solution/run.sh'], cwd=tmp_path, timeout=120)
    report = json.loads((tmp_path / 'artifacts/logs/robustness_timeout.json').read_text())
    assert report['stdout'] == 'partial\n'
    assert report['timeout_seconds'] == 120
    assert report['phase'] == 'robustness'


@pytest.mark.parametrize('kind', ['benchmark', 'robustness'])
def test_diagnostic_disk_failure_preserves_timeout(tmp_path, monkeypatch, kind):
    from pathlib import Path
    from tools.correctness import runtime_robustness
    def no_space(*args, **kwargs):
        raise OSError(28, 'No space left on device')
    monkeypatch.setattr(Path, 'write_text', no_space)
    monkeypatch.setattr(subprocess, 'run', timeout)
    with pytest.raises(subprocess.TimeoutExpired):
        if kind == 'benchmark':
            player_benchmark.run_case(tmp_path, 'solution', {'case_id': 'case', 'N': 1, 'D': 32, 'S': 1},
                                      tmp_path / 'input', tmp_path / 'output', tmp_path / 'raw/case.round1.json', 5, 20)
        else:
            runtime_robustness._run_process(['bash', 'solution/run.sh'], cwd=tmp_path, timeout=120)
