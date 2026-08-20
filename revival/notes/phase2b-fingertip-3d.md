# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.9C.2 — frame-synchronous live anatomy fusion

Physical unit: `0101007379`.

Current status:

**2B.9C.1 LIVE MAJOR PROGRESS / WRONG FINITE HIGH ON FAST POSE TRANSITION / 2B.9C.2 IMPLEMENTED CANDIDATE / DO NOT MERGE**

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm object measured about 54–55 mm. Phase 2B sits on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

Binding evidence:

- `revival/notes/phase2b7-physical-smoke-2026-08-20.md`
- `revival/notes/phase2b8-physical-smoke-2026-08-20.md`
- `revival/notes/phase2b9-landmark-oracle-evaluation.md`
- `revival/notes/phase2b9b-landmark-guided-distal.md`
- `revival/notes/phase2b9b-physical-smoke-2026-08-20.md`
- `revival/notes/phase2b9b1-roi-reacquisition.md`
- `revival/notes/phase2b9c1-live-anatomy-sidecar.md`
- `revival/notes/phase2b9c1-physical-smoke-2026-08-20.md`
- `revival/notes/phase2b9c2-frame-sync-anatomy.md`
- `revival/notes/phase2b8-landmark-fallback-evaluation.md`

Raw user physical photos/videos are intentionally not committed.

## Accepted lower layers — do not loosen to fix identity

The following remain accepted and separate from Phase 2B identity work:

1. persistent Touch+ capture after Etron `SWUnlock`;
2. per-device stereo calibration `K/D/R/T/P/Q`;
3. Phase 1C robust stereo matching and metric Q reprojection;
4. Phase 2A surface transform `surface/0101007379.json`;
5. V5 learned-background appearance silhouette;
6. V6 physical-support bounding.

Do **not** change camera calibration, Q, the accepted surface transform, or stereo gates merely to improve fingertip recall.

## Physical progression

1. **2B.1 wiring PASS** — tracker runtime, `T`, surface-model load and heartbeat work.
2. **2B.1 foreground FAIL** — real scene produced ~19k–26k-cell pseudo-hands.
3. **2B.2 hardened segmentation PARTIAL PASS** — giant foreground continent removed; components became physically plausible sizes.
4. **2B.2 fingertip identity FAIL** — finite candidates landed on wrist/back-of-hand.
5. **2B.3 top-entry geodesic FAIL** — no-hand pseudo-hands and jumping endpoints remained.
6. **2B.4 learned background PARTIAL PASS** — `NOT_READY -> LEARNING -> READY` works and no-hand rejection improved dramatically.
7. **2B.5 appearance silhouette PARTIAL PASS / fingertip FAIL** — low-texture distal skin became visible independently of dense depth.
8. **2B.6 support-bounded skeleton PARTIAL PASS / fingertip FAIL** — ambiguity improved, but anatomically wrong candidates could still receive HIGH stereo confidence.
9. **2B.7 palm-core branch PARTIAL PASS / fingertip FAIL** — SCOPA-inspired palm-first decomposition improved diagnostics, but a single index could still be re-elected as materially different pixels across adjacent frames.
10. **2B.8 temporal palm/branch identity PHYSICAL PARTIAL PASS** — large identity jumps can now be rejected before stereo, and correct locks exist, but recall/continuity remain too intermittent for merge.
11. **2B.9A OpenCV Zoo landmark probe RUNTIME PASS / EXACT-TIP ORACLE FAIL** — hand landmarks run well on Touch+ imagery, but raw landmark 8 can be confidently too proximal.
12. **2B.9B landmark-guided distal projection PHYSICAL PARTIAL PASS** — all five published guided points in the first physical dataset were plausible, but recall was only 5/10.
13. **2B.9B.1 silhouette ROI reacquisition PHYSICAL/OFFLINE DATASET PASS** — same physical dataset improved to 8/10 guided distal, 0 observed wrong published tips, 2 safe rejects.
14. **2B.9C.1 live anatomical sidecar PHYSICAL PARTIAL PASS / HARD FAIL** — live full-frame/ROI anatomy and anatomy-only rescue work, but a one-frame-old result during fast pose rotation produced a wrong finite HIGH fingertip.
15. **2B.9C.2 frame-synchronous anatomy IMPLEMENTED CANDIDATE** — source/current silhouette + palm snapshots align age>0 anatomy to the current frame, then require a current distal boundary before fusion/stereo.

