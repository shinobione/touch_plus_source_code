# Phase 2B.10M.1 — conservative MediaPipe advisory fusion

Status: **OFFLINE / SHADOW-ONLY / FAIL-CLOSED / DO NOT MERGE**

## Why this follow-up exists

Phase 2B.10M tested the current Google MediaPipe Tasks Hand Landmarker on two archived Touch+ LEFT-eye datasets.

The first guided set was encouraging: the no-hand control stayed `NO_HAND`, all ten real-hand frames were detected, and visual review found no wrong-finger result. Two results were visibly proximal but retained a useful distal index axis.

The second/original pointing set exposed the blocker: MediaPipe can understand the hand while assigning landmarks 5→6→7→8 to the wrong physical finger with non-trivial confidence. It also had one real-hand `NO_HAND` case. Therefore MediaPipe is **not** an index-identity oracle and must never independently publish or rescue a Touch+ fingertip.

Binding rule remains:

> **wrong finite fingertip = blocker; UNKNOWN/reject is safe.**

## M.1 question

A narrower use can still be valuable:

> Can MediaPipe contribute a distal anatomical axis only when it independently agrees with an already-conservative Touch+ identity path?

For this offline benchmark the baseline is the existing **2B.9B.1 `GUIDED_DISTAL`** path: OpenCV-Zoo anatomy + Touch+ learned-background silhouette + fail-closed distal projection/ROI reacquisition. That path was previously physically reviewed at 8/10 published, 0 observed wrong published tips, 2 safe rejects.

This is a benchmark baseline, **not a runtime promotion** and not a replacement for the current authoritative Phase 2B output.

## Fusion policy

```text
2B.9B.1 baseline identity
        |
        +---- rejected/uncertain ------------------> NO RESCUE
        |
        v
baseline GUIDED_DISTAL
        |
        +-----------------------------+
        |                             |
        v                             v
baseline index chain             Google MediaPipe
+ distal axis                    5/6/7/8 chain
        |                             |
        +--------- agreement gate ----+
                      |
             +--------+--------+
             |                 |
          disagree            agree
             |                 |
             v                 v
      MP advisory REJECT   MP axis advisory
      baseline unchanged   allowed shadow-only
```

MediaPipe **never owns the output pixel**. M.1 records whether its axis may be consumed by a later refiner experiment; it does not publish a new fingertip.

If MediaPipe is unavailable while the baseline is valid, the baseline remains valid and MediaPipe contributes nothing. If the baseline rejects, MediaPipe is forbidden from rescuing it.

## Conservative agreement gates

All distances are normalized by the baseline distal scale. Initial thresholds are deliberately diagnostic and are recorded in every `summary.json`:

- baseline must be `GUIDED_DISTAL` with valid index chain + distal axis;
- MediaPipe PIP→TIP axis cosine must be at least `0.80` (~36.9° maximum angular disagreement);
- full 5/6/7/8 chain RMS disagreement <= `1.25` baseline distal scales;
- MediaPipe #8 to baseline model #8 <= `1.30` scales;
- MediaPipe #8 lateral distance from the baseline distal corridor <= `0.90` scales;
- MediaPipe #8 may be proximal (`-4.0` scales) but may not extend materially past the baseline distal boundary (`+0.60` scales);
- projected MediaPipe 5→6→7→8 order must remain monotonic along the baseline distal axis;
- multiple passing MediaPipe hands that remain spatially ambiguous cause a reject.

These values are **not runtime constants**. The archived physical datasets decide whether the gate is useful; any unexpected finite wrong-finger advisory is a hard fail.

## Tooling

- `revival/tools/touchplus_mediapipe_fusion_benchmark.py`
  - runs 2B.9B.1 and Google MediaPipe on the same LEFT image;
  - compares only image-space index identity/axis evidence;
  - writes per-frame fusion decisions, metrics, overlays, CSV and JSON;
  - ignores MediaPipe image/world Z;
  - cannot publish or rescue a fingertip by construction.
- `revival/tools/run-touchplus-mediapipe-fusion-benchmark.ps1`
  - reuses the isolated `%LOCALAPPDATA%\TouchPlus\MediaPipeBenchmark` venv/model;
  - auto-discovers the archived `landmark-assets` directory beside the capture folders;
  - auto-selects `landmark-guided-captures\raw\pair-001-left.png` as the no-hand learned-background reference unless overridden.

## Primary replay gate

Replay the original pointing set because it contains the known MediaPipe wrong-finger stress cases. In particular, the visually reviewed M baseline expects the problematic MediaPipe cases to become advisory rejects rather than accepted axes.

Desired safety shape:

```text
correct/same-axis MediaPipe : ADVISORY_AXIS_ACCEPT where baseline is valid
wrong-finger MediaPipe      : REJECT_MEDIAPIPE_IDENTITY_DISAGREEMENT
MediaPipe unavailable       : KEEP_BASELINE_MEDIAPIPE_UNAVAILABLE
baseline reject             : BASELINE_REJECT_NO_MEDIAPIPE_RESCUE
wrong finite MP publication : impossible by policy
```

No new camera capture is required.

## Safety boundary

M.1 does not modify:

- authoritative Phase 2B fingertip selection;
- stereo calibration `K/D/R/T/P/Q`;
- robust stereo/Q metric XYZ;
- surface frame / `H`;
- Phase 2C contact semantics;
- Windows input injection.

Raw user captures and generated overlays remain local and must not be committed.
