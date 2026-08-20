# Phase 2B.9A — offline landmark oracle evaluation

Date: 2026-08-20  
Physical unit: `0101007379`

## Goal

Determine whether a modern hand-landmark model can reliably identify the **2D distal index tip** on real Touch+ imagery before adding any DNN runtime dependency to the stable Win32 Etron capture process.

This slice is deliberately an **evaluation boundary**, not a replacement tracker.

## Why this boundary exists

Phase 2B.8 physically proved two important things:

1. the temporal geometry safety gate can reject implausible identity changes before stereo;
2. a correct index lock is possible, but geometry-only recall and continuity remain too intermittent.

The next question is therefore not "which heuristic coefficient should change?" It is:

> does a modern anatomical landmark model generalize to the Touch+ image domain well enough to act as an independent 2D oracle/veto?

## Candidate

The probe uses the official OpenCV Zoo MediaPipe models:

- `palm_detection_mediapipe_2023feb.onnx`
- `handpose_estimation_mediapipe_2023feb.onnx`

and the official OpenCV Zoo Python preprocessing/postprocessing modules downloaded locally during setup.

The HandPose model estimates 21 hand landmarks after palm detection.

## Hard architecture rules

The landmark probe:

- consumes local LEFT-eye images only;
- may output a 2D `INDEX_FINGER_TIP` hypothesis;
- may compare that hypothesis with a geometry sidecar when available;
- **never** supplies Touch+ metric Z;
- **never** modifies `K/D/R/T/P/Q`;
- **never** modifies the accepted surface frame;
- **never** loosens the robust stereo matcher;
- does not run inside the Win32 Etron process in 2B.9A.

Touch+ stereo/Q remains the only metric XYZ source.

## Tools

### `setup-touchplus-landmark-probe.ps1`

Creates a local Python virtual environment, installs:

- NumPy;
- OpenCV Python 4.10+;

then downloads the official OpenCV Zoo:

- `mp_palmdet.py`
- `mp_handpose.py`
- full-precision palm detector ONNX;
- full-precision handpose ONNX.

The ONNX downloads are SHA-256 checked.

The int8 handpose model is intentionally not used because OpenCV Zoo warns that its accuracy can drop enough to produce invalid results.

### `touchplus_landmark_probe.py`

Processes one LEFT image or a directory of images.

For each image it:

1. finds palms;
2. runs 21-landmark hand pose estimation;
3. reads landmark 8 (`INDEX_FINGER_TIP`);
4. applies a conservative 2D "index extended" sanity check;
5. optionally compares the landmark tip with a geometry JSON sidecar;
6. writes annotated images and `landmark-summary.json`.

The current arbitration is diagnostic only.

Possible statuses:

```text
AGREE_LOCKED
AGREE_DIAGNOSTIC
DISAGREE_VETO
LANDMARK_ONLY_DIAGNOSTIC
ORACLE_NON_INDEX_POSE
ORACLE_UNAVAILABLE
ERROR
```

`DISAGREE_VETO` means only that a future live integration should prefer UNKNOWN when independent anatomy disagrees. It does not mutate the current V8 runtime.

## Local capture source

The evaluation kit also packages the existing persistent calibration-capture executable under the explicit name:

```text
touchplus_landmark_capture.exe
```

It is reused only because it already provides reliable persistent stereo capture and local PNG output. No checkerboard is required.

A recommended physical dataset is 8–12 single-index poses:

- vertical;
- horizontal left/right;
- diagonal;
- near/far;
- modest palm translations;
- at least two poses where geometry V8 previously struggled.

Keep the whole hand visible.

## Physical acceptance gate for the oracle

The model is worth integrating only if real Touch+ LEFT imagery shows:

- reliable palm/hand detection on clearly visible single-hand frames;
- landmark 8 visually lands on the distal index tip, not knuckle/palm/wrist;
- failures mostly become `ORACLE_UNAVAILABLE` rather than confident wrong tips;
- orientation changes do not systematically swap index with another finger;
- the result is materially more anatomically stable than geometry-only V8 in the difficult poses.

A practical first threshold is at least **8/10 clearly visible single-index frames anatomically correct** before considering live integration.

This is not a product accuracy claim; it is a go/no-go engineering gate for whether the oracle is worth integrating.

## If the oracle passes

Next slice: **2B.9B live landmark-assisted identity**.

Preferred architecture:

```text
Win32 Touch+ geometry V8
        |
        +---- 2D candidate / palm state
        |
        v
landmark oracle process (2D only)
        |
        +---- agree --------> identity evidence strengthened
        |
        +---- disagree -----> UNKNOWN
        |
        +---- unavailable --> geometry keeps its own conservative gates

ONLY accepted identity
        |
        v
existing Touch+ stereo refinement
        |
        v
Xsurface / Ysurface / H
```

The exact IPC mechanism is intentionally deferred until the offline physical generalization test passes.

## If the oracle fails

Do not integrate it. Preserve 2B.8 safety behavior and revisit the geometry/anatomical representation rather than adding an AI dependency that is unreliable on this sensor.

## Merge rule

PR #9 remains Draft / DO NOT MERGE.

Phase 2C touch/click remains blocked until fingertip identity is physically reliable.
