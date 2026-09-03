# Phase 2B.10M — Google MediaPipe Hand Landmarker benchmark

Status: **DIAGNOSTIC-ONLY / OFFLINE / NOT A RUNTIME PROMOTION**

## Purpose

Evaluate the current **Google MediaPipe Tasks Hand Landmarker** on real Touch+ LEFT-eye captures, separately from the earlier OpenCV Zoo PalmDet/HandPose experiments.

The question is deliberately narrow:

> Can MediaPipe reliably identify the anatomical index chain and provide a useful distal direction/seed for the existing Ractiv-style local refiner?

This phase does **not** ask MediaPipe to estimate Touch+ metric depth.

## Hard safety boundary

Phase 2B.10M must not modify or override:

- accepted Touch+ stereo calibration;
- Q / metric depth;
- the validated surface frame or `H`;
- Phase 2B authoritative output;
- Phase 2C contact semantics;
- `K / D / R / T / P / Q` controls;
- Windows input injection.

MediaPipe image-space `z` and world landmarks may be recorded in JSON for diagnostics, but are explicitly **not metric authority**. Touch+ stereo + Q + surface mapping remain the only accepted path to metric XYZ/H.

## Tooling

- `revival/tools/setup-touchplus-mediapipe-benchmark.ps1`
  - creates a dedicated venv under `%LOCALAPPDATA%\TouchPlus\MediaPipeBenchmark` (outside the Git working tree);
  - installs pinned `mediapipe==1.0.1` plus OpenCV/Numpy;
  - downloads Google's versioned Hand Landmarker bundle (`float16/1`);
  - records model URL + SHA-256 + package version in local provenance metadata;
  - runs a dependency-free self-test and a real model-load/inference smoke.
- `revival/tools/run-touchplus-mediapipe-benchmark.ps1`
  - wrapper for offline runs;
  - defaults to LEFT-tagged images only.
- `revival/tools/touchplus_mediapipe_benchmark.py`
  - runs MediaPipe in IMAGE mode;
  - records all 21 image landmarks and diagnostic world landmarks;
  - highlights landmarks 5/6/7/8 (INDEX MCP/PIP/DIP/TIP);
  - writes overlays, per-image JSON, `summary.json`, and `summary.csv`;
  - defaults its output to `%LOCALAPPDATA%\TouchPlus\MediaPipeBenchmark\output`, keeping `git status` clean.

## Recommended replay dataset

Prefer the same real LEFT-eye captures previously used to judge the OpenCV Zoo / guided-distal experiments. That gives us an A/B comparison on identical physical poses without recapturing anything.

Default input filtering is conservative: only paths whose filename or parent directory contains a standalone `LEFT` token are processed. `--eye all` exists only for datasets known to contain LEFT images exclusively.

## Manual review classes

For each pose, inspect the generated overlay and classify the MediaPipe index result as one of:

1. `TIP_GOOD` — landmark #8 is on/very near the visible distal index tip.
2. `TIP_PROXIMAL_BUT_AXIS_GOOD` — #8 is visibly proximal, but the 5→6→7→8 chain identifies the correct index and points toward the true distal. This can still be valuable as a seed for the Ractiv-style refiner.
3. `WRONG_FINGER` — confident anatomy points to the wrong finger/knuckle/hand region.
4. `UNAVAILABLE` — no usable hand/index result.

The tool's own `LIKELY_EXTENDED` flag is **diagnostic only** and must never replace visual review.

## Gate for any future live integration

No runtime integration is authorized by this benchmark alone.

A future proposal may be considered only if the real dataset shows:

- no convincing confident `WRONG_FINGER` cases in the intended pointing poses;
- useful availability across varied pointing orientations;
- either accurate `TIP_GOOD` output or repeatable `TIP_PROXIMAL_BUT_AXIS_GOOD` output that the already-validated local distal refiner can improve;
- fail-closed behavior when MediaPipe is unavailable or contradictory.

Even if this gate is promising, any live integration must start as **shadow/diagnostic** and must pass a separate hardware smoke before it can influence authoritative fingertip selection.

## Run

From the repository root:

```powershell
.\revival\tools\setup-touchplus-mediapipe-benchmark.ps1

.\revival\tools\run-touchplus-mediapipe-benchmark.ps1 `
  -Input "C:\path\to\your\existing\landmark-captures"
```

Expected outputs:

```text
%LOCALAPPDATA%\TouchPlus\MediaPipeBenchmark\output\
├── annotations\
├── per-image\
├── summary.csv
└── summary.json
```

Upload or zip **that output folder only** for review. No Touch+ camera calibration, surface file, or runtime binary is required for this offline benchmark.
