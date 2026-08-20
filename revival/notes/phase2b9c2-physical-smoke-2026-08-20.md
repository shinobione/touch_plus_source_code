# Phase 2B.9C.2 physical smoke — 2026-08-20

## Verdict

**PASS — Phase 2B physically accepted.**

This note records the final live hardware gate for the Touch+ fingertip identity stack. No personal raw video or extracted frame is committed to the repository.

The accepted pre-merge code head was:

`a33cadd2fbbb655435078718c9ecf7a66c61562a`

PR #9 was squash-merged after the physical pass as:

`e43e6445708b4fd27d431956e404ae4fd0d8ceae`

## Physical observations

The live smoke deliberately stressed the asynchronous sidecar with stable poses, slow motion and fast pose/orientation changes, especially right/left-oriented index -> frontal / foreshortened transitions.

Observed behavior:

```text
background/no-hand                         PASS
live anatomy + ROI reacquisition           PASS
stable age=1 MOTION_COMPENSATED cases      PASS
TIP_NOT_CURRENT_DISTAL on fast transition  PASS
SHAPE_CHANGED fail-closed                  PASS
PALM_SCALE_CHANGED fail-closed             PASS
age>1 anatomy-only blocked                 PASS
large geometry/anatomy disagreement        PASS -> UNKNOWN / stereo NOT_RUN
valid static/slow distal fingertips        PASS
wrong finite MEDIUM/HIGH observed          0 in this smoke
```

The binding 2B.9C.1 failure class is closed on hardware: a stale anatomy result that is still inside the current hand but is no longer a current distal fingertip is rejected before stereo instead of being promoted to a finite HIGH XYZ.

Representative safe outcomes observed during violent transitions included:

```text
sync=TIP_NOT_CURRENT_DISTAL
fusion=UNKNOWN
stereo_confidence=NOT_RUN
fingertip=UNKNOWN
```

and:

```text
sync=PALM_SCALE_CHANGED
fusion=UNKNOWN
stereo=NOT_RUN
```

The smoke also exercised shape-change and too-old anatomy rejection, while preserving valid static/slow fingertip outputs.

## Binding safety rule

> **wrong finite/HIGH fingertip = BLOCKER**

> **UNKNOWN is acceptable when identity is uncertain**

This rule remains in force for Phase 2C. Touch/contact semantics must never convert identity uncertainty into a touch event.

## Accepted ownership boundaries

The physical closeout does not change lower-layer ownership:

```text
metric_z_source          = TOUCHPLUS_STEREO_ONLY
raw landmark 8           = DIAGNOSTIC ONLY
OpenCV/ONNX in Etron EXE = NO
K/D/R/T/P/Q              = UNCHANGED
Phase 2A surface frame   = UNCHANGED
persistent capture       = UNCHANGED
stereo matcher           = UNCHANGED
```

The sidecar contributes 2D anatomy/direction only. The Touch+ silhouette owns the actual distal boundary and the accepted Touch+ stereo/Q path owns metric XYZ.

## CI revalidated immediately before merge

On exact head `a33cadd2fbbb655435078718c9ecf7a66c61562a`:

- Revival Fingertip 3D #117 — **SUCCESS**
- Revival Windows Build #292 — **SUCCESS**
- Revival Surface Frame #136 — **SUCCESS**

## Closeout

Phase 2B is complete at the current acceptance boundary: the runtime can publish a physically plausible single-index fingertip in surface-relative metric coordinates while failing closed during identity ambiguity or fast asynchronous pose changes.

Next canonical phase: **Phase 2C — touch/contact detection**.
