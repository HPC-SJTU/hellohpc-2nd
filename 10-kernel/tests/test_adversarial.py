"""Check legal coverage, analytic answers, and rejection of common wrong algorithms."""
from decimal import Decimal, localcontext
import json
from pathlib import Path

import numpy as np
import pytest

from tools.correctness.adversarial import generate_adversarial
from tools.correctness.check_correctness import compare_case
from tools.reference.reference import ragged_softmax_moments, write_inputs, write_outputs

DATA = Path(__file__).resolve().parents[1] / 'tools/data'


def spec(family, lengths=(17,33), d=32, **kwargs):
    return dict(case_id='unit', seed=220922, S=len(lengths), N=sum(lengths), D=d,
                lengths=list(lengths), adversarial=family, correctness_only=True,
                epsilon=3e-6, **kwargs)


@pytest.mark.parametrize('family', ['distant_center','signed_cancellation','low_mass_tail',
    'dominant','tiny_spread','sparse_ulp','constant','finite_bits','layout'])
def test_deterministic_legal_inputs(family):
    cfg=spec(family);a=generate_adversarial(cfg);b=generate_adversarial(cfg)
    for x,y in zip(a[:3],b[:3]):np.testing.assert_array_equal(x,y)
    assert a[0].dtype == a[1].dtype == np.float16
    assert a[2].dtype == np.int32
    assert np.isfinite(a[0]).all() and np.isfinite(a[1]).all()
    assert np.diff(a[2]).tolist() == cfg['lengths']


def test_distant_center_decimal_oracle():
    q,x,o,_=generate_adversarial(spec('distant_center',lengths=(4096,),gap=.001))
    with localcontext() as ctx:
        ctx.prec=70
        w=Decimal.from_float(float(q[1])).exp()
        z=1+4095*w
        a,b=Decimal.from_float(float(x[0,0])),Decimal.from_float(float(x[1,0]))
        mu=(a+4095*w*b)/z
        var=((a-mu)**2+4095*w*(b-mu)**2)/z
        r=1/(var+Decimal.from_float(float(np.float32(3e-6)))).sqrt()
    actual=ragged_softmax_moments(q,x,o,3e-6)
    assert actual[0][0,0] == np.float32(str(mu))
    np.testing.assert_allclose(actual[1][0,0],np.float32(str(r)),rtol=2e-7)


@pytest.mark.parametrize('order',['reverse','shuffle'])
def test_joint_row_permutation_preserves_math(order):
    a=generate_adversarial(spec('distant_center',lengths=(129,),gap=.1))
    b=generate_adversarial(spec('distant_center',lengths=(129,),gap=.1,row_order=order))
    for x,y in zip(ragged_softmax_moments(*a[:3],3e-6),ragged_softmax_moments(*b[:3],3e-6)):
        np.testing.assert_allclose(x,y,rtol=2e-7,atol=1e-6)


def test_equal_score_shift_changes_only_logsumexp():
    a=generate_adversarial(spec('signed_cancellation',lengths=(129,)))
    b=generate_adversarial(spec('signed_cancellation',lengths=(129,),score_shift=1024))
    ma,ra,la=ragged_softmax_moments(*a[:3],3e-6)
    mb,rb,lb=ragged_softmax_moments(*b[:3],3e-6)
    np.testing.assert_array_equal(ma,mb);np.testing.assert_array_equal(ra,rb)
    np.testing.assert_allclose(lb,la+1024,rtol=1e-7)


@pytest.mark.parametrize('fault',['epsilon','broadcast','offsets','drop_tail','missing_output'])
def test_checker_rejects_faulty_outputs(tmp_path,fault):
    family={'epsilon':'constant','broadcast':'layout','offsets':'layout',
            'drop_tail':'low_mass_tail','missing_output':'layout'}[fault]
    cfg=spec(family,lengths=(4096,33))
    q,x,o,meta=generate_adversarial(cfg)
    if fault=='epsilon':outputs=ragged_softmax_moments(q,x,o,1e-5)
    elif fault=='offsets':outputs=ragged_softmax_moments(q,x,np.array([0,33,4129],np.int32),3e-6)
    elif fault=='drop_tail':
        bad=q.copy();bad[q<0]=-65504
        outputs=ragged_softmax_moments(bad,x,o,3e-6)
    else:
        outputs=ragged_softmax_moments(q,x,o,3e-6)
        if fault=='broadcast':outputs[0][:]=outputs[0][:,:1]
        else:outputs[0][0,0]=np.nan
    write_inputs(tmp_path/'in',q,x,o,meta);write_outputs(tmp_path/'out',*outputs)
    assert not compare_case(tmp_path/'in',tmp_path/'out')['passed']


@pytest.mark.parametrize('override',[{'N':100},{'D':33},{'epsilon':1e-7},
                                    {'lengths':[0,50]},{'lengths':[16385,1],'N':16386}])
def test_invalid_specs_fail(override):
    cfg=spec('layout');cfg.update(override)
    with pytest.raises(ValueError):generate_adversarial(cfg)


def test_manifest_coverage_stays_correctness_only():
    cases=json.loads((DATA/'public_cases.json').read_text())['cases']
    additions=[c for c in cases if 'adversarial' in c]
    assert {c['adversarial'] for c in additions} == {
        'distant_center','signed_cancellation','low_mass_tail','dominant','tiny_spread',
        'constant','finite_bits','layout','sparse_ulp'}
    assert all(c['correctness_only'] and c.get('weight',0)==0 for c in additions)
    assert len([c for c in cases if not c.get('correctness_only',False)]) == 5
    for c in additions:
        q,x,o,m=generate_adversarial(c)
        assert x.size <= 16777216 and q.size == c['N']
        assert len(o)==c['S']+1 and np.diff(o).min()>=1 and np.diff(o).max()<=16384


def test_checker_rejects_uncentered_second_moment(tmp_path):
    q,x,o,meta=generate_adversarial(spec('sparse_ulp',lengths=(17,33)))
    mean,rstd,lse=ragged_softmax_moments(q,x,o,3e-6)
    for seg,(lo,hi) in enumerate(zip(o[:-1],o[1:])):
        a=x[lo:hi].astype(np.float32)
        naive=np.maximum(np.mean(a*a,axis=0)-np.mean(a,axis=0)**2,0)
        rstd[seg]=1/np.sqrt(naive+np.float32(3e-6))
    write_inputs(tmp_path/'in',q,x,o,meta)
    write_outputs(tmp_path/'out',mean,rstd,lse)
    assert not compare_case(tmp_path/'in',tmp_path/'out')['passed']


@pytest.mark.parametrize('change', ['column_order', 'segment_order'])
def test_transforms_preserve_permuted_reference(change):
    cfg = spec('finite_bits', lengths=(1, 17, 33), d=80)
    q, x, offsets, _ = generate_adversarial(cfg)
    expected = ragged_softmax_moments(q, x, offsets, 3e-6)
    tq, tx, to, _ = generate_adversarial(dict(cfg, **{change: 'reverse'}))
    actual = ragged_softmax_moments(tq, tx, to, 3e-6)
    for index, (a, b) in enumerate(zip(actual, expected)):
        if change == 'segment_order':
            b = b[::-1]
        elif index < 2:
            b = b[:, ::-1]
        np.testing.assert_array_equal(a, b)


@pytest.mark.parametrize('change', ['column_order', 'segment_order'])
def test_invalid_transform_rejected(change):
    with pytest.raises(ValueError):
        generate_adversarial(spec('layout', **{change: 'invalid'}))
