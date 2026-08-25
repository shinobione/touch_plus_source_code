# Phase 2C.1C — stereo refiner rejection diagnostic

Date: 2026-08-23

## Purpose

Phase 2C.1B physical telemetry narrowed the current failure to the local full-resolution stereo refinement stage. A representative real-device frame showed:

```text
fusion=PUBLISHED
nearest_support_px=2.0
nearest_disparity_px=158.0
refined_candidates=0
refined_consistent=0
stereo_confidence=LOW
reason=REFINED_SUPPORT_TOO_LOW
```

This proves that accepted dense stereo support exists very close to the fused fingertip target, while none of the local full-resolution refinement probes survives into a metric fingertip sample.

Phase 2C.1C is diagnostic-only. It does not change the matcher, thresholds, disparity range, calibration, K/D/R/T/P/Q, surface frame, identity/fusion, A/B selection, smoothing, contact semantics or OS output.

## Added telemetry

For the existing 7x7 / 49-probe local refinement grid, the runtime now records per-frame rejection counters:

```text
probes
out_of_bounds
outside_hand_mask
forward_match_fail
reverse_match_fail
left_right_mismatch
matcher_other_fail
q_projection_fail
surface_h_reject
surface_roi_reject
accepted
```

The existing `refined_candidates` and `refined_consistent` counters remain authoritative for the accepted refinement path.

When the authoritative `mutually_consistent_match()` rejects a probe, Phase 2C.1C replays the same forward/reverse matching stages only to classify the rejection. The authoritative match result remains untouched. This can add diagnostic CPU work on rejected probes, but it does not alter acceptance logic.

The `[METRIC]` heartbeat now includes the counters above together with target pixel, nearest dense support distance/disparity, identity/stereo confidence and the existing metric rejection reason.

## Physical smoke goal

Use accepted default A mode, no hybrid promotion, no OS injection.

1. Start a fresh session.
2. Clear scene, press `B`, wait for `background=READY`.
3. Hold one index clearly visible in hover for ~5–10 s.
4. Move near the table for ~5–10 s.
5. Hold physical contact for ~5–10 s.
6. Close with `Q`/`ESC`.

The goal is not to pass contact semantics yet. The goal is to identify which rejection class dominates the 49 local stereo probes when `reason=REFINED_SUPPORT_TOO_LOW`.

## Decision rule

Do not tune matcher thresholds from a single frame. Use repeated `[METRIC]` lines to determine the dominant failure family first:

- dominant `forward_match_fail` -> inspect texture/correlation/uniqueness on LEFT->RIGHT search;
- dominant `reverse_match_fail` or `left_right_mismatch` -> inspect fingertip occlusion / L-R visibility and consistency;
- dominant `outside_hand_mask` -> inspect fused target versus accepted silhouette geometry;
- dominant `surface_h_reject` / `surface_roi_reject` -> inspect metric support geometry without changing accepted surface calibration;
- significant accepted probes but too few consistent -> inspect H-consistency/refinement support rather than the matcher front-end.

Any future algorithm or threshold change must be a separate reversible slice with synthetic regressions and a new physical gate.

## Safety boundary

- PR #17 remains Draft;
- no merge from this diagnostic alone;
- `OS_INJECTION=DISABLED` remains mandatory;
- no PointerMapper, UDP, mouse or Windows touch output;
- calibration and Phase 2A surface frame remain protected;
- wrong finite/high-confidence fingertip remains a BLOCKER.
