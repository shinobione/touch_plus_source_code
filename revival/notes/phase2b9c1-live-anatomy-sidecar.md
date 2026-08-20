# Phase 2B.9C.1 — live anatomical sidecar fusion

Date: 2026-08-20  
Physical unit: `0101007379`

## Status

**IMPLEMENTED / CI + PHYSICAL LIVE SMOKE REQUIRED / DO NOT MERGE**

This slice is the first controlled live integration of the landmark-guided distal approach validated offline in 2B.9B.1.

Raw personal images/videos remain outside the repository.

## Binding input from 2B.9B.1

The same ten-pose LEFT-eye physical dataset produced:

```text
GUIDED_DISTAL                : 8 / 10
visually plausible published : 8 / 8
wrong guided tips observed   : 0 / 8
GUIDED_REJECTED              : 2 / 10
GUIDED_UNAVAILABLE           : 0 / 10
```

ROI reacquisition rescued pairs 007, 008 and 010. Pair 007 is retained as a high-extension regression. Pair 011 remains the broad fist/knuckle fail-closed safety class.

## Non-negotiable ownership

```text
model / MediaPipe Z : DISABLED
metric XYZ source   : TOUCHPLUS_STEREO_ONLY
exact landmark 8    : DIAGNOSTIC ONLY
```

2B.9C.1 does not modify accepted `K/D/R/T/P/Q`, Phase 2A surface frame, persistent Etron capture or the hardened Phase 1C stereo matcher.

## Process architecture

OpenCV/ONNX still does **not** enter the Win32 Etron process.

```text
Win32 Touch+ runtime
  persistent capture
  V5 background appearance silhouette
  V6 physical support
  V8 palm + temporal geometry safety
        |
        | LEFT gray + current Touch+ silhouette only
        v
named shared memory
        |
        v
Python/OpenCV Zoo sidecar
  full-frame PalmDet/HandPose
       -> if needed silhouette ROI reacquisition
       -> index MCP/PIP/DIP direction
       -> guided distal projection to Touch+ silhouette edge
        |
        | 2D result only (NO Z)
        v
Win32 conservative fusion
        |
        v
existing stereo/Q -> Xsurface/Ysurface/H
```

Shared-memory ABI:

```text
Local\TouchPlusRevival2B9C_Frame_v1
Local\TouchPlusRevival2B9C_Result_v1
```

A sequence-lock guards each packet against half-written reads.

## Live anatomy packet

The sidecar result includes only information needed for 2D identity:

- frame ID / age;
- status `GUIDED_DISTAL / GUIDED_REJECTED / UNAVAILABLE / ERROR`;
- source `FULL_FRAME / ROI_1 / ROI_2 / ROI_3`;
- pose mode;
- guided `(x,y)`;
- distal axis direction/quality;
- hand confidence;
- silhouette corridor continuity/lateral/extension diagnostics.

There is deliberately no metric/model Z field.

## Temporal anatomy gate

One DNN frame cannot instantly become a final identity.

The live anatomy track uses:

```text
UNKNOWN -> ACQUIRING -> LOCKED
```

Two compatible fresh guided observations are required for LOCKED. Duplicate packets do not advance acquisition. Large unexplained 2D anatomy jumps fail closed. Stale sidecar output fails closed.

## Conservative fusion

Fusion happens **before stereo**.

### Geometry + anatomy

If V8 already publishes a locked geometry branch and anatomy is locked, the two tips must agree within a palm-relative spatial gate.

```text
agree    -> fused identity may publish
 disagree -> UNKNOWN, stereo NOT_RUN
```

The published 2D point is the landmark-guided Touch+ silhouette boundary, not raw MediaPipe landmark 8.

### Anatomy-only recovery

2B.8 physical testing showed that geometry can be too intermittent even with an obvious index. 2B.9B.1 proved a pair-007-like class where guided anatomy recovers a correct distal point.

Therefore anatomy-only recovery is allowed only when all of the following hold:

1. a valid current V8 hand/palm exists;
2. V8 has not rejected the palm temporally;
3. V8 has not rejected a large geometry identity jump;
4. anatomy is temporally LOCKED;
5. the guided anatomy point lies on/near the **current Touch+ silhouette**;
6. the sidecar did not explicitly reject the pose.

The recovery gets a separate high-bit identity ID so metric smoothing cannot silently inherit a geometry branch's history.

## Fail-closed rules

The following produce final identity `UNKNOWN` and prevent stereo:

- no fresh locked anatomy;
- sidecar unavailable/error/stale;
- explicit anatomy reject;
- anatomy temporal jump reject;
- invalid current V8 palm;
- V8 palm temporal reject;
- V8 geometry jump reject;
- geometry/anatomy disagreement when both are locked;
- anatomy tip outside the current Touch+ silhouette.

A strong stereo match never overrides these failures.

## Diagnostics

2B.9C.1 runtime banner:

```text
[TRACK] PHASE 2B.9C.1 RUNTIME ACTIVE | tracker=V8+LIVE-ANATOMY-SIDECAR
```

Overlay:

```text
cyan    = V8 palm
white   = V8 geometry candidate
magenta = live landmark-guided distal candidate
```

Heartbeat reports geometry state, anatomy status/track/source/age, fusion mode/reason, and stereo confidence separately.

## Launcher

Physical testing must start via:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\start-touchplus-phase2b9c.ps1
```

The launcher bootstraps the existing landmark environment/assets if needed, starts the Python sidecar, then starts the Win32 tracker, and stops the sidecar when the tracker exits.

The artifact does **not** contain the accepted `surface/0101007379.json`; copy that separate Phase 2A artifact into `surface/` before physical smoke.

## Synthetic / CI gates

Required on the final candidate SHA:

- V8 x64 self-test;
- V8 Win32 self-test;
- 2B.9C fusion x64 self-test;
- 2B.9C fusion Win32 self-test;
- 2B.9B.1 ROI regression;
- sidecar shared-memory/ROI ABI self-test;
- real OpenCV Zoo model-load smoke;
- full Revival Win32 build;
- Phase 2A surface regression;
- Phase 1C calibration/Q regression;
- identifiable Win32 artifact packaging.

The fusion self-test specifically protects:

- geometry+anatomy agreement;
- disagreement -> UNKNOWN;
- explicit anatomy reject;
- stale sidecar -> UNKNOWN;
- pair-007 anatomy-only rescue;
- pair-011 rejection;
- current-silhouette support;
- HIGH stereo cannot override LOW identity.

Synthetic PASS is not physical acceptance.

## Physical smoke gate

After CI is fully green:

1. start through the launcher;
2. learn background with `B`;
3. verify no-hand remains quiet;
4. test one clear index vertical / horizontal / diagonal;
5. include a pair-007-like perspective/foreshortened pose;
6. include a broad/front-facing fist/two-finger ambiguity class that should fail closed;
7. observe that magenta anatomy can rescue true distal identity;
8. observe geometry/anatomy disagreement becoming `UNKNOWN` before stereo;
9. verify any finite `(Xsurface,Ysurface,H)` remains anatomically on the real index fingertip.

Binding project rule remains:

> **wrong finite/HIGH fingertip = BLOCKER**

and:

> **UNKNOWN is acceptable when identity is uncertain**

## Merge rule

PR #9 remains **Draft / DO NOT MERGE** until the physical 2B.9C.1 smoke passes.

Phase 2C touch/click remains blocked.
