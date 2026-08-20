# Phase 2B.9B — landmark-guided distal projection

Date: 2026-08-20  
Physical unit: `0101007379`

## Status

**IMPLEMENTATION / SYNTHETIC EVALUATION BOUNDARY — PHYSICAL SMOKE REQUIRED — DO NOT MERGE**

## Goal

Keep the useful anatomical information from 2B.9A without treating MediaPipe landmark 8 as ground truth.

2B.9B asks a narrower question:

> Can the model identify the index's distal **direction**, while the Touch+ learned-background silhouette finds the actual visible fingertip boundary?

This remains an offline evaluation. No OpenCV DNN/ONNX dependency is added to the stable Win32 Etron tracker runtime yet.

## Binding lesson from 2B.9A

The real Touch+ dataset showed:

- reliable model execution;
- hand detection on 8/10 LEFT captures;
- very high confidence on several captures;
- but multiple confident raw `INDEX_FINGER_TIP` points were visibly too proximal.

Therefore raw landmark-tip agreement/veto is retired.

## Architecture

```text
Touch+ LEFT frame
      +
clean LEFT background from SAME persistent session
      |
      v
V5-style appearance delta
      |
      v
changed hand silhouette component
      ^
      |
MediaPipe hand anatomy
MCP -> PIP -> DIP -> TIP direction
      |
      v
landmark-guided distal corridor
      |
      v
continuous silhouette boundary
      |
      v
GUIDED_DISTAL 2D candidate
```

The exact MediaPipe tip may be short. It participates only in estimating direction and chain consistency.

## Distal-axis model

The probe uses the index phalanx chain:

- `INDEX_MCP` (5)
- `INDEX_PIP` (6)
- `INDEX_DIP` (7)
- `INDEX_TIP` (8)

A weighted distal direction is computed from the three consecutive segment directions. Adjacent phalanges must broadly agree; folded/contradictory chains fail closed.

The scale used for the corridor is the median observed index-phalanx length, so corridor width and maximum projection distance remain relative to the current hand size rather than fixed magic pixels.

## Touch+ appearance silhouette

For this offline boundary, one clean LEFT frame is captured first in the **same persistent camera session**.

The evaluator then uses the accepted V5 appearance-only threshold as its baseline:

```text
appearance delta threshold = 24
```

The selected changed component must overlap multiple palm/index landmarks. If no coherent changed component exists, the result is `GUIDED_UNAVAILABLE` / `GUIDED_REJECTED` rather than a model-only fingertip.

This offline component-selection step is **not** claimed to replace V6 physical-support bounding in the real tracker. It exists only to test whether landmark direction + Touch+ appearance boundary solves the 2B.9A proximal-tip failure class.

## Projection safety rules

A guided tip is published by the evaluator only when:

1. hand-pose confidence is at least 0.80;
2. the pose passes the conservative 2D extended-index check;
3. MCP/PIP/DIP/TIP directions are coherent;
4. a Touch+ appearance silhouette is available;
5. the distal corridor connects continuously from around `INDEX_DIP`;
6. the selected distal edge remains inside the corridor;
7. a detached changed tail beyond a real gap cannot steal the endpoint.

Possible relevant statuses:

```text
GUIDED_DISTAL
GUIDED_REJECTED
GUIDED_UNAVAILABLE
ERROR
```

`GUIDED_UNAVAILABLE` and `GUIDED_REJECTED` are safe outcomes.

A confidently wrong guided distal point is a blocker.

## Explicitly disabled policy

2B.9B writes:

```text
exact_tip_oracle_policy = DISABLED_AFTER_2B9A_PHYSICAL_FAIL
```

The raw model `INDEX_TIP` is drawn diagnostically but is not allowed to veto a better silhouette boundary by distance alone.

## Metric ownership remains unchanged

The probe always reports:

```text
metric_z_source = TOUCHPLUS_STEREO_ONLY
```

It does not calculate or publish metric Z from MediaPipe.

If this concept is later integrated live, the accepted Touch+ stereo/Q/surface pipeline remains the sole source of `(Xsurface, Ysurface, H)`.

## Synthetic regressions

The 2B.9B self-test includes the physical 2B.9A failure class in synthetic form:

### High-confidence proximal model tip

```text
model hand confidence: ~0.998
model tip: deliberately ~28–30 px short
silhouette: continues to the true distal boundary
expected: GUIDED_DISTAL on the silhouette edge
```

The test proves that a very confident short landmark 8 does **not** cap the distal result.

Additional tests cover:

- diagonal distal projection;
- contradictory/bent phalanx directions -> `GUIDED_REJECTED`;
- missing silhouette -> `GUIDED_UNAVAILABLE`;
- non-index pose -> `GUIDED_REJECTED`.

These are synthetic engineering tests only. They are not physical acceptance.

## Physical smoke protocol

Use the packaged persistent capture executable. Keep the Touch+ stream open for one dataset.

Capture **11 pairs**:

1. pair 001: completely clear work area / no hand — this is the clean LEFT background;
2. pairs 002–011: ten clear single-index poses.

Recommended poses:

- vertical;
- horizontal left;
- horizontal right;
- diagonal left;
- diagonal right;
- nearer;
- farther;
- palm translated left;
- palm translated right;
- one pose similar to a previous V8/2B.9A difficulty.

Run the evaluator with `--background-first` so pair 001 becomes the background and only subsequent LEFT images are evaluated.

The tool now prefers `*-left.*` files by default, preventing the 2B.9A directory scan from also mixing FULL/RIGHT frames into the summary.

## Physical acceptance gate

2B.9B is worth taking toward live integration only if:

- at least **8/10** obvious single-index LEFT poses return a guided distal point visually on the actual fingertip boundary;
- successful points are materially better than the proximal raw landmark-8 failures from 2B.9A;
- failures prefer `GUIDED_UNAVAILABLE` / `GUIDED_REJECTED`;
- no confidently wrong guided distal point is observed;
- large orientation changes do not project onto another finger or an appearance tail.

The raw green model tip is not the judge anymore. The **guided distal marker on the real silhouette edge** is the judge.

## If 2B.9B passes

Next slice should be a controlled **2B.9C live anatomical sidecar integration**:

- V8 remains the temporal palm/branch safety gate;
- landmark evidence supplies index direction/identity, not Z;
- the live Touch+ silhouette owns distal boundary placement;
- disagreement or unavailable anatomy becomes `UNKNOWN`;
- only accepted 2D identity reaches the existing stereo matcher;
- the metric smoothing remains scoped to one locked branch identity.

The IPC/runtime mechanism should be chosen only after this physical projection gate passes.

## If 2B.9B fails

Do not add DNN runtime complexity. Preserve V8's safe UNKNOWN behavior and revisit the anatomical representation or sensor-domain adaptation.

## Merge rule

PR #9 remains **Draft / DO NOT MERGE**.

Phase 2C remains blocked.