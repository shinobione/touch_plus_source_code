# Phase 2B.10M — Google MediaPipe Hand Landmarker benchmark

Status: **DIAGNOSTIC-ONLY / OFFLINE / STANDALONE ORACLE FAILED / M.1 ADVISORY FOLLOW-UP PASSED ITS ARCHIVED-DATASET SAFETY GATE**

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

## Archived dataset results

### Guided set

```text
no-hand control            : correct NO_HAND
real-hand detection        : 10 / 10
TIP_GOOD                   : 8 / 10
TIP_PROXIMAL_BUT_AXIS_GOOD : 2 / 10
WRONG_FINGER               : 0 / 10
```

### Original pointing set

```text
TIP_GOOD                   : 2 / 10
TIP_PROXIMAL_BUT_AXIS_GOOD : 5 / 10
WRONG_FINGER               : 2 / 10
UNAVAILABLE                : 1 / 10
```

The original pointing set therefore failed the standalone identity-oracle gate. In particular, confident MediaPipe hand output could still attach landmarks 5→6→7→8 to the wrong physical finger. Model confidence and handedness must not be treated as fingertip identity authority.

## Standalone verdict

**MediaPipe standalone index/fingertip oracle: FAIL.**

It is useful as anatomical evidence but unsafe as the source that chooses the finger or publishes the final 2D fingertip.

Binding rule remains:

> **wrong finite fingertip = blocker; UNKNOWN/reject is safe.**

## Follow-up: Phase 2B.10M.1 advisory fusion

A narrower fail-closed role was implemented in `phase2b10m1-mediapipe-advisory-fusion.md`:

- the conservative 2B.9B.1 `GUIDED_DISTAL` path remains the offline identity baseline;
- MediaPipe cannot rescue a rejected baseline;
- MediaPipe cannot own the output pixel;
- only a MediaPipe 5/6/7/8 chain that independently agrees with the baseline may expose an **axis advisory**;
- disagreement produces an advisory reject while leaving the baseline untouched.

The 2026-09-04 archived physical-dataset replay passed its **primary offline safety gate**:

```text
ADVISORY_AXIS_ACCEPT                    : 6 / 10
REJECT_MEDIAPIPE_IDENTITY_DISAGREEMENT : 1 / 10
BASELINE_REJECT_NO_MEDIAPIPE_RESCUE    : 3 / 10
```

Both known standalone wrong-finger stress cases were neutralized:

- `pair-005`: explicit MediaPipe identity disagreement reject;
- `pair-010`: baseline rejected, MediaPipe forbidden from rescue.

No accepted advisory axis came from either known wrong-finger case. This is a safety/architecture pass only, **not** a runtime promotion and **not** a live hardware pass.

## Gate for any future live integration

No runtime integration is authorized by M or M.1 alone.

Any future proposal must remain shadow-only at first and preserve all existing frame-synchronization, stale-result, identity, stereo and metric-depth safety gates. MediaPipe may only be considered as an additional advisory feature when independent Touch+ identity already exists.

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
