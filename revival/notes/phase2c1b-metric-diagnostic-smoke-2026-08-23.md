# Phase 2C.1B — metric stereo diagnostic smoke — 2026-08-23

## Scope

Real-device diagnostic run for PR #17 (`revival/phase2c1-contact-semantics`) using exact-head `b4ac2abf7ac7092fdb04447d5d356a0833649648` and the diagnostic-only `[METRIC]` telemetry. No acceptance threshold, matcher, calibration, surface frame, contact semantic or OS output behavior was changed.

## Key observation

The run confirms that the dominant blocker is **not** missing nearby dense stereo support.

Representative live telemetry while the post-sidecar anatomy/fusion path was accepted:

```text
[METRIC] frame=465
fusion=PUBLISHED
target=319,309
nearest_support_px=2.0
nearest_disparity_px=158.0
refined_candidates=0
refined_consistent=0
identity_confidence=MEDIUM
stereo_confidence=LOW
reason=REFINED_SUPPORT_TOO_LOW
OS_INJECTION=DISABLED
```

The same recording also contains earlier `FUSION_NOT_PUBLISHED` periods while identity/anatomy is not yet accepted; those are expected fail-closed states and are not the metric blocker of interest.

## Interpretation

For the representative accepted-fusion frame:

- anatomy/identity fusion is published;
- the fused fingertip target has accepted dense stereo support only ~2 px away;
- a valid coarse disparity estimate exists (~158 px);
- however the existing local full-resolution robust stereo refinement around the fingertip produces **zero accepted refined candidates**;
- therefore `stereo_confidence` remains `LOW` and the authoritative metric fingertip is not published.

This isolates the current failure to the **local refined stereo matching / refinement acceptance stage around the accepted 2D fingertip**, downstream of anatomy/fusion and downstream of coarse dense-support discovery.

The current evidence does **not** justify changing:

- Phase 2C 6/4/8 mm contact thresholds;
- K/D/R/T/P/Q;
- the accepted Phase 2A surface frame;
- sidecar timing or anatomy gates;
- A/B authoritative selection.

## Next engineering question

Before any algorithm change, instrument or audit the existing local robust-match rejection reasons around the fingertip so the zero-candidate result is decomposed into at least:

- sample outside image/mask;
- left/right consistency failure;
- no valid disparity in the coarse ±18 px search window;
- invalid/non-finite Q projection;
- surface-H / ROI rejection;
- post-H-consistency rejection.

The immediate goal is to learn **why `mutually_consistent_match(...)` yields no accepted local samples despite nearby dense support**, not to loosen gates blindly.

## Physical verdict

`DIAGNOSTIC PASS / ROOT CAUSE NARROWED`

Phase 2C.1 itself remains **PHYSICAL FAIL / SAFE FALSE-NEGATIVE** because intended contact events still cannot be evaluated reliably without a valid authoritative metric fingertip.

PR #17 remains Draft / DO NOT MERGE. `OS_INJECTION=DISABLED` remains mandatory.
