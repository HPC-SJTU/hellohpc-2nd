"""Snapshot and launch one evaluation on reserved CPU cores within a NUMA node."""
from __future__ import annotations
import argparse
import datetime
import fcntl
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import yaml
from .parallel_agents import discover_numa_cpus, WORKER_COUNT
from .submission_surface import require_eligible_surface, REASONING_EFFORT

ROOT = Path(__file__).resolve().parents[2]


def acquire_lock(path: Path, *, shared=False):
    descriptor = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(descriptor, (fcntl.LOCK_SH if shared else fcntl.LOCK_EX) | fcntl.LOCK_NB)
    except BaseException:
        os.close(descriptor)
        raise
    return descriptor


def reserve_node(work: Path, topology, requested=None, *, shared_node=False):
    """Reserve a whole node, or a disjoint two-core slot with a shared node lease.

    The shared gate blocks old launchers/debug tools that take its exclusive
    lock. It does not serialize these node-isolated evaluations.
    """
    work.mkdir(parents=True, exist_ok=True)
    gate = acquire_lock(work / 'evaluation.lock', shared=True)
    try:
        locks = work / 'evaluation-node-locks'; locks.mkdir(exist_ok=True)
        nodes = sorted(topology) if requested is None else [requested]
        for node in nodes:
            cpus = tuple(topology.get(node, ()))
            if len(cpus) < WORKER_COUNT:
                continue
            try:
                lease = acquire_lock(locks / f'numa-{node}.lock', shared=shared_node)
            except BlockingIOError:
                continue
            if not shared_node:
                return node, cpus, (gate, lease)
            # One core per agent in each evaluation slot.
            for offset in range(0, len(cpus), WORKER_COUNT):
                slot = cpus[offset:offset + WORKER_COUNT]
                if len(slot) < WORKER_COUNT:
                    continue
                try:
                    slot_lease = acquire_lock(locks / f'numa-{node}-slot-{offset // WORKER_COUNT}.lock')
                except BlockingIOError:
                    continue
                return node, slot, (gate, lease, slot_lease)
            os.close(lease)
        raise RuntimeError('No requested NUMA node is available for this evaluation')
    except BaseException:
        os.close(gate)
        raise


