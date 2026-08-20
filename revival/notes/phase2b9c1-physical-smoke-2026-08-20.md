# Phase 2B.9C.1 — physical live smoke

Date: 2026-08-20  
Physical unit: `0101007379`  
Binding tested head before this note: `5cb809fa0ae35ea13965efdd974b2666979c8019`

## Verdict

**LIVE MAJOR PROGRESS / WRONG FINITE HIGH ON FAST POSE TRANSITION / DO NOT MERGE**

Raw personal video/frames remain outside the repository. This note records only derived engineering observations from the local physical smoke.

## What passed physically

2B.9C.1 proved that the live sidecar architecture is real, not just an offline probe:

- the fixed PowerShell launcher works from a normal Windows path containing spaces;
- the Python/OpenCV Zoo sidecar stays online next to the Win32 Etron runtime;
- named shared memory frame/result IPC works live;
- learned background can reach READY and the empty scene can stay no-hand;
- full-frame landmark anatomy works live;
- silhouette ROI reacquisition works live;
- `GEOMETRY+ANATOMY` agreement can publish a plausible real fingertip;
- `ANATOMY_ONLY` can rescue a plausible real fingertip when V8 geometry is acquiring/unknown;
- disagreement/reject/jump/palm gates can fail closed to `UNKNOWN` before stereo;
- the model still never owns metric Z; accepted Touch+ stereo/Q remains the only XYZ source.

Representative successful live observations included:

```text
~36 s: geometry and anatomy agree around the visible distal index
~38 s: ANATOMY_ONLY / HIGH + stereo HIGH on a plausible distal index
~39 s: ROI_2 live reacquisition + ANATOMY_ONLY / HIGH + stereo HIGH
```

The important physical conclusion is that the offline 2B.9B.1 ROI idea survives into the live pipeline.

## Binding blocker

During a fast transition from a right-oriented index toward a much more frontal / foreshortened pose, the runtime produced a finite HIGH result from a one-frame-old anatomy observation:

```text
anatomy=GUIDED_DISTAL
anatomy_track=LOCKED/HIGH
src=ROI_3
age=1
fusion=ANATOMY_ONLY/HIGH
stereo=HIGH
tip_pixel=435,167
support=6
final_confidence=HIGH
XYZ=(61.1,-92.4,H=86.0) mm
```

Visual review of the physical LEFT image showed that `435,167` was no longer the current distal index. The current true distal was materially elsewhere; the old anatomy point had become a non-distal hand/knuckle/palm-region pixel.

This is a hard blocker under the project rule:

> **wrong finite/HIGH fingertip = BLOCKER**

## Root cause

The sidecar is asynchronous. 2B.9C.1 stores the source `frame_id`, but fusion only checked whether the returned point was still near the **current hand silhouette**.

That is insufficient during a fast pose change:

```text
sidecar processes frame N distal tip
           |
           v
runtime advances to frame N+1
hand rotates / foreshortens
           |
           v
old tip still lies somewhere inside current hand silhouette
           |
           v
2B.9C.1 silhouette-near gate passes
           |
           v
ANATOMY_ONLY can reach stereo
           |
           v
stereo correctly measures the WRONG current anatomical pixel
```

The bug is therefore not stereo confidence, calibration, Q, surface transform, or landmark model loading. It is **frame synchronization / current-distal validation before fusion**.

## Safety behavior that remains valuable

The same smoke also showed fail-closed cases such as:

```text
geometry-anatomy-disagree -> UNKNOWN
anatomy-reject             -> UNKNOWN
anatomy-jump-reject        -> UNKNOWN
geometry-jump-reject       -> UNKNOWN
palm-temporal-reject       -> UNKNOWN
stereo LOW                 -> no finite XYZ
```

These protections remain and must not be loosened to recover recall.

## Next boundary

**Phase 2B.9C.2 — frame-synchronous anatomy fusion**

Required behavior:

1. retain a short history of the Touch+ silhouette + V8 palm for source frames;
2. resolve the returned anatomy `frame_id` against that history;
3. compensate source tip position by source->current palm translation/scale only when motion remains plausible;
4. reject excessive palm motion, palm scale changes, or silhouette-shape changes;
5. require the compensated point to remain a **current distal boundary**, not merely somewhere near the hand;
6. keep `ANATOMY_ONLY` stricter than geometry+anatomy agreement;
7. `age>1` anatomy-only rescue must fail closed;
8. any failed synchronization becomes `UNKNOWN / stereo=NOT_RUN`.

No changes are authorized by this finding to accepted `K/D/R/T/P/Q`, Phase 2A surface frame, persistent capture, or hardened stereo matching.

## Merge rule

PR #9 remains **Draft / DO NOT MERGE**.

Phase 2C remains blocked.
