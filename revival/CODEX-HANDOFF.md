# TouchPlus Revival — Codex handoff

This is the short operational entry point for the current coding slice. `revival/REVIVAL-ROADMAP.md` remains the accepted-stack reference; the active PR + this handoff carry the experimental delta.

## Working model

- **Codex** handles focused repo implementation, CMake/MSVC, tests, CI and packaging.
- **Physical bench loop** owns real Touch+ execution and PHYSICAL PASS/FAIL.
- CI is never a substitute for a real-device gate.

## Canonical base

Repository: `shinobione/touch_plus_source_code`

Integration branch: `revival/main`

Accepted merge containing 2B.10A/10B/10C:

`74e28c329256185eee543ae9d8b865f7788f0b54`

Read `AGENTS.md` before changing code.

## Active experiment

Branch:

`revival/phase2b10d-gated-promotion`

Phase:

**2B.10D — gated authoritative promotion, explicit opt-in only**

Design note:

`revival/notes/phase2b10d-gated-authoritative-promotion.md`

PR #14 is merged. Its physical results are accepted evidence:

- 2B.10A local distal refiner: PHYSICAL PASS;
- 2B.10B shadow stereo A/B: PHYSICAL PASS / PROMISING;
- 2B.10C counterfactual promotion gate: PHYSICAL PASS.

2B.10C real-device gate summary:

```text
gate evaluations = 659
KEEP_A           = 644
WOULD_SELECT_B   = 15
```

No anatomically wrong finite `WOULD_SELECT_B` was observed in that smoke. `WOULD_SELECT_B` remained non-authoritative in 2B.10C.

## 2B.10D objective

Implement the first **authoritative** B selection, but only behind an explicit runtime opt-in.

Default launch must remain accepted-A behavior:

```text
HYBRID_PROMOTION=DISABLED -> A authoritative only
```

Experimental bench mode:

```text
HYBRID_PROMOTION=ENABLED
+ existing 2B.10C gate == WOULD_SELECT_B
-> selected same-frame source = B
else
-> selected same-frame source = A
```

Reuse the existing `evaluate_promotion_gate_v10c(...)` rule. Do not loosen thresholds in this slice.

When B is selected, keep the source coherent for that frame:

- selected pixel = B refined pixel;
- selected raw `Xsurface / Ysurface / H` = B raw metric result;
- modern identity id/confidence remains authoritative;
- downstream smoothing may consume the selected metric sample only in explicit promotion-enabled mode.

Do not mix A pixel with B XYZ/H or vice versa.

## Hard exclusions

- `B_only` is never authoritative;
- UNKNOWN/stale/non-current identity is never promotable;
- rejected/inward refiner output is never promotable;
- K/D/R/T/P/Q unchanged;
- camera calibration unchanged;
- accepted surface frame unchanged;
- Phase 2C remains paused and untouched;
- PointerMapper / UDP / mouse / touch / OS injection remain disabled;
- do not let B create/reacquire finger identity.

## Required telemetry

Expose clearly:

```text
promotion_mode=DISABLED|ENABLED
promotion_gate=KEEP_A|WOULD_SELECT_B
selected_source=A|B
selected_reason=...
A confidence/support
B confidence/support
shift_px
dXYZ
dH
selected_A cumulative
selected_B cumulative
source_switches cumulative
```

## Required synthetic coverage

At minimum:

1. promotion disabled + WOULD_SELECT_B -> A;
2. promotion enabled + WOULD_SELECT_B -> B;
3. promotion enabled + KEEP_A -> A;
4. B_only -> A;
5. UNKNOWN/stale/non-current -> A;
6. non-finite/excessive A/B delta -> A;
7. inward/rejected refiner -> A;
8. selected pixel and selected metric source always match;
9. existing V8/V9/2B.10A/2B.10C tests remain green x64 + Win32;
10. Phase 2A and Phase 1C/Q regressions remain green;
11. OS injection remains disabled.

## Scope discipline

Do not re-audit the entire repository unless a contradiction appears.

Primary working set should be limited to the existing 2B.10C gate, its runtime integration, focused tests, CMake/workflow packaging and this slice's documentation.

Do not modify unrelated accepted layers.

## End-of-slice contract

Before handing back to the bench:

- commit + push to `revival/phase2b10d-gated-promotion`;
- report exact SHA;
- report files changed;
- report local tests/builds;
- verify exact-head GitHub CI;
- package a Win32 artifact with promotion disabled by default;
- document the exact opt-in launch command for the promotion-enabled physical smoke;
- do not merge;
- do not claim physical acceptance.

Physical blocker remains:

**any anatomically wrong finite promoted B fingertip = BLOCKER.**
