# Phase 2C.1E — low-texture counterfactual forward diagnostic

Date: 2026-08-24

## Trigger

Phase 2C.1D real-device telemetry showed a clear regime change in the local full-resolution fingertip matcher.

Representative higher-hover frame:

```text
frame=255
texture_low=0
corr_low=7
uniqueness_fail=10
```

Representative late near/contact frames:

```text
frame=450
fwd_fail=25
texture_low=24
corr_low=0
uniqueness_fail=1
reference_variance_max=92.6
```

```text
frame=465
fwd_fail=20
texture_low=20
corr_low=0
uniqueness_fail=0
reference_variance_max=70.0
```

Dense coarse stereo support remained very close to the fused fingertip target (about 1.0–2.8 px in those late frames) with disparity around 144–146 px. The diagnostic therefore isolates the physically important near/contact failure to the authoritative forward matcher's texture gate, not Q, surface H, ROI, calibration or the Phase 2C 6/4/8 mm semantic thresholds.

## Purpose

Do **not** lower the authoritative global texture threshold from this evidence alone.

Instead, for probes that the authoritative matcher already rejected as `TextureLow`, continue the existing 2C.1D replay in shadow only and evaluate the same NCC search, correlation threshold and uniqueness threshold that would follow if the texture early-return were absent.

The authoritative rejection reason remains `TextureLow` and the authoritative matcher/result is untouched.

## Telemetry

No new runtime line is required. Existing `[FWD]` telemetry now receives counterfactual NCC values for texture-rejected probes:

```text
best_ncc_max
second_ncc_at_best
best_minus_second
winning_disparity
```

In late near/contact frames where all forward failures are `texture_low`, this answers the next decision directly:

- high best NCC + healthy uniqueness gap -> a local lower texture floor may be promising;
- high best NCC + near-zero uniqueness gap -> low-texture patches are ambiguous/repetitive and lowering texture alone is unsafe;
- low NCC -> texture rejection is hiding genuinely poor correspondence, so lowering texture alone is unlikely to help;
- no candidate -> the candidate-side patch validity/search geometry is the real shadow blocker.

The 2C.1D `texture_low` counter remains the authoritative rejection classification. `diag_accepted` remains the consistency alarm for the original authoritative forward path.

## Safety boundary

Phase 2C.1E is diagnostic-only:

- no change to `search_left_to_right()`;
- no change to `mutually_consistent_match()`;
- no change to `kMinTextureVariance=90.0`;
- no change to NCC correlation or uniqueness thresholds;
- no change to patch size or disparity range;
- no change to reverse/L-R authority;
- no change to Q, calibration or surface frame;
- no change to identity/fusion, A/B selection or smoothing;
- no change to contact semantics;
- no OS injection.

PR #17 remains **DRAFT / DO NOT MERGE**.

## Physical smoke

Use default accepted A mode, no hybrid promotion.

1. clear scene and press `B` once;
2. wait for `background=READY`;
3. hold a visible index in high hover for about 5–10 s;
4. lower toward the surface for about 5–10 s;
5. hold physical contact for about 5–10 s;
6. close with `Q`/`ESC`.

Capture repeated `[METRIC]` + `[FWD]` pairs, especially late near/contact frames with `texture_low` dominating.

The goal is not to generate contact events yet. The goal is to determine whether texture-rejected fingertip patches contain a strong and unique stereo NCC solution in shadow.
