# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.9B — landmark-guided distal projection

Physical unit: `0101007379`.

Current status:

**2B.8 PHYSICAL SAFETY PARTIAL PASS / 2B.9A EXACT-TIP ORACLE FAIL / 2B.9B SYNTHETIC CANDIDATE / DO NOT MERGE**

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm object measured about 54–55 mm. Phase 2B sits on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

Binding evidence:

- `revival/notes/phase2b7-physical-smoke-2026-08-20.md`
- `revival/notes/phase2b8-physical-smoke-2026-08-20.md`
- `revival/notes/phase2b9-landmark-oracle-evaluation.md`
- `revival/notes/phase2b9b-landmark-guided-distal.md`
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
12. **2B.9B landmark-guided distal projection CANDIDATE** — model supplies index anatomy/direction; Touch+ appearance silhouette supplies the actual visible distal boundary. Physical validation pending.

## Critical identity lesson

Representative 2B.7 physical sequence:

```text
263,173 -> 265,177 -> 189,87
```

The downstream stereo matcher could report MEDIUM/HIGH for these selected pixels, but the imagery did not support treating the final jump as the same distal index.

Therefore:

> **stereo refinement confidence != fingertip identity confidence**

2B.8 moved identity continuity before stereo so a bad anatomical jump can become `UNKNOWN` before NCC/stereo gives it metric legitimacy.

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
        |
        v
ONLY LOCKED identity reaches stereo
        |
        v
identity_confidence + stereo_confidence
        |
        v
finite XYZ only if BOTH pass
```

### 2B.8 physical result

What passed on real hardware:

- learned empty scene can remain no-hand with `changed_cells=0`;
- `tip-jump-reject`, `palm-temporal-reject`, `branch-association-reject`, `ambiguous-branch` and `no-finger-like-branch` occur physically;
- representative large tip residual around `107.9` became `UNKNOWN` with `stereo_confidence=NOT_RUN`;
- a real single-index pose reached a plausible `LOCKED / HIGH identity / HIGH stereo` state.

What remains blocked:

- obvious index poses still spend too long in acquiring/unknown states;
- finite anatomical correctness is not yet certified across the whole motion set.

Binding project rule:

> one anatomically wrong finite MEDIUM/HIGH fingertip is a blocker

and:

> UNKNOWN is acceptable when identity is uncertain

## Phase 2B.9A — what the modern landmark probe actually proved

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

But visual review showed multiple confident raw `INDEX_FINGER_TIP` landmarks were materially **too proximal** on the index. One capture was around `0.998` confidence while still missing the real distal boundary.

Therefore the original hard-veto proposal is retired:

```text
raw landmark 8 far from V8 tip
    != automatic veto
```

A good Touch+ geometry/silhouette endpoint must not be rejected merely because a confident model endpoint is short.

From 2B.9A onward:

- raw landmark 8 is diagnostic only;
- model confidence does not certify endpoint correctness;
- model Z is never Touch+ metric Z;
- exact-tip distance cannot publish or veto identity by itself.

## Phase 2B.9B — landmark-guided distal projection

The useful part of the model appears to be **which anatomical branch is the index and which way is distal**.

2B.9B therefore uses:

```text
MediaPipe INDEX chain
MCP -> PIP -> DIP -> approximate TIP direction
        |
        v
index distal axis + coherence
        |
        +-------------------------------+
                                        |
Touch+ clean LEFT background            |
        +                               |
current LEFT frame                      |
        |                               |
        v                               |
V5-style appearance silhouette          |
        |                               |
        +-------------------------------+
                        |
                        v
             narrow distal corridor
                        |
                        v
       continuous real silhouette boundary
                        |
                        v
                 GUIDED_DISTAL
```

The model does not own the terminal pixel. The Touch+ silhouette does.

### 2B.9B safety rules

A guided 2D distal point requires:

- hand-pose confidence >= 0.80;
- conservative extended-index check;
- coherent MCP/PIP/DIP/TIP phalanx directions;
- a changed Touch+ silhouette overlapping the hand landmarks;
- continuous support from around `INDEX_DIP` through a narrow distal corridor;
- no detached tail stealing the endpoint after a real gap.

Safe failures are:

```text
GUIDED_REJECTED
GUIDED_UNAVAILABLE
```

A confidently wrong `GUIDED_DISTAL` is a blocker.

### Exact-tip oracle is explicitly disabled

2B.9B outputs:

```text
exact_tip_oracle_policy=DISABLED_AFTER_2B9A_PHYSICAL_FAIL
metric_z_source=TOUCHPLUS_STEREO_ONLY
```

The raw model tip may be drawn for diagnosis but is not the authority.

### Synthetic regressions

The 2B.9B self-test models the 2B.9A physical failure class:

- hand confidence near `0.998`;
- model tip deliberately ~28–30 px too proximal;
- correct distal axis;
- silhouette continues to the real fingertip;
- expected result: guided projection reaches the silhouette boundary instead of stopping at landmark 8.

Additional regressions cover:

- diagonal projection;
- contradictory phalanx directions -> reject;
- no silhouette -> unavailable;
- non-index pose -> reject.

Synthetic PASS is necessary but not physical acceptance.

## 2B.9B physical gate

The evaluation dataset should be one persistent capture session:

1. first LEFT frame = clean background, no hand;
2. ten subsequent single-index poses.

The evaluator uses `--background-first` and defaults to LEFT-only images.

Physical acceptance requires:

1. at least **8/10** obvious single-index poses produce a guided distal marker visually on the actual fingertip boundary;
2. successful guided tips materially improve the proximal landmark-8 failures seen in 2B.9A;
3. failures prefer `GUIDED_REJECTED` / `GUIDED_UNAVAILABLE`;
4. no confident guided point lands on another finger, knuckle, wrist, or detached appearance tail;
5. orientation/translation changes do not systematically break index identity.

The green/raw model tip is no longer the judge. The guided silhouette-boundary point is the judge.

## If 2B.9B passes

Next controlled slice: **2B.9C live anatomical sidecar integration**.

Constraints remain:

- V8 stays the temporal palm/branch safety gate;
- DNN anatomy may supply index identity/direction, not Z;
- Touch+ live silhouette owns distal boundary placement;
- disagreement/uncertainty -> `UNKNOWN`;
- only accepted 2D identity reaches existing stereo refinement;
- metric smoothing remains scoped to the locked branch ID;
- `K/D/R/T/P/Q`, Phase 2A surface and stereo gates remain unchanged.

The IPC/runtime mechanism is intentionally deferred until the 2B.9B physical projection gate passes.

## Diagnostics / merge rule

The physical V8 runtime remains:

```text
[TRACK] PHASE 2B.8 RUNTIME ACTIVE | tracker=TEMPORAL-PALM-BRANCH-ID
```

2B.9B is an offline evaluator and does **not** claim to replace that runtime yet.

PR #9 remains **Draft / DO NOT MERGE**.

Phase 2C touch/click remains blocked until real hardware fingertip identity is reliable.