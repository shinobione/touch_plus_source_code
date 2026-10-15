# Phase 2C.1D — forward matcher rejection diagnostic

Date: 2026-08-24

## Why this slice exists

Phase 2C.1C passed its diagnostic hardware gate. In repeated real-device frames with `fusion=PUBLISHED`, dense stereo support existed close to the fused fingertip, but the 49-probe local full-resolution refiner still produced too little support.

Representative observed distributions:

```text
frame 240: probes=49 mask=27 fwd_fail=20 rev_fail=2 lr_fail=0 accepted=0
frame 255: probes=49 mask=28 fwd_fail=16 rev_fail=1 lr_fail=3 accepted=1
frame 270: probes=49 mask=25 fwd_fail=24 rev_fail=0 lr_fail=0 accepted=0
```

Across those representative frames, `q_fail=0`, `h_reject=0`, `roi_reject=0` and `matcher_other=0`. The dominant surviving failure family after the hand mask is therefore the LEFT->RIGHT forward matcher, not Q, the accepted surface frame or the contact thresholds.

## Scope

2C.1D is diagnostic-only. It does not change:

- the authoritative `search_left_to_right()` implementation;
- `mutually_consistent_match()`;
- patch radius / 9x9 patch size;
- disparity range;
- texture threshold;
- NCC correlation threshold;
- uniqueness gap threshold;
- left-right tolerance;
- calibration / K/D/R/T/P/Q;
- surface frame / H geometry;
- identity, fusion, smoothing or A/B selection;
- Phase 2C contact thresholds or state machine;
- OS output.

The diagnostic helper replays the exact current forward-stage decisions only after the authoritative forward matcher has already rejected a probe.

## New `[FWD]` telemetry

For every `[METRIC]` report that contains forward failures, a companion `[FWD]` line reports the split:

```text
patch_oob
texture_low
window_empty
no_candidate
corr_low
uniqueness_fail
diag_accepted
```

It also reports representative quantitative values:

```text
reference_variance_max
best_ncc_max
second_ncc_at_best
best_minus_second
winning_disparity
```

and prints the unchanged active thresholds:

```text
texture=90
corr=0.78
gap=0.055
```

`diag_accepted` is an internal consistency alarm: because the diagnostic replay mirrors the forward matcher, it should normally remain zero whenever the authoritative forward stage returned invalid.

## Physical smoke

Use the same conservative default-A session as 2C.1C:

1. clear scene;
2. press `B` once;
3. wait for `background=READY`;
4. one index clearly visible in hover for ~5–10 s;
5. move near the table for ~5–10 s;
6. hold physical fingertip contact for ~5–10 s;
7. close with `Q`/`ESC`.

Film the console so paired `[METRIC]` and `[FWD]` lines are readable.

## Decision rule

Do not tune anything before the hardware distribution is known.

- dominant `texture_low` -> fingertip skin lacks enough 9x9 reference texture under the current variance gate;
- dominant `no_candidate` -> candidate patches on RIGHT are mostly unusable / texture-poor across the bounded search window;
- dominant `corr_low` -> valid patches exist, but best NCC does not reach 0.78;
- dominant `uniqueness_fail` -> best NCC is high enough but not sufficiently separated from repeated/ambiguous alternatives;
- dominant `window_empty` -> coarse disparity window or geometry is inconsistent with the full-resolution search;
- non-zero `diag_accepted` -> diagnostic replay mismatch; stop and investigate before drawing physical conclusions.

Any algorithm or threshold change after this diagnostic must be a separate reversible slice with CI regressions and a new physical gate.

## Safety

PR #17 remains Draft / DO NOT MERGE. `OS_INJECTION=DISABLED` remains mandatory. Wrong finite/high-confidence fingertip remains a blocker; UNKNOWN remains acceptable when evidence is insufficient.
