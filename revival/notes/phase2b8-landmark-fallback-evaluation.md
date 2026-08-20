# Phase 2B.8 — lightweight 2D landmark fallback evaluation

Date: 2026-08-20

This note evaluates a modern 2D hand-landmark fallback for TouchPlus Revival without changing the accepted metric camera/depth/surface stack.

## Candidate reviewed

OpenCV Zoo currently publishes **MediaPipe Handpose** as an ONNX model. Its README states that it:

- estimates **21 hand keypoints** per detected hand;
- expects a hand crop produced after palm detection;
- is distributed in the OpenCV Zoo handpose directory under **Apache 2.0**;
- includes quantized variants, while explicitly warning that the int8 model can lose enough accuracy to produce invalid results.

Reference:
`opencv/opencv_zoo/models/handpose_estimation_mediapipe`

## Why it is relevant

The remaining Touch+ blocker is not metric depth. It is 2D anatomical identity: deciding which visible distal branch is the extended index before stereo refinement.

A landmark model can provide an independent `INDEX_FINGER_TIP` hypothesis from the LEFT eye. That is exactly the type of evidence missing when geometry becomes ambiguous.

## Why it is not the primary runtime yet

TouchPlus Revival currently has a stable Win32 physical-device runtime built around the historical 32-bit Etron stack. Adding a DNN inference dependency directly to that executable would:

- increase packaging/runtime complexity before we know whether the model generalizes to Touch+ imagery;
- introduce RGB-domain assumptions into a sensor whose useful appearance can be dim, low-texture and effectively grayscale;
- risk confusing model-relative landmark depth with the already physically validated Touch+ stereo/Q metric depth.

Therefore Phase 2B.8 remains **geometry-first**.

## Proposed evaluation boundary

The fallback should first be tested as an independent/offline probe:

1. consume the rectified LEFT image only;
2. run a lightweight palm detector + 21-landmark handpose model;
3. read only the 2D `INDEX_FINGER_TIP` location;
4. compare it with the geometry tracker candidate;
5. never use the model's Z as Touch+ metric depth.

Possible policy after physical evaluation:

```text
geometry identity plausible + landmark agrees
    -> identity confidence can increase

geometry identity plausible + landmark unavailable
    -> geometry may still proceed under its own gates

geometry and landmark strongly disagree
    -> UNKNOWN

landmark alone
    -> diagnostic only until physically validated
```

## Decision for current PR #9

Do **not** add the ONNX/model runtime dependency in the 2B.8 physical build yet.

First test the new temporal geometry identity boundary. If the physical fingertip still fails, the next comparison should use OpenCV Zoo MediaPipe Handpose as an independent 2D anatomical oracle/veto, while preserving:

- accepted `K/D/R/T/P/Q`;
- accepted surface frame;
- accepted robust stereo matcher;
- Touch+ stereo reconstruction as the only metric XYZ source.

This avoids another stack of arbitrary endpoint weights while also avoiding an unvalidated AI dependency in the physical runtime.
