# Phase 2C — touch/contact detection

Status: **2C.1B IMPLEMENTED / CI + PHYSICAL RETEST REQUIRED / NO OS INJECTION**

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

2C.1A changed only the semantic evidence accumulator. It did **not** loosen Phase 2B or promote uncertain samples to valid.

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

The 2C.1A candidate bounds remain:

```text
near samples required             3 VALID samples
candidate evidence window         5 semantic frames
max consecutive transient gaps    1
DOWN near-surface band            <= 12 mm (UNCHANGED)
RELEASE hysteresis                 >= 22 mm (UNCHANGED)
```

A sparse candidate still requires recent approach evidence, bounded H/XY motion and one stable semantic identity.

### Hard resets remain hard

The following still destroy pre-contact evidence immediately:

```text
TRACKING_DISABLED
SURFACE_INVALID
NO_HAND
semantic contact identity switch
impossible H jump
impossible XY jump
more than one consecutive transient gap
evidence window expiry
```

Once contact is active, **any upstream invalidity still emits fail-safe `UP` immediately**. Sparse-gap tolerance applies only before DOWN.

## 2C.1A physical retest — 2026-08-21

The hardware retest proved the sparse-gap policy itself works, but exposed a separate semantic identity bug.

### What physically passed

- isolated transient upstream gaps were observed with `gap_count=1` and `reason=transient-gap-preserved`;
- a second consecutive transient gap expired the evidence safely;
- UNKNOWN still contributed no metric evidence and produced no DOWN;
- the physical fingertip again reached well inside the unchanged 12 mm contact band, with validated values around `H=10.3`, `5.8`, `4.6`, `4.5`, `4.2`, `5.2`, `5.0`, `8.8` mm;
- no false semantic DOWN was observed.

### New blocker: raw fusion identity churn

The accepted Phase 2B fusion intentionally uses **two different raw identity namespaces**:

```text
GEOMETRY+ANATOMY -> raw identity = persistent V8 geometry branch id
ANATOMY_ONLY     -> raw identity = 0x8000000000000000 | anatomy_id
```

A single physical fingertip can therefore legitimately alternate between e.g.:

```text
raw 21
raw 0x8000000000000007
raw 21
```

while remaining at the same physical location and height.

The 2C.1A wrapper passed this raw `fusion.identity_id` directly to `TouchContactDetectorV1`. The detector correctly treats an identity-id change as a hard semantic identity switch, so the same physical finger could repeatedly lose `near_count` merely because Phase 2B switched between its two accepted fusion modes.

Representative physical sequence:

```text
VALID | GEOMETRY+ANATOMY | raw id 21                  | H ~= 4.5 mm
VALID | ANATOMY_ONLY     | raw id 0x8000000000000007 | H ~= 4.5 mm
```

2C.1A interpreted this as `identity-switch-reset`. This is a **Phase 2C semantic adaptation problem**, not a Phase 2B regression. Phase 2B raw ids and ownership remain unchanged.

No personal video/frame from this retest is committed.

## Phase 2C.1B — cross-fusion physical contact identity

2C.1B inserts a conservative contact-only identity adapter between the accepted Phase 2B fusion result and `TouchContactDetectorV1`:

```text
accepted Phase 2B raw fusion identity
        ↓
ContactIdentityContinuityV1
        ↓
stable semantic contact_identity_id
        ↓
TouchContactDetectorV1
```

The adapter does **not** rewrite or feed anything back into Phase 2B.

### Allowed alias

A raw-id change may preserve one semantic contact identity only for the specific cross-source transition:

```text
GEOMETRY+ANATOMY <-> ANATOMY_ONLY
```

and only if all of the following hold:

- both samples are fully VALID accepted Phase 2B metric fingertips;
- metric XY continuity <= 18 mm;
- metric H continuity <= 12 mm;
- 2D fingertip continuity <= 30 px;
- the expected raw-id namespace encoding is respected;
- no invalid semantic gap lies directly across the cross-mode transition;
- any previously bound raw id for the target source matches.

