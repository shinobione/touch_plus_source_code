# Phase 2C — touch/contact detection

Status: **OPEN / DESIGN FIRST / NO OS INJECTION**

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

## Phase 2C.1 target

Implement **semantic contact events only**. Do not inject Windows mouse/touch yet.

Proposed states:

```text
NO_FINGER
   |
   v
HOVER
   |
   v
APPROACHING
   |
   v
CONTACT_CANDIDATE
   |
   v
TOUCH_DOWN
   |
   v
TOUCH_HELD
   |
   v
TOUCH_UP
   |
   +----> HOVER / NO_FINGER
```

The exact implementation may collapse transient event states internally, but diagnostics must make down/held/up unambiguous.

## Conservative initial semantics

Touch-down must not be a one-frame `H < threshold` rule.

A candidate should require all of:

- valid current fingertip identity;
- stable identity ID across the candidate window;
- valid Touch+ stereo result;
- `H` entering a near-surface band;
- recent approach/downward trend or already-established near-surface candidate;
- multiple consecutive compatible samples;
- bounded XY motion during the final contact confirmation window;
- no impossible H jump.

Release should use hysteresis: the release H threshold must be higher than the touch-down H threshold, and/or require consecutive release evidence.

Initial thresholds must be treated as **candidate values to be physically tuned**, not accepted constants. The physically validated surface noise floor is roughly millimetric, so thresholds should begin comfortably above the observed bare-table residuals rather than at zero.

## Safety rules

These are binding:

```text
identity UNKNOWN       -> no touch / cancel candidate
identity_id change     -> release/reset
stereo invalid         -> no new touch; conservative reset policy
stale anatomy          -> no touch
large XY jump          -> reset candidate
large H jump           -> reset candidate
no hand                -> NO_FINGER
```

A touch detector is not allowed to "hold through" upstream uncertainty merely to make the UI feel smooth.

## Telemetry required before physical smoke

At minimum:

```text
contact_state=...
identity_id=...
H=... mm
h_velocity=... mm/s or trend=...
near_count=...
release_count=...
xy_delta=... mm
reason=...
event=NONE|DOWN|HELD|UP
```

The logs must distinguish a semantic event from the continuously held state.

## Synthetic regressions required

Before hardware smoke, cover at least:

1. hover at 20–80 mm for a long time -> no DOWN;
2. one-frame low-H spike -> no DOWN;
3. smooth approach to surface -> one DOWN;
4. hold near surface for many frames -> no repeated DOWN;
5. lift above release threshold -> one UP;
6. repeated taps -> exactly one DOWN/UP pair per tap;
7. lateral drag while low-H -> HELD, no click spam;
8. identity loss during approach -> reset, no DOWN;
9. identity loss while held -> fail-safe UP/reset;
10. identity ID switch -> fail-safe UP/reset;
11. stereo invalidity / missing metric -> no invented contact;
12. violent H/XY jump -> reset.

## First physical smoke target

Do not map to Windows yet.

Use console/viewer telemetry and ask the operator to perform:

- hover without touching;
- slow approach and physical table touch;
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
- no no-hand touch events.

Only after this semantic layer physically passes should a later Phase 2C slice consider Windows pointer/touch injection.
