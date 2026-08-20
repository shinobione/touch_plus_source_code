# Phase 2B.8 — physical smoke 2026-08-20

Physical unit: `0101007379`

PR: #9  
Branch: `revival/phase2b-fingertip-3d`

## Verdict

**PARTIAL PASS / SAFETY GATE PASS / IDENTITY RELIABILITY STILL BLOCKED / DO NOT MERGE**

The 2B.8 temporal identity architecture materially improves safety on the real Touch+, but it does not yet meet the merge gate for anatomically reliable fingertip identity.

Raw personal video is intentionally **not committed**. This note records only derived engineering observations.

## What physically passed

### Background / no-hand

The learned-background flow remains healthy:

```text
background=NOT_READY
-> background=LEARNING
-> background=READY
```

A clear learned scene was observed with:

```text
no palm-supported hand
changed_cells=0
identity=UNKNOWN/LOW
stereo=NOT_RUN
```

### Identity rejection happens before stereo

2B.8 physically exercised the new rejection classes, including:

- `tip-jump-reject`
- `palm-temporal-reject`
- `branch-association-reject`
- `ambiguous-branch`
- `no-finger-like-branch`

A representative real rejection showed approximately:

```text
reason=tip-jump-reject
tip_residual=107.9
stereo_confidence=NOT_RUN
fingertip=UNKNOWN
```

This is a critical PASS relative to 2B.7. The old failure class could allow a materially different 2D anatomical candidate to reach a strong stereo measurement. In 2B.8, the bad identity can die upstream and never receive metric legitimacy.

### Correct lock exists on real hardware

During a clear single-index pose around the later middle portion of the bench, one observed state was approximately:

```text
identity_state=LOCKED
identity_confidence=HIGH
stereo_confidence=HIGH
tip_pixel=151,181
branch_id=47
support=10
final_confidence=HIGH
```

Visual review placed this candidate close to the real distal index tip.

Therefore 2B.8 is not a purely synthetic architecture: it can acquire and lock a physically plausible index identity.

## What still fails

### Recall / continuity is too intermittent

A visually obvious single extended index still spends too much time in safe rejection states such as:

```text
no-finger-like-branch
identity-acquiring
ambiguous-branch
stereo=NOT_RUN
```

The safe UNKNOWN behavior is preferable to a false finite fingertip, but the current recall is too low for a usable interaction layer.

### Anatomical correctness is not yet certified for every finite output

The catastrophic 2B.7 re-election pattern is substantially reduced, but this physical smoke does not justify claiming that every MEDIUM/HIGH finite output is attached to the same distal anatomical index through all tested motion.

The project rule remains binding:

> one anatomically wrong finite MEDIUM/HIGH fingertip is a blocker

and:

> UNKNOWN is acceptable when identity is uncertain

## Boundary conclusion

2B.8 successfully separates:

- 2D identity confidence;
- stereo refinement confidence;
- final metric publication.

The safety architecture is worth retaining.

However, continuing to add endpoint/branch heuristics is no longer the preferred next move. The remaining problem is anatomical recognition under real Touch+ appearance.

## Next boundary: Phase 2B.9A

Evaluate a lightweight modern **2D hand landmark oracle** independently on Touch+ imagery before integrating any DNN dependency into the Win32 Etron runtime.

Selected evaluation candidate:

- OpenCV Zoo MediaPipe palm detector;
- OpenCV Zoo MediaPipe HandPose;
- 21 2D hand landmarks;
- `INDEX_FINGER_TIP` only for anatomical evidence;
- model-relative Z is ignored.

Policy under evaluation:

```text
geometry plausible + landmark agrees
    -> evidence for stronger identity

geometry plausible + landmark strongly disagrees
    -> future live policy should return UNKNOWN

landmark only
    -> diagnostic only

metric XYZ
    -> Touch+ stereo/Q only
```

Phase 2C touch/click remains blocked.
