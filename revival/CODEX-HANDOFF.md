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

Physical diagnostic note:

`revival/notes/phase2c1a-h-characterization.md`

## Current physical status

Phase 2C.1 first smoke was a safe false-negative: `DOWN_total=0`, `UP_total=0`, no false touch, but no intended touch either.

Phase 2C.1A H-characterization run #1 then showed sparse valid high-hover samples around 60 mm and a readable near-hover sample around 43.7 mm, but the physical-contact pose was dominated by `NO_FINGER` / `INVALID_SAMPLE` with identity/fingertip state UNKNOWN/stale/non-current. No stable CONTACT H distribution was obtained.

**Do not retune 6/4/8 mm yet. The next blocker to characterize is identity/sample continuity at or extremely near physical contact.**

The next implementation, if requested, should be diagnostic-only: self-labelled per-frame CSV capture for HIGH / NEAR / CONTACT including validity/rejection state, raw H and smoothed H. It must not alter accepted tracking/contact behavior.

## Objective

The existing Phase 2C.1 state machine produces only:

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

## Current contact constants

Do not change these without new physical evidence:

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

## Next diagnostic working set

If implementing the next diagnostic, start only from:

- `revival/notes/phase2c1a-h-characterization.md`;
- `revival/src/depth_surface_frame_runtime_v10.h` around current contact/raw-H telemetry;
- `revival/tools/start-touchplus-phase2b9c.ps1` only if diagnostic hotkeys/launch options require it;
- `.github/workflows/revival-fingertip.yml` only for packaging/test wiring.

Do not read all historical Phase 2B notes.

## Next diagnostic requirements

Diagnostic only, no behavior changes:

- operator labels `HIGH`, `NEAR`, `CONTACT` (hotkeys or another explicit runtime label mechanism);
- per-frame CSV containing timestamp/frame, label, identity accepted/current/stale, fingertip valid, source A/B, raw H, smoothed H, rejection reason;
- record invalid/UNKNOWN rows too;
- preserve all current contact thresholds and state transitions;
- preserve A/B selection, surface frame and calibration;
- `OS_INJECTION=DISABLED`.

## Regression / packaging contract

Before another physical handoff:

- keep existing V8/V9/2B.10A/2B.10C/2B.10D/2C.1 tests green x64 + Win32;
- keep Phase 2A + Phase 1C/Q regressions green;
- build the real Revival Win32 runtime;
- package an exact-head Win32 physical-smoke artifact;
- include the reusable bench-cache-aware launcher;
- report SHA, files changed, tests, artifact and exact launch command;
- commit + push only to `revival/phase2c1-contact-semantics`;
- do not merge;
- do not claim physical acceptance.