## Critical identity lessons

Representative 2B.7 physical sequence:

```text
263,173 -> 265,177 -> 189,87
```

The downstream stereo matcher could report MEDIUM/HIGH for these selected pixels, but the imagery did not support treating the final jump as the same distal index.

2B.9C.1 exposed the asynchronous equivalent:

```text
source frame N: valid distal anatomy
runtime frame N+1: hand rotates/foreshortens
old tip still lies inside current hand silhouette
2B.9C.1 anatomy-only gate passes
stereo HIGH measures the wrong current anatomical pixel
```

Representative blocker from the physical live smoke:

```text
anatomy=GUIDED_DISTAL/LOCKED/HIGH
src=ROI_3
age=1
fusion=ANATOMY_ONLY/HIGH
stereo=HIGH
tip_pixel=435,167
support=6
final_confidence=HIGH
XYZ=(61.1,-92.4,H=86.0) mm
```

Visual review showed that `435,167` was no longer the current distal index.

Therefore both rules remain binding:

> **stereo refinement confidence != fingertip identity confidence**

> **wrong finite/HIGH fingertip = BLOCKER; UNKNOWN is acceptable when identity is uncertain**

## Phase 2B.8 safety architecture retained

```text
learned background / appearance silhouette
        |
        v
V6 physical support bounding
        |
        v
palm observation + validation
        |
        v
finger-like branch descriptors
        |
        v
temporal palm persistence
        |
        v
persistent branch association
        |
        v
2D jump rejection after palm-motion compensation
        |
        v
UNKNOWN -> ACQUIRING -> LOCKED
        |
        +---- unstable / ambiguous ----> UNKNOWN
```

Only identity that survives the active anatomy/fusion gate may reach stereo.

## Phase 2B.9A — exact landmark endpoint is not ground truth

The official OpenCV Zoo MediaPipe PalmDet + HandPose stack was tested offline on Touch+ LEFT imagery.

Binding LEFT-eye physical subset:

```text
frames                         : 10
hand landmarks found           : 8 / 10
index_extended_2d              : 7 / 8 detected
ORACLE_NON_INDEX_POSE          : 1 / 10
ORACLE_UNAVAILABLE             : 2 / 10
median detected hand confidence: 0.9883
minimum detected confidence    : 0.9271
```

Visual review showed several confident raw `INDEX_FINGER_TIP` landmarks materially too proximal. Therefore:

- raw landmark 8 is diagnostic only;
- model confidence does not certify endpoint correctness;
- model Z is never Touch+ metric Z;
- exact landmark-tip distance cannot by itself publish or veto identity.

## Phase 2B.9B / 2B.9B.1 — anatomy direction + Touch+ distal boundary

The useful model evidence is the index chain and distal direction:

```text
INDEX_MCP -> INDEX_PIP -> INDEX_DIP -> distal direction
                         |
                         v
Touch+ appearance silhouette
                         |
                         v
continuous distal corridor
                         |
                         v
GUIDED_DISTAL
```

2B.9B physical result:

```text
GUIDED_DISTAL correct : 5 / 10
wrong guided distal   : 0 / 10
safe reject/unavail   : 5 / 10
```

2B.9B.1 added silhouette-ROI landmark reacquisition. Replaying the same physical dataset produced:

```text
GUIDED_DISTAL                : 8 / 10
visually plausible published : 8 / 8
wrong guided tips observed   : 0 / 8
GUIDED_REJECTED              : 2 / 10
GUIDED_UNAVAILABLE           : 0 / 10
```

ROI reacquisition rescued pairs 007, 008 and 010. Pair 011 remains a broad fist/knuckle fail-closed regression.

## Phase 2B.9C.1 — live sidecar result

OpenCV/ONNX stays outside the Win32 Etron process. Named shared memory carries only LEFT grayscale, Touch+ silhouette and a 2D result. Touch+ stereo/Q remains the only metric XYZ source.

