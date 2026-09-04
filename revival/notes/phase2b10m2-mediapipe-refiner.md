# Phase 2B.10M.2 — MediaPipe advisory axis + Ractiv-style distal refiner

Status: **OFFLINE / SHADOW-ONLY / REPLAY PENDING / DO NOT MERGE**

## Question

Phase 2B.10M proved that Google MediaPipe cannot own index identity: the original pointing set contained two visually confirmed wrong-finger cases.

Phase 2B.10M.1 then proved a narrower safety architecture on the same archived physical data:

- MediaPipe cannot rescue a rejected baseline;
- MediaPipe cannot publish a fingertip;
- only a 5→6→7→8 chain that agrees with the conservative 2B.9B.1 identity baseline may expose an advisory axis;
- the two known wrong-finger stress cases were neutralized.

M.2 asks the next narrow question:

> When M.1 accepts the MediaPipe axis, does that axis help the existing Ractiv-style local distal-refinement idea recover the conservative distal reference at least as well as the baseline anatomy axis?

## Important archived-data limitation

The archived image dataset does **not** contain the exact modern live A pixel, live V6 downscaled hand mask, live stereo support or frame state that `fingertip_refiner_v10.h` receives in the runtime.

Therefore M.2 is deliberately **not** presented as a bit-for-bit C++ runtime replay.

The offline tool uses:

- the existing 2B.9B.1 result as conservative identity;
- `index_tip_model` as the same proximal/coarse seed for both refiner runs;
- `GUIDED_DISTAL` as a **comparison reference**, not ground truth;
- the archived full-resolution learned-background primary hand component as the local support mask;
- a Python behavioral mirror of the accepted V10 bounded corridor/component/distal-cap logic and constants.

This isolates the value of the **axis** without claiming data we do not possess.

## Experiment

Only frames with:

```text
M.1 decision = ADVISORY_AXIS_ACCEPT
```

are allowed to enter M.2.

For each eligible frame:

```text
                         same proximal seed
                     2B.9B.1 index_tip_model
                              |
                 +------------+------------+
                 |                         |
                 v                         v
       baseline anatomy axis        MediaPipe advisory axis
                 |                         |
                 v                         v
       V10-style local refiner      V10-style local refiner
                 |                         |
                 +------------+------------+
                              |
                              v
                 compare both shadow points
                     to 2B.9B.1 GUIDED_DISTAL
```

The baseline `GUIDED_DISTAL` point stays unchanged. Neither refined point is authoritative.

## Safety gate

- any M.1 reject/no-rescue frame → `NOT_RUN_M1_GATE`;
- MediaPipe cannot create/reacquire identity;
- MediaPipe cannot rescue a missing baseline;
- known standalone wrong-finger cases must never enter the refiner;
- any visually wrong finite magenta MediaPipe-axis shadow candidate is a **hard fail**;
- distance to `GUIDED_DISTAL` is comparative evidence only and must not be called physical ground truth;
- no stereo, Q, surface `H`, Phase 2C or OS injection is run or modified.

Diagnostic outcomes include:

```text
MP_AXIS_CLOSER_TO_BASELINE_REFERENCE
LEGACY_AXIS_CLOSER_TO_BASELINE_REFERENCE
AXIS_PARITY_TO_BASELINE_REFERENCE
MP_AXIS_REFINER_REJECT
MP_AXIS_SHADOW_REGRESSION
MP_ONLY_SHADOW_ACCEPT
NOT_RUN_M1_GATE
```

## Tooling

- `revival/tools/touchplus_mediapipe_refiner_benchmark.py`
- `revival/tools/run-touchplus-mediapipe-refiner-benchmark.ps1`

Outputs stay under `%LOCALAPPDATA%\TouchPlus\MediaPipeBenchmark` by default:

```text
output-refiner-m2\
├── annotations\
├── legacy-2b9b1\
├── per-image\
├── summary.csv
└── summary.json
```

Raw user captures and generated overlays must remain local and are not committed.

## Primary replay

Use the same original 10-frame pointing set used by M and M.1. No new camera capture is required.

A promising result requires, at minimum:

1. the known wrong-finger M cases remain `NOT_RUN_M1_GATE`;
2. no visually wrong finite MediaPipe-axis shadow candidate;
3. accepted MediaPipe-axis candidates are mostly parity/better relative to the conservative distal reference rather than regressions.

Even a promising M.2 result would remain **offline evidence only**. Any live use must be a separate shadow-only runtime slice with its own frame-sync and physical gate.
