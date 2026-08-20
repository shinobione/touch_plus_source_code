# Phase 2C — touch/contact detection

Status: **2C.1 IMPLEMENTED / CI CANDIDATE / PHYSICAL SMOKE REQUIRED / NO OS INJECTION**

Phase 2B is physically accepted and merged. Phase 2C consumes only the accepted current fingertip result; it does not redefine camera calibration, surface calibration, anatomical identity, or stereo depth.

## Hard boundaries

Do not modify:

- `K / D / R / T / P / Q`;
- accepted per-device camera calibration;
- `surface/<serial>.json` semantics;
- persistent Touch+ capture;
- V8/V9 fingertip identity ownership;
- OpenCV landmark sidecar ownership;
- robust Touch+ stereo matcher.

The input to contact semantics is only a currently valid fingertip with:

```text
identity valid/current
stereo valid/current
identity_id
Xsurface
Ysurface
H
frame/time
```

Any invalid/unknown/stale upstream state is a hard contact reset/fail-safe.

## Phase 2C.1 implementation

2C.1 is deliberately a new semantic layer above the accepted 2B.9C.2 runtime:

```text
accepted current fingertip
        ↓
TouchContactDetectorV1
        ↓
NO_FINGER / HOVER / APPROACHING / CONTACT_CANDIDATE
        ↓
TOUCH_DOWN / TOUCH_HELD / TOUCH_UP
        ↓
console telemetry only
```

There is **no Windows mouse/touch injection** in this slice.

The runtime wrapper is `revival/src/touch_contact_runtime.h`. It force-includes the accepted `depth_surface_frame_runtime.h` first and consumes only its final valid `TrackingResult` plus `fusion.identity_id`. This keeps calibration, surface geometry, anatomical fusion and stereo matching physically owned by the already accepted layers.

## Candidate thresholds for the first hardware smoke

These values are intentionally conservative **candidates**, not accepted constants:

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

The physically validated bare-table residual is millimetric, so DOWN begins comfortably above zero. The real Touch+ smoke decides whether these thresholds need tuning; CI does not promote them to physical truth.

## Conservative semantics

Touch-down is never a one-frame `H < threshold` rule.

A DOWN candidate requires all of:

- valid current fingertip identity;
- non-zero stable `identity_id` across the candidate window;
- valid Touch+ stereo metric result;
- recent downward/approach evidence;
- entry into the near-surface band;
- multiple consecutive compatible samples;
- bounded XY motion during final confirmation;
- no impossible H/XY jump.

Release uses hysteresis and consecutive release evidence. Once held, ordinary lateral motion is allowed so a drag remains one held contact rather than click spam.

## Safety rules

These are binding:

```text
identity UNKNOWN       -> no DOWN / cancel candidate
identity_id change     -> fail-safe UP if held, then reset
stereo invalid         -> no DOWN; fail-safe UP if held
stale anatomy          -> no DOWN / reset through upstream invalidity
large XY jump          -> reset; fail-safe UP if held
large H jump           -> reset; fail-safe UP if held
no hand                -> NO_FINGER
```

The detector is not allowed to hold through upstream uncertainty merely to make the UI feel smooth.

## Runtime telemetry

State transitions and DOWN/UP edges are logged immediately. A heartbeat also reports:

```text
contact_state=...
event=NONE|DOWN|HELD|UP
identity_id=...
H=... mm
h_velocity=... mm/s
near_count=...
release_count=...
xy_delta=... mm
reason=...
```

Important distinction:

- `event=DOWN` is emitted once on contact confirmation;
- subsequent contact frames report `event=HELD`;
- `event=UP` is emitted once on release or fail-safe release.

## Synthetic regressions implemented

`touchplus_touch_contact_selftest` covers:

1. long hover -> zero DOWN;
2. one-frame low-H spike -> zero DOWN;
3. smooth approach -> exactly one DOWN;
4. long hold -> no repeated DOWN;
5. lift -> exactly one UP;
6. five repeated taps -> exactly five DOWN/UP pairs;
7. lateral drag while low-H -> HELD, no click spam;
8. identity loss during approach -> reset, no DOWN;
9. identity/stereo loss while held -> fail-safe UP;
10. identity ID switch while held -> fail-safe UP;
11. invalid stereo/no finger -> no invented contact;
12. violent pre-contact XY jump -> reset;
13. stationary already-near finger without observed approach -> no DOWN.

Synthetic PASS is only permission to perform the hardware smoke. It is not physical acceptance.

## First physical smoke target

Do not map to Windows yet.

Start the accepted anatomy sidecar + tracker runtime, learn the clean background with `B`, then perform:

- hover at several heights without touching;
- slow approach and real physical table touch;
- hold for several seconds;
- lift;
- 5 deliberate taps;
- one lateral drag while touching;
- deliberate ambiguous/fast hand movement.

Acceptance target:

- hover emits zero DOWN;
- each real tap produces exactly one DOWN + one UP;
- hold produces no repeated DOWN;
- drag remains one held contact;
- ambiguity/identity loss fails safe;
- no no-hand touch events;
- no DOWN is created from an uncertain/invalid fingertip.

Only after this semantic layer physically passes should Phase 2C.2 consider Windows pointer/touch injection.
