# Phase 2B.10C — counterfactual promotion gate

Date: 2026-08-23

## Status

Implemented on `revival/hybrid-ractiv-refiner` as a strictly non-authoritative diagnostic slice.

The gate emits only:

```text
KEEP_A
WOULD_SELECT_B
```

`WOULD_SELECT_B` does not select B in the runtime. A remains the only source of official raw/smoothed `XYZ/H`, return values and downstream state.

## Conservative rule

B is counterfactually eligible only when all of the following are true:

- modern fusion is published with a nonzero persistent identity id;
- modern identity confidence is `MEDIUM` or `HIGH`;
- the anatomy decision is locked, published, non-stale and frame-current or accepted motion-compensated;
- the refiner accepted and its forward displacement is non-negative;
- A and B are both stereo-valid;
- A and B stereo confidence values are recognized as `MEDIUM` or `HIGH`;
- B confidence rank is not lower than A;
- B support is not lower than A;
- confidence or support improves strictly;
- full-resolution `A -> B` displacement is finite and at most `18 px`;
- `B - A` raw metric delta is finite, with `dXYZ <= 18 mm` and `abs(dH) <= 12 mm`.

The evidence rule is deliberately Pareto-conservative: B may improve confidence or support, but may degrade neither.

The `18 px` limit is stricter than the refiner's accepted `31 px` search result limit and retains the physically useful 2B.10A/10B examples. The metric limits are far below the accepted tracker's `85 mm` temporal-jump boundary and are intended to isolate only tightly coherent same-frame A/B pairs for review.

## Hard exclusions

- `B_only` always produces `KEEP_A`;
- UNKNOWN, stale or non-current identity produces `KEEP_A`;
- inward or rejected refinement produces `KEEP_A`;
- invalid or excessive 2D/metric delta produces `KEEP_A`;
- equal evidence without a strict gain produces `KEEP_A`;
- no gate result is written into the accepted tracker, smoothing or runtime result;
- K/D/R/T/P/Q and the accepted surface frame are unchanged;
- Phase 2C remains paused and untouched;
- OS injection remains disabled.

## Telemetry

The 30-frame heartbeat reports:

- the current `KEEP_A` / `WOULD_SELECT_B` decision;
- one explicit reason code;
- gate `shift_px`, `dH` and `dXYZ` when available;
- cumulative gate evaluations;
- cumulative `KEEP_A` and `WOULD_SELECT_B` counts;
- cumulative counts for every observed reason.

Representative reason codes include:

```text
IDENTITY_UNKNOWN
IDENTITY_STALE
IDENTITY_NOT_CURRENT
REFINER_INWARD
REFINER_REJECTED
B_ONLY_INELIGIBLE
EVIDENCE_NOT_STRICTLY_BETTER
EXCESSIVE_2D_DELTA
NONFINITE_METRIC_DELTA
EXCESSIVE_METRIC_DELTA
STRICT_EVIDENCE_GAIN
```

Every heartbeat preserves the explicit boundary:

```text
authoritative=A metric_output=UNCHANGED OS_INJECTION=DISABLED
```

## Synthetic regression

`touchplus_fingertip_promotion_gate_selftest` covers:

- B with strictly better confidence/support -> `WOULD_SELECT_B`;
- inferior B -> `KEEP_A`;
- `B_only` -> `KEEP_A`;
- UNKNOWN and stale identity -> `KEEP_A`;
- non-finite metric delta -> `KEEP_A`;
- excessive metric delta -> `KEEP_A`;
- excessive 2D delta -> `KEEP_A`;
- inward and rejected refiner -> `KEEP_A`.

The accepted V8 identity, V9 fusion and 2B.10A refiner tests remain in both x64 and Win32 CI.

## Local verification

Completed successfully before commit:

- x64: V8 identity, V9 fusion, 2B.10A refiner and 2B.10C gate self-tests;
- Win32: V8 identity, V9 fusion, 2B.10A refiner and 2B.10C gate self-tests;
- full Revival Win32 build, including `touchplus_depth_viewer.exe`;
- Phase 2A dominant-surface regression;
- Phase 1C calibration/Q live-depth self-test for serial `0101007379`.

Exact-head GitHub Actions must be green after push before any physical 2B.10C evaluation is requested. CI success still does not constitute physical acceptance.

## Later physical gate

The later physical review should inspect only frames labeled `WOULD_SELECT_B`. Any anatomically wrong finite candidate is a BLOCKER. A remains authoritative until a separate, explicitly approved promotion slice passes its physical boundary.
