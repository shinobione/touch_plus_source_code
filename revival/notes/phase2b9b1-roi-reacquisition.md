# Phase 2B.9B.1 — silhouette-ROI landmark reacquisition

Date: 2026-08-20  
Physical unit: `0101007379`

## Goal

Improve 2B.9B recall on the **existing** physical dataset without weakening the property that all currently published `GUIDED_DISTAL` points were anatomically plausible.

This is still an offline evaluation boundary. The stable Win32 Etron tracker remains V8 and does not gain an OpenCV/ONNX dependency here.

## Why this slice exists

Physical 2B.9B produced:

```text
5 / 10 GUIDED_DISTAL
5 / 5 published tips visually correct
0 wrong guided tips observed
3 GUIDED_UNAVAILABLE
2 GUIDED_REJECTED
```

Therefore the target is not "make projection looser". It is:

> recover more reliable index anatomy before projection, while failing closed on broad/wrong anatomical paths.

## 1. Touch+ silhouette-guided ROI reacquisition

Before landmark inference, the evaluator can already compare the current LEFT frame with the clean pair-001 LEFT background.

2B.9B.1 uses the accepted V5 appearance-only delta threshold to find a changed component, then computes a distance transform inside that component. The maximum interior distance becomes an **appearance palm-core proposal** used only to center reacquisition crops.

The crop sizes are relative to the measured palm radius:

```text
6.5 x palm_radius
8.0 x palm_radius
10.0 x palm_radius
```

with conservative minimum/maximum bounds.

PalmDet/HandPose is retried only if full-frame guidance does not produce `GUIDED_DISTAL`.

The ROI path may lower PalmDet's proposal threshold from 0.50 to 0.35 because the crop is already constrained by real Touch+ appearance change. That lower PalmDet score **cannot publish anything by itself**: hand-pose confidence, silhouette support, anatomy and distal projection gates remain binding.

ROI screen landmarks are remapped back into the original 640x480 LEFT coordinate system before any projection or comparison.

## Physical dataset localization evidence

Without rerunning the DNN, the already-captured frames show valid appearance ROI seeds for all five 2B.9B failures.

Representative derived seeds:

```text
pair 007 : changed bbox ~236,101 320x241 | palm radius ~46 px
pair 008 : changed bbox ~336,109 133x87  | palm radius ~21 px
pair 010 : changed bbox ~372,67 117x131 | palm radius ~24 px
```

This is especially useful for pair 008/010, where the hand is relatively small in the full 640x480 frame.

These values are derived engineering telemetry only; no raw image is committed.

## 2. Perspective-aware index evidence

The old 2D extension test includes a wrist-distance relation that can fail under strong foreshortening.

2B.9B.1 does **not** simply remove that check.

The strict path remains first choice:

```text
STRICT_2D
```

If strict 2D extension fails, a perspective path is considered only when:

1. MCP -> PIP -> DIP -> TIP directions form a coherent distal axis;
2. direct/chain straightness remains plausible;
3. the axis crosses a Touch+ silhouette corridor whose cross-section is finger-like rather than fist-like.

Accepted perspective mode is reported as:

```text
PERSPECTIVE_SILHOUETTE
```

A broad path becomes:

```text
PERSPECTIVE_PATH_TOO_WIDE
```

and stays rejected.

## Physical safety regression from pair 011

Using the existing 2B.9B full-frame landmarks and Touch+ appearance component, pair 011's axis traverses a silhouette cross-section around **9.4 phalanx scales wide**.

Successful physical guided fingers are roughly **0.5–1.3 phalanx scales wide** in the equivalent check.

2B.9B.1 therefore uses a generous maximum ratio of 2.2 for the perspective fallback. This keeps pair-011-like fist/knuckle paths out while allowing real foreshortened narrow fingers.

Pair 002's existing full-frame distal chain is internally contradictory, so it remains rejected unless ROI reacquisition returns better anatomy.

## 3. Reacquisition disagreement gate

Several ROI scales may produce candidates.

If two similarly strong `GUIDED_DISTAL` candidates are materially separated in full-frame coordinates, 2B.9B.1 returns:

```text
GUIDED_REJECTED
reason = multiple strong reacquisition candidates disagree
```

The evaluator does not hide anatomical uncertainty by selecting whichever DNN confidence is microscopically larger.

## Synthetic regressions

The 2B.9B.1 self-test retains the original high-confidence/proximal-tip regression and adds:

- foreshortened narrow index: strict 2D fails, perspective silhouette proof passes;
- broad fist/knuckle path: perspective fallback rejects;
- background-derived palm-core ROI creation;
- ROI screen-coordinate remapping back to full image;
- existing distal projection remains the endpoint owner.

Synthetic PASS remains necessary but is not physical acceptance.

## Existing-dataset re-test gate

No new Touch+ capture is required for the first 2B.9B.1 test.

Re-run the same pair-001 background + pairs 002–011 images through the new evaluator.

Target:

```text
>= 8 / 10 anatomically correct GUIDED_DISTAL
0 confidently wrong GUIDED_DISTAL
```

Any additional recovery must come from ROI/pose evidence, not from weakening terminal projection or trusting model Z.

## Hard invariants

```text
exact_tip_oracle_policy = DISABLED_AFTER_2B9A_PHYSICAL_FAIL
metric_z_source         = TOUCHPLUS_STEREO_ONLY
reacquisition_policy    = FULL_THEN_SILHOUETTE_ROI
```

No changes to:

- `K/D/R/T/P/Q`;
- accepted Phase 2A surface frame;
- persistent Etron capture;
- robust Touch+ stereo matcher;
- V8 temporal safety gate.

## Merge rule

PR #9 remains **Draft / DO NOT MERGE**.

Phase 2C remains blocked.
