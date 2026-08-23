# TouchPlus Revival — Codex handoff

This is the short operational entry point for the current coding slice. Do not re-audit the whole repository unless a contradiction appears.

## Canonical base

Repository: `shinobione/touch_plus_source_code`

Integration branch: `revival/main`

Accepted Phase 2B.10D merge:

`bd9f7eb905210595837482dbd0d45410f4d92cb2`

Phase 2B.10D is physically accepted. Explicit gated B promotion passed on the real Touch+, but promotion remains opt-in. OS injection remains disabled.

Read `AGENTS.md` before changing code.

## Active slice

Branch:

`revival/phase2c1-contact-semantics`

Phase:

**2C.1 — conservative touch/contact semantics, semantic events only**

Design note:

`revival/notes/phase2c1-contact-semantics.md`

## Objective

Add a deterministic contact state machine on top of the already-selected authoritative fingertip stream (`Xsurface / Ysurface / H`).

Externally visible semantic events only:

```text
HOVER
TOUCH_DOWN
TOUCH_HELD
TOUCH_UP
```

No Windows mouse/touch injection, PointerMapper, UDP, click synthesis or other OS output is allowed.

## Hard ownership boundaries

- consume the accepted selected fingertip; do not choose/recompute A vs B;
- current accepted identity remains authoritative;
- UNKNOWN/stale/non-current identity can never create/continue contact;
- invalid/non-finite metric sample can never create/continue contact;
- camera calibration and `K/D/R/T/P/Q` unchanged;
- accepted per-setup surface frame unchanged;
- capture, matcher, sidecar, fingertip identity/fusion, refiner, 2B.10C gate and 2B.10D promotion rules unchanged;
- OS injection remains disabled.

## Initial first-smoke constants

Use the values from the design note exactly for the first implementation:

```text
candidate_h_mm    = 6.0
contact_down_h_mm = 4.0
contact_up_h_mm   = 8.0
candidate_frames  = 3
release_frames    = 2
approach_delta_mm = -0.5
max_frame_dh_mm   = 20.0
max_frame_dxy_mm  = 50.0
```

Do not silently tune them from synthetic tests.

## Primary working set

Start from only:

- `revival/notes/phase2c1-contact-semantics.md`;
- the final authoritative fingertip result path in `revival/src/depth_surface_frame_runtime_v10.h`;
- a new isolated contact-state-machine header/source + self-test;
- `revival/phase2b_test/CMakeLists.txt` (or a narrowly scoped Phase 2C test target if cleaner);
- `.github/workflows/revival-fingertip.yml` only as needed to add the new self-test / package the physical kit;
- runtime telemetry integration only where necessary.

Do not read all historical Phase 2B notes.

## Required behavior

- one low-H sample never creates DOWN;
- sustained near-surface approach creates exactly one DOWN;
- held finger never repeats DOWN;
- lateral XY movement while low stays held;
- release hysteresis creates exactly one UP;
- identity loss/change while held emits one fail-safe UP then resets;
- invalid/non-finite/excessive jump fails closed;
- repeated taps create one DOWN/UP pair each;
- A and B selected-source samples obey identical contact semantics.

## Telemetry

Expose transition lines and periodic state telemetry containing at least:

```text
contact_state
contact_event
contact_reason
identity_id
fingertip_source
X / Y / H
dH / dXY
candidate_count / release_count
DOWN_total / UP_total
OS_INJECTION=DISABLED
```

## Regression / packaging contract

Before handoff to the bench:

- add focused synthetic coverage from the design note;
- keep existing V8/V9/2B.10A/2B.10C/2B.10D tests green x64 + Win32;
- keep Phase 2A + Phase 1C/Q regressions green;
- build the real Revival Win32 runtime;
- package an exact-head Win32 physical-smoke artifact;
- include the sidecar launcher path already used by the accepted stack;
- report SHA, files changed, tests, artifact and exact launch command;
- commit + push only to `revival/phase2c1-contact-semantics`;
- do not merge;
- do not claim physical acceptance.

Physical blocker:

**any confident `TOUCH_DOWN` while the fingertip is visibly hovering = BLOCKER.**
