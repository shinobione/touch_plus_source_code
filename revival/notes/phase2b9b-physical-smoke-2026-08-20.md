# Phase 2B.9B — physical guided-distal smoke 2026-08-20

Physical unit: `0101007379`

PR: #9  
Branch: `revival/phase2b-fingertip-3d`

## Verdict

**DISTAL PROJECTION QUALITY PASS WHEN PUBLISHED / GLOBAL RECALL FAIL / DO NOT MERGE**

The physical 2B.9B dataset used one persistent capture session with pair 001 as the clean LEFT-eye background and pairs 002–011 as ten single-index poses.

Raw user images remain outside the repository. This note records only derived engineering observations.

## Binding LEFT-eye result

```text
single-index poses evaluated : 10
GUIDED_DISTAL                : 5 / 10
visually correct guided tips : 5 / 5 published
wrong guided tips observed   : 0 / 5 published
GUIDED_UNAVAILABLE           : 3 / 10
GUIDED_REJECTED              : 2 / 10
global >=8/10 gate           : FAIL
```

Per-pose status:

```text
pair 002  GUIDED_REJECTED
pair 003  GUIDED_DISTAL      correct
pair 004  GUIDED_DISTAL      correct
pair 005  GUIDED_DISTAL      correct
pair 006  GUIDED_DISTAL      correct
pair 007  GUIDED_UNAVAILABLE
pair 008  GUIDED_UNAVAILABLE
pair 009  GUIDED_DISTAL      correct
pair 010  GUIDED_UNAVAILABLE
pair 011  GUIDED_REJECTED
```

## What physically passed

All five published guided distal points landed on or very near the visible distal index boundary during visual review.

The projection materially corrected the 2B.9A proximal-tip failure class. Observed extension beyond the raw model landmark 8 was approximately:

```text
pair 003 : +37.2 px
pair 004 : +10.4 px
pair 005 : +36.4 px
pair 006 : +16.1 px
pair 009 : +11.8 px
median   : +16.1 px
```

This supports the core 2B.9B hypothesis:

> MediaPipe can be useful for index anatomy/direction even when raw landmark 8 is not the true distal endpoint.

The Touch+ appearance silhouette is a better owner of the visible terminal boundary.

## What still fails

The global acceptance threshold was at least 8/10 correct guided distal poses. Physical result is 5/10, so 2B.9B does **not** advance to live 2B.9C yet.

The remaining failures separate into two upstream classes.

### Full-frame landmark recall

Pairs 007, 008 and 010 returned no reliable hand landmarks despite a visibly present single-index pose.

This is no longer a distal-projection error. The projection never received anatomical evidence.

The far/small pair 008 is especially relevant: background-difference analysis still extracts a compact changed hand component, so the Touch+ image contains useful localization evidence even when full-frame PalmDet misses.

### Pose gate / foreshortening

Pairs 002 and 011 found a hand but failed the conservative `index_extended_2d` gate.

Pair 011 is an important safety case. Its model index axis points through a broad fist/knuckle region rather than a narrow distal finger corridor. Derived cross-section analysis gives a width around **9.4 phalanx scales**, versus roughly **0.5–1.3** on successful guided fingers. Therefore 011 must remain rejected unless reacquisition produces materially better anatomy.

Pair 002 has an internally inconsistent distal phalanx chain in the full-frame inference, so simple threshold relaxation is not justified.

## Boundary conclusion

2B.9B turns the problem into a cleaner one:

```text
published distal placement : physically promising / 0 wrong observed
model/pose recall          : insufficient
```

The next step must target reacquisition/pose evidence without weakening the distal safety gate.

## Next boundary: Phase 2B.9B.1

Use the same already-captured physical dataset. No new hardware capture is required initially.

Planned changes:

1. derive a changed-hand component from LEFT + pair-001 background before landmark inference;
2. estimate an appearance palm core using a distance transform;
3. retry PalmDet/HandPose on several palm-centered square ROIs when full-frame guidance does not publish;
4. remap ROI screen landmarks back to full 640x480 coordinates;
5. allow a foreshortened perspective pose only when a coherent index axis traverses a **finger-width** Touch+ silhouette corridor;
6. reject broad fist/knuckle paths rather than merely relaxing the old wrist-distance condition;
7. if multiple strong reacquisition candidates disagree materially, return `GUIDED_REJECTED`.

Hard invariants remain:

```text
exact_tip_oracle_policy = DISABLED_AFTER_2B9A_PHYSICAL_FAIL
metric_z_source         = TOUCHPLUS_STEREO_ONLY
```

No OpenCV/ONNX runtime enters the Win32 Etron tracker in this slice.

## Merge rule

PR #9 remains **Draft / DO NOT MERGE**.

Phase 2C remains blocked.
