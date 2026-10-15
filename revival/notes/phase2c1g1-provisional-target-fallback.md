# Phase 2C.1G.1 — provisional 2D target fallback for raw dense diagnostic

## Why this slice exists

The first real-device Phase 2C.1G smoke exercised the requested physical labels (HIGH -> NEAR -> CONTACT) with background ready, but the conservative Phase 2B fusion gate did not publish a fingertip on the useful portions of that sequence. Because 2C.1G originally required `fusion.publish`, its `[RAW_DENSE]` telemetry was largely absent and the run could not answer the intended question about pre-support dense H near physical contact.

This is an instrumentation blind spot, not evidence that raw dense H is good or bad.

## Diagnostic-only change

2C.1G.1 keeps the exact same raw dense measurement and only broadens how its 2D telemetry center is selected:

1. `FUSED` — published fused fingertip, unchanged preferred source;
2. `ANATOMY` — existing non-stale, non-rejected V9 anatomy candidate;
3. `GEOMETRY` — existing V8 geometry candidate.

The emitted line now includes:

```text
target_source=FUSED|ANATOMY|GEOMETRY
```

Provisional targets are telemetry-only. They do not feed authoritative identity, full-resolution stereo, smoothing, promotion, contact semantics, calibration/surface state, or OS output.

## Raw dense measurement remains unchanged

Within 24 full-resolution pixels of the selected diagnostic target, reuse the current V9 dense validity rule:

- positive dense disparity;
- existing cost threshold;
- existing uniqueness threshold;
- current selected hand mask;
- accepted Q projection;
- accepted surface transform and XY ROI;
- intentionally bypass only the V6 `H >= 8 mm` support floor.

Reported values remain:

```text
count
nearest_px
nearest_H
nearest_disparity
H_min
H_p25
H_median
```

## Safety boundary

Unchanged:

- full-resolution matcher and all thresholds;
- dense matcher and validity thresholds;
- Phase 2B identity/fusion decisions;
- calibration/Q;
- accepted surface model;
- Phase 2C.1 6/4/8 mm contact thresholds;
- authoritative A/B selection;
- OS injection remains disabled.

## Physical gate

Repeat labelled HIGH / NEAR / CONTACT. Interpret `target_source` explicitly. `FUSED` is strongest evidence; `ANATOMY`/`GEOMETRY` are allowed only to characterize whether raw dense H carries a useful physical trend near the already-computed 2D fingertip when conservative fusion fails closed.

This remains diagnostic-only. PR #17 stays Draft / DO NOT MERGE until semantic contact passes the real-device gate.
