# Phase 2B.9C.2 — frame-synchronous anatomy fusion

Date: 2026-08-20  
Physical unit: `0101007379`

## Status

**IMPLEMENTED CANDIDATE / SYNTHETIC + CI REQUIRED / PHYSICAL LIVE SMOKE REQUIRED / DO NOT MERGE**

## Goal

Close the specific physical 2B.9C.1 failure where a valid distal result from frame N arrived at frame N+1 after a fast pose change, remained somewhere inside the new hand silhouette, and was therefore allowed to reach stereo even though it was no longer the current fingertip.

2B.9C.2 does not change the model or metric stack. It changes the **time ownership** of the anatomy result.

## Architecture

The Win32 tracker now retains a small history of per-frame 2D state:

```text
frame_id
Touch+ selected silhouette (320x240)
V8 palm center
V8 palm radius
```

When the sidecar returns a `GUIDED_DISTAL` result:

```text
result source frame_id
        |
        v
find source snapshot
        |
        v
compare source/current palm motion + scale
        |
        v
align source silhouette to current palm
        |
        v
require sufficient shape overlap
        |
        v
transport source tip by palm translation/scale
        |
        v
CURRENT-DISTAL validation
        |
        +-- fail --> UNKNOWN / stereo NOT_RUN
        |
        v
TemporalAnatomyGate
        |
        v
conservative geometry/anatomy fusion
```

No rotation is invented. If the hand rotates enough that translation/scale cannot preserve a current distal interpretation, the result fails closed.

## Current-distal validation

A synchronized anatomy point must satisfy more than the old 2B.9C.1 check "near any current hand pixel".

The compensated point must:

- be on/very near the current Touch+ silhouette;
- be near a silhouette boundary;
- have silhouette support inward along the anatomical distal axis;
- clear the silhouette outward along that axis.

This is specifically intended to reject the physical failure class where an old fingertip becomes an interior knuckle/palm pixel while still remaining inside the hand mask.

## Sync statuses / diagnostics

The runtime exposes:

```text
CURRENT
MOTION_COMPENSATED
MISSING_SOURCE_FRAME
PALM_INVALID
PALM_MOTION_TOO_LARGE
PALM_SCALE_CHANGED
SHAPE_CHANGED
TIP_NOT_CURRENT_DISTAL
TOO_OLD
```

Relevant fail-closed fusion reasons include:

```text
anatomy-stale-motion
anatomy-not-current-distal
anatomy-only-too-old
anatomy-only-sync-shape-weak
```

Heartbeat telemetry reports sync status, shape overlap and palm shift in addition to source/age/fusion/stereo state.

## Conservative anatomy-only rule

Anatomy-only rescue remains useful (pair-007-style behavior), but becomes stricter:

- age 0: eligible if all normal gates pass;
- age 1: eligible only after frame compensation, current-distal validation and strong aligned-shape overlap;
- age >1: **not eligible for anatomy-only publication**;
- geometry+anatomy agreement may still use a synchronized age <=2 result because the current V8 geometry supplies an independent current-frame anatomical check.

## Synthetic physical regression

The fusion self-test now contains two synchronization boundaries:

### Stable one-frame translation

A valid source fingertip on frame N is followed by the same hand translated slightly on N+1.

Expected:

```text
sync=MOTION_COMPENSATED
transported tip reaches the translated current distal
candidate remains eligible
```

### 2B.9C.1 fast-pose failure class

A one-frame-old fingertip remains inside the current hand silhouette after a large pose change / broad knuckle region.

The test deliberately verifies that the **old 2B.9C.1 silhouette-near gate would pass**, then requires the new frame-sync/current-distal layer to reject it:

```text
old gate: point still near current hand = true
new sync: SHAPE_CHANGED or TIP_NOT_CURRENT_DISTAL
fusion: UNKNOWN
stereo: must never be allowed to legitimize that point
```

The test is synthetic and exists to encode the derived physical failure class. It is not physical acceptance.

## Hard ownership remains

```text
metric_z_source          = TOUCHPLUS_STEREO_ONLY
raw landmark 8           = DIAGNOSTIC ONLY
OpenCV/ONNX in Etron EXE = NO
K/D/R/T/P/Q              = UNCHANGED
Phase 2A surface frame   = UNCHANGED
stereo matcher           = UNCHANGED
```

## Physical acceptance target

Repeat the live pose sequence with special emphasis on quick right/left -> frontal transitions.

Required:

- previously good live `GEOMETRY+ANATOMY` and ROI anatomy-only cases still exist;
- quick pose transitions prefer `UNKNOWN` if synchronization cannot be proved;
- `TIP_NOT_CURRENT_DISTAL`, `SHAPE_CHANGED` or another explicit sync reject is acceptable and desirable during fast rotation;
- no finite MEDIUM/HIGH result may land on a non-distal hand pixel.

## Merge rule

PR #9 remains **Draft / DO NOT MERGE** until the physical live smoke passes.

Phase 2C remains blocked.
