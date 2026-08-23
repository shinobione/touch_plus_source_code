# Phase 2C.1 — conservative touch/contact semantics

Date: 2026-08-23

## Status

Planned implementation slice on `revival/phase2c1-contact-semantics`.

Accepted base: `revival/main` after PR #16 / Phase 2B.10D physical PASS and merge at:

`bd9f7eb905210595837482dbd0d45410f4d92cb2`

Phase 2C.1 must produce semantic contact events only. No mouse, Windows touch, UDP, PointerMapper, click injection, or other OS output is allowed in this slice.

## Objective

Consume the accepted current fingertip stream (`Xsurface / Ysurface / H`) and add a conservative temporal contact state machine that can emit only:

- `HOVER`
- `TOUCH_DOWN`
- `TOUCH_HELD`
- `TOUCH_UP`

The contact layer must not change fingertip identity, stereo, calibration, surface-frame geometry, A/B promotion rules, or Phase 2B tracking.

## Input ownership

The state machine consumes the already-selected authoritative fingertip sample from the accepted runtime. It does not choose A vs B itself.

Required input per frame:

- current accepted identity is published/current and non-stale;
- fingertip metric sample is valid and finite;
- selected `Xsurface / Ysurface / H` from the accepted runtime;
- current identity id so an identity change can reset/fail safe;
- source label A/B for telemetry only.

If identity is UNKNOWN/stale/non-current, stereo/sample is invalid, or metric values are non-finite, the frame is never eligible to create or continue contact.

## State model

Use an explicit deterministic state machine, e.g.:

`NO_FINGER -> HOVER -> APPROACHING -> CONTACT_CANDIDATE -> TOUCH_DOWN -> TOUCH_HELD -> RELEASE -> HOVER`

Externally visible semantic events remain only `HOVER`, `TOUCH_DOWN`, `TOUCH_HELD`, `TOUCH_UP`.

Safety rules:

1. one near-surface frame can never create `TOUCH_DOWN`;
2. touchdown requires temporal persistence plus approach/downward context;
3. release threshold is strictly above touchdown threshold (hysteresis);
4. identity change/loss while touching must fail safe by ending the held contact, never leaving a stuck touch;
5. large impossible H/XY jumps reset candidate/approach state;
6. no-hand/invalid frames can never create a down event;
7. a held stationary fingertip must not emit repeated `TOUCH_DOWN` events;
8. lateral XY motion while low-H remains one held contact;
9. repeated physical taps must produce exactly one DOWN/UP pair per tap.

## Initial conservative constants

These are first-smoke tuning values, not new calibration facts:

```text
candidate_h_mm      = 6.0
contact_down_h_mm   = 4.0
contact_up_h_mm     = 8.0
candidate_frames    = 3
release_frames      = 2
approach_delta_mm   = -0.5 over recent valid samples
max_frame_dh_mm     = 20.0
max_frame_dxy_mm    = 50.0
```

At the accepted ~30 fps cadence, three candidate frames are roughly 100 ms. False negatives are preferable to false touches in this first slice.

Do not silently tune these values from synthetic tests. Any threshold changes after implementation must be justified by the physical smoke.

## Fail-safe behavior

- Invalid/unknown before touch: reset to `NO_FINGER`, no touch event.
- Invalid/unknown while `TOUCH_DOWN`/`TOUCH_HELD`: emit one fail-safe `TOUCH_UP` semantic event with explicit reason, then reset.
- Identity id change while held: same fail-safe release behavior.
- Non-finite sample: same fail-safe behavior.
- Excessive jump: cancel approach/candidate; if already held, emit one fail-safe `TOUCH_UP` and reset.

## Telemetry

Report at a human-reviewable cadence and on every semantic transition:

```text
contact_state=...
contact_event=NONE|HOVER|TOUCH_DOWN|TOUCH_HELD|TOUCH_UP
contact_reason=...
identity_id=...
fingertip_source=A|B
X=...
Y=...
H=...
dH=...
dXY=...
candidate_count=...
release_count=...
DOWN_total=...
UP_total=...
OS_INJECTION=DISABLED
```

Transition lines should be easy to grep independently from regular tracking heartbeat output.

## Synthetic coverage

At minimum test:

1. hover only -> zero DOWN/UP;
2. one low-H spike -> zero DOWN;
3. sustained approach through candidate threshold -> exactly one DOWN;
4. stationary hold -> no repeated DOWN;
5. lateral motion while held -> remains held;
6. H rises through release hysteresis -> exactly one UP;
7. repeated taps -> one DOWN/UP pair per tap;
8. invalid/stale/UNKNOWN before contact -> zero DOWN;
9. invalid/stale/identity change while held -> exactly one fail-safe UP;
10. excessive H/XY jump -> fail closed;
11. non-finite metric input -> fail closed;
12. input source A and input source B obey identical contact semantics;
13. existing Phase 1C, Phase 2A and Phase 2B through 2B.10D regressions remain green;
14. OS injection remains disabled.

## Physical gate

First smoke should run with default accepted tracking behavior and semantic contact enabled, with no OS injection.

Required real-device sequences:

- hover clearly above table for several seconds: zero touch;
- hover close to table without touching: zero touch;
- slow approach to physical surface: exactly one DOWN near contact;
- hold still on table: no DOWN spam;
- lift: exactly one UP;
- at least five repeated taps: one DOWN/UP pair each;
- drag laterally while touching: one held contact until lift;
- deliberately leave/re-enter view: no invented DOWN; held contact must fail-safe UP on loss;
- no-hand scene: zero events.

Physical review must compare event timing to the actual finger/table contact in video and inspect H telemetry. A confident false `TOUCH_DOWN` while visibly hovering is a BLOCKER.

## Merge rule

CI alone is insufficient. Keep the PR Draft until the real Touch+ semantic-contact smoke passes.
