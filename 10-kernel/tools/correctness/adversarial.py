"""Deterministic legal inputs targeting independent numerical and layout hazards."""
from __future__ import annotations

import numpy as np


def generate_adversarial(spec):
    rng = np.random.default_rng(int(spec['seed']))
    lengths = np.tile(np.asarray(spec['lengths'], dtype=np.int64),
                      int(spec.get('length_repeats', 1)))
    d, s, n = int(spec['D']), lengths.size, int(lengths.sum())
    epsilon = float(spec.get('epsilon', 1e-5))
    if (d not in (32, 48, 64, 80, 96, 128, 160, 192, 256)
            or not 1 <= s <= 8192 or np.any(lengths < 1)
            or np.any(lengths > 16384) or n > 1048576 or n * d > 16777216
            or s != int(spec['S']) or n != int(spec['N'])
            or not 3e-6 <= epsilon <= 1e-2):
        raise ValueError('adversarial input exceeds the published shape/epsilon contract')
    offsets = np.concatenate(([0], np.cumsum(lengths))).astype(np.int32)
    score = np.empty(n, dtype=np.float16)
    x = np.empty((n, d), dtype=np.float16)
    signs = np.where(np.arange(d) % 2, -1., 1.)
    scales = np.resize(np.array([1., 0.5, 0.125, 0.0078125]), d)
    family = spec['adversarial']
    for segment, (lo, hi) in enumerate(zip(offsets[:-1], offsets[1:])):
        lo, hi = int(lo), int(hi)
        count = hi - lo
        q, a = score[lo:hi], x[lo:hi]
        position = int(spec.get('position', 0)) % count
        q[:] = 0
        if family == 'distant_center':
            q[:] = -float(spec.get('gap', 0.1))
            q[position] = 0
            a[:] = (60032 * signs * scales).astype(np.float16)
            a[position] = (-65504 * signs * scales).astype(np.float16)
        elif family == 'signed_cancellation':
            a[:] = (np.float16(0.1) * signs).astype(np.float16)
            pairs = count // 3
            a[:pairs] = (65504 * signs * scales).astype(np.float16)
            a[pairs:2*pairs] = (-65504 * signs * scales).astype(np.float16)
        elif family == 'low_mass_tail':
            q[:] = -float(spec.get('gap', 24))
            q[position] = 0
            a[:] = (65504 * signs * scales).astype(np.float16)
            a[position] = (0.125 * signs).astype(np.float16)
        elif family == 'dominant':
            q[:] = -65504
            q[position] = 65504
            a[:] = rng.uniform(-65504, 65504, (count, d)).astype(np.float16)
            # Distinct features expose broadcasting one column over the output.
            a[position] = (np.arange(d) - d/2).astype(np.float16)
        elif family == 'tiny_spread':
            values = np.array([0., -0., 2**-24, -2**-24, 2**-14, -2**-14,
                               2**-10, -2**-10, 0.00390625], dtype=np.float16)
            a[:] = rng.choice(values, (count, d))
            q[:] = rng.uniform(-4, 4, count).astype(np.float16)
        elif family == 'sparse_ulp':
            centres = (60032 * signs * scales).astype(np.float16)
            a[:] = centres
            a[position] = (60000 * signs * scales).astype(np.float16)
            q[:] = -float(spec.get('gap', 0))
            q[position] = 0
        elif family == 'constant':
            values = np.array([0., -0., 2**-24, -2**-24, 1., -1.,
                               60032., -65504.], dtype=np.float16)
            a[:] = np.roll(np.resize(values, d), segment % d)
            q[:] = rng.uniform(-16, 16, count).astype(np.float16)
        elif family == 'finite_bits':
            # Sample sign, exponent (excluding 31), and mantissa independently.
            bits = rng.integers(0, 31 * 1024, (count, d), dtype=np.uint16)
            bits |= rng.integers(0, 2, (count, d), dtype=np.uint16) << 15
            a[:] = bits.view(np.float16)
            q[:] = rng.uniform(-16, 16, count).astype(np.float16)
        elif family == 'layout':
            a[:] = rng.uniform(-4, 4, (count, d)).astype(np.float16)
            a[:] *= np.resize(np.array([1, 2, 4, 8], np.float16), d)
            q[:] = rng.uniform(-8, 8, count).astype(np.float16)
        else:
            raise ValueError(f'unknown adversarial family: {family}')
        # Apply paired transformations to score/x, never just one tensor.
        order = spec.get('row_order', 'original')
        if order == 'reverse':
            q[:], a[:] = q[::-1].copy(), a[::-1].copy()
        elif order == 'shuffle':
            permutation = rng.permutation(count)
            q[:], a[:] = q[permutation], a[permutation]
        elif order != 'original':
            raise ValueError('unknown row_order')
        shift = float(spec.get('score_shift', 0))
        q[:] = (q.astype(np.float32) + shift).astype(np.float16)
    # Transform complete inputs deterministically; each transformed case is
    # judged against its own oracle, with the ordinary precision contract.
    if spec.get('column_order', 'original') == 'reverse':
        x = x[:, ::-1].copy()
    elif spec.get('column_order', 'original') != 'original':
        raise ValueError('unknown column_order')
    if spec.get('segment_order', 'original') == 'reverse':
        parts = [(score[lo:hi].copy(), x[lo:hi].copy())
                 for lo, hi in zip(offsets[:-1], offsets[1:])][::-1]
        score = np.concatenate([q for q, _ in parts])
        x = np.concatenate([a for _, a in parts])
        lengths = lengths[::-1].copy()
        offsets = np.concatenate(([0], np.cumsum(lengths))).astype(np.int32)
    elif spec.get('segment_order', 'original') != 'original':
        raise ValueError('unknown segment_order')
    if not np.isfinite(score).all() or not np.isfinite(x).all():
        raise ValueError('adversarial inputs must be finite FP16')
    metadata = dict(spec, N=n, S=s, dtype='float16',
                    distribution_name='adversarial/' + family,
                    max_segment_length=int(lengths.max()),
                    mean_segment_length=float(lengths.mean()), correctness_only=True)
    return score, x, offsets, metadata
