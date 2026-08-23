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

## Physical smoke — 2026-08-23

A real-device counterfactual-gate smoke was reviewed over approximately 92 seconds with the Touch+ unit `0101007379`.

The work area remained clear through background learning; `background=READY` was reached before the hand entered the work area. The run then exercised extended-index poses across left/right, vertical and diagonal orientations.

Final observed telemetry was:

```text
refiner accepts / attempts = 107 / 162
shadow valid / attempted   = 73 / 107
both A+B valid             = 71
A_only                     = 10
B_only                     = 2

gate evaluations           = 659
KEEP_A                     = 644
WOULD_SELECT_B             = 15
```

`WOULD_SELECT_B` therefore fired on about 2.3% of gate evaluations and only for `STRICT_EVIDENCE_GAIN`.

Final observed reason counters included:

```text
IDENTITY_UNKNOWN              = 444
IDENTITY_STALE                = 53
REFINER_INWARD                = 31
REFINER_REJECTED              = 44
B_ONLY_INELIGIBLE             = 2
A_INVALID                     = 19
B_INVALID                     = 9
EVIDENCE_NOT_STRICTLY_BETTER  = 44
EXCESSIVE_2D_DELTA            = 1
STRICT_EVIDENCE_GAIN          = 15
```

Representative accepted counterfactual selections included:

```text
coarse 543,249 -> refined 547,246
shift = 5.0 px
A = VALID / MEDIUM, support=3
B = VALID / HIGH,   support=6
B-A: dXYZ ~= 0.9 mm, dH ~= +0.3 mm
=> WOULD_SELECT_B
```

and:

```text
coarse 299,270 -> refined 301,270
shift = 2.0 px
A = VALID / HIGH, support=6
B = VALID / HIGH, support=7
B-A: dXYZ ~= 1.3 mm, dH ~= +0.7 mm
=> WOULD_SELECT_B
```

Visual review found the `WOULD_SELECT_B` occurrences on plausible distal index poses and did not observe an anatomically wrong finite counterfactual selection in this smoke. The gate also demonstrated the intended fail-closed behavior: `B_only` remained ineligible, one excessive 2D move was rejected, and stale/unknown identity dominated the `KEEP_A` population.

**2B.10C verdict: PHYSICAL PASS for the counterfactual selector.**

This is not a promotion of B. A remains authoritative and unchanged. Any later authoritative use of B requires a separate explicit promotion slice and a fresh physical gate. Personal video/frames are not committed.

## Promotion boundary

2B.10C has now passed its physical diagnostic boundary. The next architectural step, if pursued, should be a separate minimal promotion experiment rather than silently changing this PR's output ownership.

A remains authoritative until such a follow-up slice is explicitly implemented and physically accepted.
