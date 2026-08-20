# Phase 2C — touch/contact detection

Status: **2C.1A IMPLEMENTED / CI + PHYSICAL RETEST REQUIRED / NO OS INJECTION**

Phase 2B is physically accepted and merged. Phase 2C consumes only the accepted fingertip stream; it does not redefine camera calibration, surface calibration, anatomical identity, or stereo depth.

## Hard boundaries

Do not modify:

- `K / D / R / T / P / Q`;
- accepted per-device camera calibration;
- `surface/<serial>.json` semantics;
- persistent Touch+ capture;
- V8/V9 fingertip identity ownership;
- OpenCV landmark sidecar ownership;
- robust Touch+ stereo matcher.

The only metric source remains Touch+ stereo/Q. Model-relative Z remains disabled.

## Phase 2C.1 initial implementation

2C.1 introduced a semantic layer above the accepted 2B.9C.2 runtime:

```text
accepted fingertip stream
        ↓
TouchContactDetectorV1
        ↓
NO_FINGER / HOVER / APPROACHING / CONTACT_CANDIDATE
        ↓
TOUCH_DOWN / TOUCH_HELD / TOUCH_UP
        ↓
console telemetry only
```

There is **no Windows mouse/touch injection** in this boundary.

Initial candidate thresholds were:

```text
approach band             <= 45 mm
DOWN near-surface band    <= 12 mm
candidate slack           +3 mm
RELEASE hysteresis        >= 22 mm
near samples required     3
release samples required  2
candidate XY step max     16 mm
pre-contact XY jump max   45 mm
held XY jump max          90 mm
H jump max                28 mm
recent approach memory    12 semantic samples
```

## 2C.1 physical smoke — 2026-08-20

The first real Touch+ smoke was a **PARTIAL PASS / TOUCH ACQUISITION FAIL / DO NOT MERGE**.

Observed physical behavior:

- background/no-hand remained safe;
- `NO_FINGER -> HOVER -> APPROACHING` occurred on hardware;
- valid fingertip heights repeatedly reached the physical contact band, including approximately `H=9.9`, `4.1`, `3.7`, `4.4`, `6.1`, `5.4`, `4.2`, `6.0`, `5.6` mm;
- no false semantic `DOWN` was observed;
- `CONTACT_CANDIDATE`, `DOWN`, `HELD`, `UP` were not reached.

The blocker was not the 12 mm threshold. The accepted 2B stream was physically correct when it published, but intentionally intermittent because anatomy/stereo uncertainty often returns UNKNOWN. The 2C.1 detector treated every invalid upstream semantic sample as a complete `hard_reset()`, destroying `near_count`, approach memory, and identity evidence.

Representative hardware pattern:

```text
VALID H=4.1 mm -> APPROACHING
UNKNOWN
VALID H=3.7 mm
UNKNOWN
VALID H=4.4 mm
```

2C.1 incorrectly converted each UNKNOWN into total loss of contact evidence, even though the neighboring validated measurements belonged to the same physical approach.

No personal video/frame from this smoke is committed to the repository.

## Phase 2C.1A — sparse validated contact evidence

2C.1A changes only the semantic evidence accumulator. It does **not** loosen Phase 2B or promote uncertain samples to valid.

Principle:

```text
VALID near-surface sample
UNKNOWN transient gap
VALID near-surface sample
UNKNOWN transient gap
VALID near-surface sample
        ↓
DOWN may be confirmed only from the 3 VALID samples
```

UNKNOWN samples:

- never increment `near_count`;
- never create `DOWN`;
- never provide metric H/XY evidence;
- may preserve already-earned pre-contact evidence only through a very short bounded gap.

The current 2C.1A candidate bounds are:

```text
near samples required             3 VALID samples
candidate evidence window         5 semantic frames
max consecutive transient gaps    1
DOWN near-surface band            <= 12 mm (UNCHANGED)
RELEASE hysteresis                 >= 22 mm (UNCHANGED)
```

A sparse candidate still requires:

- same non-zero `identity_id` on the validated samples;
- recent downward/approach evidence;
- bounded H difference versus the last validated sample;
- bounded XY difference versus the last validated sample;
- all three validated H samples inside the contact band/candidate slack as appropriate.

### Hard resets remain hard

The following still destroy pre-contact evidence immediately:

```text
TRACKING_DISABLED
SURFACE_INVALID
NO_HAND
identity_id switch
impossible H jump
impossible XY jump
more than one consecutive transient gap
evidence window expiry
```

Once contact is active, **any upstream invalidity still emits fail-safe `UP` immediately**. Sparse-gap tolerance applies only before DOWN.

## Upstream reason telemetry

2C.1A classifies an invalid semantic input as one of:

```text
TRACKING_DISABLED
SURFACE_INVALID
NO_HAND
IDENTITY_UNKNOWN
ANATOMY_REJECT
STEREO_LOW
NO_FRESH_METRIC
```

Runtime contact telemetry now reports:

```text
contact_state=...
event=NONE|DOWN|HELD|UP
input=VALID|...
identity_id=...
H=... mm
h_velocity=... mm/s
near_count=...
gap_count=...
evidence_age=...
release_count=...
xy_delta=... mm
reason=...
```

This is diagnostic only; the upstream reason does not bypass the accepted 2B gate.

## Synthetic regressions

`touchplus_touch_contact_selftest` covers:

1. long hover -> zero DOWN;
2. one-frame low-H spike -> zero DOWN;
3. smooth approach -> exactly one DOWN;
4. long hold -> no repeated DOWN;
5. lift -> exactly one UP;
6. five repeated taps -> exactly five DOWN/UP pairs;
7. lateral drag while low-H -> HELD;
8. `NO_HAND` during approach -> hard reset, no DOWN;
9. three validated near samples separated by isolated transient gaps -> exactly one DOWN;
10. transient gaps themselves emit no contact event;
11. two consecutive transient gaps -> evidence expires, no DOWN;
12. invalidity while held -> immediate fail-safe UP;
13. identity ID switch while held -> immediate fail-safe UP;
14. no-hand stream -> no invented contact;
15. violent pre-contact XY jump -> reset;
16. stationary already-near finger without observed approach -> no DOWN.

Synthetic PASS is only permission to perform the hardware smoke. It is not physical acceptance.

## 2C.1A physical retest target

Reuse the accepted anatomy sidecar + tracker runtime, learn the clean background with `B`, then perform:

- hover without touching;
- slow approach and real physical table touch;
- hold;
- lift;
- five deliberate taps;
- one lateral drag;
- deliberate ambiguous/fast hand movement.

Acceptance target:

- hover emits zero DOWN;
- isolated `IDENTITY_UNKNOWN` / `ANATOMY_REJECT` / `STEREO_LOW` / `NO_FRESH_METRIC` may preserve pre-contact evidence but never add evidence;
- a real approach can reach `CONTACT_CANDIDATE` and exactly one `DOWN` from three validated near samples;
- hold produces no repeated DOWN;
- lift produces exactly one UP;
- drag remains one held contact;
- no-hand, hard ambiguity, identity switch, impossible motion, or prolonged gap resets safely;
- no semantic DOWN is created from an uncertain/invalid fingertip.

Only after this semantic layer physically passes should Phase 2C.2 consider Windows pointer/touch injection.