Once a semantic identity has learned both sides, the pair is bound, for example:

```text
geometry raw id = 21
anatomy raw id  = 0x8000000000000007
contact id      = 3
```

and either accepted source may continue to report the same contact id while motion remains continuous.

### Explicit non-alias cases

The adapter creates a new semantic contact identity for:

```text
GEOMETRY raw 21 -> GEOMETRY raw 34
ANATOMY raw 7   -> ANATOMY raw 12
GEOMETRY 21 -> ANATOMY 7 with excessive H/XY/2D motion
GEOMETRY 21 -> invalid gap -> ANATOMY 7
bound GEOMETRY 21 -> ANATOMY 7 -> GEOMETRY 34
```

A hard interruption (`NO_HAND`, invalid surface, tracking disabled) clears the current contact identity epoch. A sparse transient gap may preserve the same raw identity for 2C.1A evidence, but it disarms creation of a new cross-mode alias until a fresh stable valid sample re-establishes continuity.

### Telemetry

2C.1B reports both namespaces explicitly:

```text
raw_identity_id=...
contact_identity_id=...
identity_source=GEOMETRY+ANATOMY|ANATOMY_ONLY
identity_alias=contact-identity-stable|cross-mode-physical-identity-bridge|...
```

This prevents future physical smoke analysis from confusing a Phase 2B source-mode transition with a real physical-finger identity switch.

## Upstream reason telemetry

Invalid semantic input remains classified as:

```text
TRACKING_DISABLED
SURFACE_INVALID
NO_HAND
IDENTITY_UNKNOWN
ANATOMY_REJECT
STEREO_LOW
NO_FRESH_METRIC
```

The upstream reason never bypasses the accepted 2B gate.

## Synthetic regressions

`touchplus_touch_contact_selftest` keeps all 2C.1A tests and adds 2C.1B identity regressions:

1. `GEOMETRY 21 -> ANATOMY 7 -> GEOMETRY 21` keeps one contact identity;
2. `GEOMETRY 21 -> ANATOMY 7 -> GEOMETRY 34` is a real identity switch;
3. `ANATOMY 7 -> ANATOMY 12` is a real identity switch;
4. excessive cross-mode metric/2D motion refuses aliasing;
5. cross-mode aliasing cannot be created across an invalid semantic gap;
6. the same raw identity may resume after one sparse transient gap;
7. hard interruption starts a new identity epoch;
8. the physical regression class (`raw 21 <-> raw high-bit anatomy 7` near the same H) must accumulate three validated near samples and produce exactly one semantic DOWN.

The original contact regressions still cover hover, spikes, smooth DOWN, held behavior, lift/UP, repeated taps, drag, sparse gaps, hard NO_HAND, held invalidity, violent motion and stationary already-near fingers.

Synthetic PASS is only permission to perform the hardware smoke. It is not physical acceptance.

## 2C.1B physical retest target

Reuse the accepted anatomy sidecar + tracker runtime, learn the clean background with `B`, then perform:

- hover without touching;
- slow approach and real physical table touch;
- hold;
- lift;
- five deliberate taps;
- one lateral drag;
- deliberate ambiguous/fast hand movement.

The critical new observation is raw-vs-contact identity telemetry. A continuous physical finger may switch between `GEOMETRY+ANATOMY` and `ANATOMY_ONLY`, but a safe bridge should keep `contact_identity_id` stable and avoid `identity-switch-reset`.

Acceptance target:

- hover emits zero DOWN;
- isolated transient gaps may preserve pre-contact evidence but never add evidence;
- valid cross-fusion source churn for the same continuous fingertip does not reset semantic contact identity;
- same-source raw-id changes, impossible motion and hard interruptions still reset;
- a real approach reaches `CONTACT_CANDIDATE` and exactly one `DOWN` from three validated near samples;
- hold produces no repeated DOWN;
- lift produces exactly one UP;
- drag remains one held contact;
- no semantic DOWN is created from an uncertain/invalid fingertip.

Only after this semantic layer physically passes should Phase 2C.2 consider Windows pointer/touch injection.
