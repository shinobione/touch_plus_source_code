# Phase 2C.1F — low-texture shadow mutual-consistency diagnostic

## Purpose

Phase 2C.1E.1 established on the physical Touch+ that simply lowering the 9x9 reference-texture threshold is not justified by forward NCC alone. Most authoritative `TextureLow` probes still produce weak counterfactual forward correlation, one representative frame is maximally ambiguous, and the isolated stronger forward example sits far from the nearest coarse-support disparity.

2C.1F therefore asks one narrower question before any matcher tuning:

> Does any authoritative `TextureLow` probe that counterfactually passes the unchanged forward correlation and uniqueness gates also survive a counterfactual reverse search and the existing left/right disparity-consistency tolerance?

## Safety boundary

This slice is diagnostic-only.

It does **not** change:

- authoritative `search_left_to_right()`;
- authoritative `search_right_to_left()`;
- authoritative `mutually_consistent_match()`;
- `kMinTextureVariance = 90.0`;
- `kMinCorrelation = 0.78`;
- `kMinCorrelationGap = 0.055`;
- `kLeftRightTolerancePx = 1.25`;
- patch radius / 9x9 patch size;
- disparity limits or local search window;
- calibration / K-D-R-T-P-Q;
- Phase 2A surface frame;
- fingertip identity/fusion/A-B selection/smoothing;
- Phase 2C.1 contact thresholds or state machine;
- OS output (`OS_INJECTION=DISABLED`).

## Diagnostic behavior

The existing 2C.1E.1 replay still classifies the authoritative rejection exactly as `TextureLow`.

For a low-texture probe only:

1. continue the shadow LEFT->RIGHT NCC search;
2. require the unchanged correlation gate;
3. require the unchanged uniqueness-gap gate;
4. subpixel-refine the shadow forward disparity exactly as before;
5. perform a shadow RIGHT->LEFT search in the same ±3 px narrow disparity window used by authoritative mutual consistency;
6. require the existing `kLeftRightTolerancePx` gate.

The shadow reverse search bypasses only the reverse reference-patch `variance < 90` early return. It still uses the existing `zero_mean_ncc()` implementation, candidate-patch validity rules, NCC threshold and uniqueness threshold.

No shadow result is ever returned to authoritative tracking.

## Telemetry contract

To avoid adding another console line, the existing `[FWD]` aggregate is tightened for 2C.1F:

- `texture_low=N` continues to report the authoritative TextureLow count;
- `reference_variance_max` remains the aggregate forward-failure variance diagnostic;
- `best_ncc_max`, `second_ncc_at_best`, `best_minus_second` and `winning_disparity` are now populated only by a low-texture shadow candidate which also passes shadow reverse + LR consistency;
- if low-texture probes exist but none survive full shadow mutual consistency, those exported match-quality fields remain `nan`.

This makes the physical interpretation intentionally binary:

- `texture_low>0` + match-quality fields `nan` => no low-texture probe survived full shadow mutual consistency; simple texture-threshold lowering is not supported;
- finite match-quality fields => at least one low-texture probe survives mutual consistency and requires one further metric/Q/surface-H shadow check before any authoritative change could be considered.

## Merge gate

Phase 2C.1 remains **DRAFT / DO NOT MERGE** until the semantic contact physical gate passes. CI is not physical acceptance.