The physical live smoke proved:

- launcher paths with spaces PASS;
- Python sidecar and shared-memory IPC PASS;
- full-frame anatomy PASS;
- ROI reacquisition works live;
- plausible `GEOMETRY+ANATOMY` publication exists;
- plausible `ANATOMY_ONLY` publication exists;
- disagreement/reject/jump/palm gates fail closed.

But a fast pose transition produced the wrong finite HIGH age-1 anatomy-only result documented above. 2B.9C.1 is therefore **DO NOT MERGE**.

## Phase 2B.9C.2 — frame-synchronous anatomy

2B.9C.2 retains a short history of:

```text
frame_id
current Touch+ selected silhouette
V8 palm center
V8 palm radius
```

An age>0 anatomy observation must now resolve its source frame and survive:

1. source/current palm validity;
2. bounded palm translation;
3. bounded palm scale change;
4. aligned silhouette shape overlap;
5. source-tip transport into the current frame by palm translation/scale;
6. **current-distal validation** on the transported point.

Current-distal validation requires the point to be near the current silhouette boundary, with silhouette support inward along the anatomical axis and clear space outward. Merely being somewhere inside the hand is no longer enough.

Possible sync diagnostics include:

```text
CURRENT
MOTION_COMPENSATED
MISSING_SOURCE_FRAME
PALM_INVALID
PALM_MOTION_TOO_LARGE
PALM_SCALE_CHANGED
SHAPE_CHANGED
TIP_NOT_CURRENT_DISTAL
TOO_OLD
```

Relevant fail-closed fusion reasons:

```text
anatomy-stale-motion
anatomy-not-current-distal
anatomy-only-too-old
anatomy-only-sync-shape-weak
```

`ANATOMY_ONLY` is deliberately stricter:

- age 0 can publish after normal gates;
- age 1 can publish only after motion compensation + current-distal validation + strong shape overlap;
- age >1 cannot publish anatomy-only;
- synchronized age <=2 anatomy may still participate in `GEOMETRY+ANATOMY` agreement because current V8 geometry supplies an independent current-frame check.

## 2B.9C.2 physical regression encoded in CI

The synthetic fusion test models the actual failure class:

```text
old 2B.9C.1 gate:
old tip still near current hand = PASS

new 2B.9C.2 gate:
same old tip no longer current distal
-> TIP_NOT_CURRENT_DISTAL / reject
-> UNKNOWN
-> stereo must not legitimize it
```

The test also preserves a safe age-1 translation case where the same hand simply moves slightly and the compensated fingertip remains a real distal boundary.

Synthetic/CI success is necessary but not physical acceptance.

## Hard ownership

```text
metric_z_source          = TOUCHPLUS_STEREO_ONLY
raw landmark 8           = DIAGNOSTIC ONLY
OpenCV/ONNX in Etron EXE = NO
K/D/R/T/P/Q              = UNCHANGED
Phase 2A surface frame   = UNCHANGED
stereo matcher           = UNCHANGED
```

## Physical gate for 2B.9C.2

The next live smoke must retain the previously good static/slow cases and deliberately stress quick orientation changes, especially right/left -> frontal/foreshortened.

Required:

1. plausible `GEOMETRY+ANATOMY` and ROI/anatomy-only fingertip cases still occur;
2. stable age-1 translation may show `MOTION_COMPENSATED` and remain valid;
3. rapid pose changes prefer `TIP_NOT_CURRENT_DISTAL`, `SHAPE_CHANGED`, another sync reject, or `UNKNOWN`;
4. no finite MEDIUM/HIGH result may land on a non-distal hand pixel;
5. HIGH stereo must never override failed identity/synchronization.

## Diagnostics / merge rule

Runtime banner:

```text
[TRACK] PHASE 2B.9C.2 RUNTIME ACTIVE | tracker=V8+FRAME-SYNC-ANATOMY
```

Overlay:

```text
cyan    = V8 palm
white   = V8 geometry candidate
magenta = frame-synchronized landmark-guided distal candidate
```

PR #9 remains **Draft / DO NOT MERGE**.

Phase 2C touch/click remains blocked until real hardware fingertip identity is reliable.
