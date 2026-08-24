# Phase 2C.1G — raw dense pre-support H diagnostic

## Why this slice exists

The real-device Phase 2C.1F smoke closed the simple `kMinTextureVariance` relaxation path. During visible physical contact, authoritative `TextureLow` probes were replayed through a complete diagnostic forward + reverse + left/right consistency path and no low-texture candidate survived to export a coherent shadow match.

The next question is therefore different: does the already-accepted half-resolution dense stereo contain useful near-surface metric evidence around the fused fingertip before the V6 physical-support floor removes points below `H=8 mm`?

## Diagnostic-only ownership

Phase 2C.1G does **not** alter:

- `search_left_to_right()` or `search_right_to_left()`;
- `mutually_consistent_match()`;
- `kMinTextureVariance`, NCC, uniqueness or LR thresholds;
- dense stereo production or its cost map;
- calibration K/D/R/T/P/Q;
- the accepted Phase 2A surface frame;
- the V6 support floor used by authoritative tracking;
- identity, anatomy, A/B fusion, promotion, smoothing or contact semantics;
- OS output or injection.

The new wrapper runs the complete accepted 2B.10D / Phase 2C runtime first and only then observes existing `DepthWorkspace` data.

## Raw-dense replay

Around an already-published fused fingertip, Phase 2C.1G inspects a 24 full-resolution pixel radius. It keeps only cells that:

1. belong to the current selected hand mask;
2. satisfy the exact dense validity rule already used by `FingertipTrackerV9` (`best_disp > 0`, average cost <= 44 over the 5x5 patch, and second/best uniqueness >= 1.08 when a second cost exists);
3. project to finite XYZ through the accepted Q matrix;
4. project to finite surface coordinates;
5. remain inside the accepted surface XY ROI.

Critically, the diagnostic does **not** apply `kV6MinSupportHmm = 8.0` or any replacement H floor.

## Telemetry

Every 15 modern tracker frames when a fused fingertip is published:

```text
[RAW_DENSE]
frame=...
target=x,y
count=...
nearest_px=...
nearest_H=...
nearest_disparity=...
H_min=...
H_p25=...
H_median=...
radius_px=24
pre_support_H_floor=BYPASSED
authoritative=UNCHANGED
OS_INJECTION=DISABLED
```

## Physical interpretation gate

Compare stable high-hover, near-surface and physical-contact intervals.

Promising contact-proxy evidence requires more than a single low outlier. During physical contact, the local raw-dense distribution should move coherently toward the plane, for example a low `H_p25` / `H_median` together with a nearby low-H sample, while high hover remains clearly separated.

A lone `H_min` near zero with high `H_p25` / `H_median` is not sufficient because it can be one unrelated/edge/table cell. Likewise, if no raw-dense cells survive near the fingertip during contact, the correct conclusion is that dense stereo cannot directly provide a contact proxy at the occluded distal point.

## Merge status

Phase 2C.1 remains **Draft / DO NOT MERGE**. 2C.1G is diagnostic permission to perform another physical smoke only; CI is not physical acceptance.