def prepare_project(run: Path, cpu: int, *, empty_skill: bool = False):
    project = run / 'project'; project.mkdir()
    for name in ('src', 'submission.yaml', 'SKILL.md', 'config.yaml', 'problem.yaml', 'evaluate.py',
                 'submit.py', 'requirements-scoring.txt', 'references'):
        source = ROOT / name
        if not source.exists():
            continue
        if source.is_dir():
            shutil.copytree(source, project / name, ignore=shutil.ignore_patterns('__pycache__'))
        else:
            shutil.copy2(source, project / name)
    if empty_skill:
        (project / 'SKILL.md').write_text(
            '---\nname: cpu-hpc-skill\ndescription: Empty baseline.\n---\n',
            encoding='utf-8'
        )
        if (project / 'references').exists():
            shutil.rmtree(project / 'references')
    path = project / 'config.yaml'
    cfg = yaml.safe_load(path.read_text())
    cfg['pinning']['cores'] = str(cpu)
    cfg['pinning']['omp_num_threads'] = 1
    path.write_text(yaml.safe_dump(cfg, sort_keys=False))
    require_eligible_surface(project, repository_layout=True)
    return project


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--numa-node', type=int, default=None)
    parser.add_argument('--exclusive-node', action='store_true', help='Reserve the entire NUMA node')
    parser.add_argument('--run-dir', type=Path)
    parser.add_argument('--empty-skill', action='store_true',
                        help='Use a metadata-only skill with no references in this run snapshot')
    parser.add_argument('--wait-for-node', action='store_true',
                        help='Queue until a NUMA node is available (up to 24 hours)')
    args = parser.parse_args()
    if not os.environ.get('SJTU_API_KEY'):
        parser.error('Load the private runtime environment before launching')
    (ROOT / 'work').mkdir(parents=True, exist_ok=True)
    run = args.run_dir.resolve() if args.run_dir else Path(tempfile.mkdtemp(prefix='evaluation-parallel-', dir=ROOT/'work'))
    if args.run_dir:
        run.mkdir(parents=True, exist_ok=False)
    # Freeze inputs before waiting so queued runs cannot pick up later edits.
    project = prepare_project(run, 0, empty_skill=args.empty_skill)
    surface = require_eligible_surface(project, repository_layout=True)
    queued_at = datetime.datetime.now(datetime.timezone.utc).isoformat()
    (run/'queue.json').write_text(json.dumps(dict(pid=os.getpid(), queued_at=queued_at,
        model=surface.model, reasoning_effort=REASONING_EFFORT, status='waiting'), indent=2)+'\n')
    deadline = time.monotonic() + 24 * 3600
    while True:
        try:
            node, cpus, locks = reserve_node(ROOT/'work', discover_numa_cpus(), args.numa_node,
                                            shared_node=not args.exclusive_node)
            break
        except (BlockingIOError, RuntimeError):
            if not args.wait_for_node or time.monotonic() >= deadline:
                raise
            time.sleep(10)
    cfg_path = project/'config.yaml'
    cfg = yaml.safe_load(cfg_path.read_text()); cfg['pinning']['cores'] = str(cpus[0])
    cfg_path.write_text(yaml.safe_dump(cfg, sort_keys=False))
    (run/'queue.json').write_text(json.dumps(dict(pid=os.getpid(), queued_at=queued_at,
        model=surface.model, reasoning_effort=REASONING_EFFORT, status='started'), indent=2)+'\n')
    (run/'evidence').mkdir()
    now = datetime.datetime.now(datetime.timezone.utc)
    timing_dir = ROOT/'work'/'evaluation-timing-locks'
    timing_dir.mkdir(exist_ok=True)
    timing_lock = timing_dir/f'numa-{node}.lock'
    descriptor = os.open(timing_lock, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    os.close(descriptor)
    metadata = dict(shared_numa=not args.exclusive_node, timing_lock=str(timing_lock), pid=os.getpid(), started_at=now.isoformat(), model='sjtu/'+surface.model,
                    reasoning_effort=REASONING_EFFORT, agents=WORKER_COUNT, skill_variant="empty" if args.empty_skill else "current", numa_node=node, reserved_cpus=list(cpus),
                    formal_scoring_cpu=cpus[0], skill_tokens=surface.estimated_tokens,
                    skill_sha256=surface.sha256, operator=surface.operator,
                    status_check_due=(now+datetime.timedelta(minutes=30)).isoformat(),
                    status_check_scheduled=False, scheduler_unavailable=True)
    (run/'launch.json').write_text(json.dumps(metadata, indent=2)+'\n')
    print(f'Results: {run}\nReserved NUMA node {node}; CPUs {cpus[0]}–{cpus[-1]}; formal CPU {cpus[0]}', flush=True)
    subprocess.run([sys.executable, '-m', 'hellohpc', 'validate'], cwd=project, check=True)
    env = dict(os.environ, CHEATSHEET_TRUSTED_OUTPUT_ROOT=str(run/'evidence'),
               CHEATSHEET_NODE_TIMING_LOCK=str(timing_lock))
    # Inherited descriptors keep reservations alive through the complete run.
    for descriptor in locks:
        os.set_inheritable(descriptor, True)
    os.chdir(project)
    command = ['numactl', '--physcpubind='+','.join(map(str, cpus)), '--membind='+str(node),
               sys.executable, '-m', 'hellohpc', 'test', '--output', str(run/'result.json'), '--keep-workspace']
    os.execvpe(command[0], command, env)


if __name__ == '__main__':
    main()
