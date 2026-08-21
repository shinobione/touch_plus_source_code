# Ractiv historical code vs TouchPlus Revival — audit checkpoint

Date: **2026-08-22**

Purpose: record the evidence behind the temporary Ractiv-first strategy. This is not a claim that the historical software was production-ready; it is a source-level comparison of what is visibly implemented.

## Historical references

User fork historical snapshot:

- repo: `shinobione/touch_plus_source_code`
- branch: `master`
- latest snapshot commit: `902cb8cc3e660b0ca8d9049fabd383c34da69607` (2015-07-20)

Official upstream final source:

- repo: `Ractiv/touch_plus_source_code`
- branch: `master`
- final observed commit: `58f29eb69e7a3f6095de83bcb43cf489d7ea2cef` (2015-12-01)

## High-level finding

Ractiv had implemented most conceptual layers of the promised product:

`Touch+ camera -> background/motion -> hand -> anatomy/fingertip -> stereo 3D -> interaction plane -> pointer/contact -> Windows/TUIO output`

The critical caveat is integration state. The July lineage is visibly wired end-to-end, while the December final `main.cpp` appears to be in a refactor where the active path stops after SCOPA despite downstream classes remaining in the tree.

## July integrated lineage

The historical `main.cpp` in this fork wires:

- `Camera`;
- IMU;
- exposure adjustment;
- `MotionProcessorNew`;
- `ForegroundExtractorNew`;
- `HandSplitterNew`;
- `MonoProcessorNew`;
- `PoseEstimator`;
- `HandResolver`;
- `Reprojector`;
- `PointerMapper`;
- UDP messages to `win_cursor_plus`.

When pose `point` is active, it calls `HandResolver::compute()`, then `PointerMapper::compute()`, then transmits pointer X/Y, distance-to-plane and DOWN state to the cursor process.

This makes the July lineage the best current candidate for a first historical end-to-end recovery smoke.

## December final upstream lineage

The final Ractiv source adds/refines more advanced components.

### `SurfaceComputer`

Analyzes the scene and estimates a reflection/surface-related Y boundary used by motion processing.

### `SCOPA`

Exposes explicit anatomical points:

- palm;
- thumb;
- index;
- middle;
- ring;
- pinky.

Its implementation estimates palm center/radius using a distance transform, skeletonizes the hand and processes finger branches/tips.

This is materially closer to anatomical hand decomposition than the early Revival geometry heuristics that were later replaced/hardened.

### `HandResolver`

Takes coarse SCOPA index/thumb points and refines them at higher resolution against current image/background information after rectification.

### `PointResolver`

Provides another local high-resolution foreground-based point resolution path.

### Stereo/reprojection

The source contains rectification, several stereo processors and per-device disparity/depth logic.

### Data tooling

The final tree includes `track_plus_data_labeller`, indicating an internal data/labeling workflow for the tracking algorithms.

## Historical interaction-plane logic

`PointerMapper` is especially important because it already implements a version of what Revival Phase 2A/2C was rebuilding conceptually:

- four calibration point collections;
- median calibration points;
- 3D plane construction;
- projection of fingertip 3D point onto the interaction plane;
- quadrilateral/rectangle warping into normalized 0..1000 coordinates;
- low-pass pointer filtering;
- actuation when fingertip-plane distance is below a configured threshold;
- release when distance rises above threshold plus hysteresis;
- thumb/index pinch-to-zoom state.

This is historical evidence that Ractiv already had explicit contact semantics, not merely pointer position.

## Historical Windows output

`win_cursor_plus` receives index/thumb X/Y/Z/DOWN state and contains:

- Windows Touch Injection path;
- mouse-event fallback;
- TUIO output;
- DOWN/UPDATE/UP contact transitions;
- cursor overlays.

Therefore the intended end product path to OS input exists in source.

## Where Ractiv is weaker than Revival today

### Calibration dependency

Ractiv expects serial-specific files:

- `0.jpg`;
- `1.jpg`;
- `stereoCalibData.txt`;
- `rect0.txt`;
- `rect1.txt`.

If absent, it attempts to download from the historical CloudFront endpoint. That service is dead. Revival solved this with a fully local per-device calibration workflow and physically validated calibration for unit `0101007379`.

### Metric validation

Ractiv's `Reprojector` fits a disparity-to-depth curve and estimates plane size using fitted/empirical functions. Revival instead has physically validated stereo calibration, Q-based depth and surface-frame measurements.

### Safety discipline

The Revival project has explicit fail-closed regressions for wrong fingertip identity, stale anatomy, stereo uncertainty and contact semantics. The historical source does not provide comparable modern regression coverage.

### Modern runtime/toolchain

Ractiv's documented build requires Visual Studio Community 2015 and even instructs modifying an SDK header (`winnt.h`) if a build error occurs. Its C++ projects target Win32, while cursor code is C# and the runtime is coupled to old daemon/menu/IPC behavior.

## Where Ractiv may be ahead of the modern Revival line

At the current pause point, Ractiv may have a shorter path to a complete user interaction loop because it already contains:

- anatomical finger naming;
- interaction-plane mapping;
- click actuation + release hysteresis;
- pinch-to-zoom;
- Windows touch/mouse/TUIO output;
- daemon/UI plumbing.

Revival has stronger physical calibration, diagnostics and safety, but is currently still validating the final contact acquisition bridge before enabling OS injection.

## Key source-integration warning

The official December final `main.cpp` includes/instantiates `HandResolver`, `PointResolver` and `PointerMapper`, but its visible active compute path reaches SCOPA and does not call those downstream components.

By contrast, the July historical `main.cpp` in the user fork explicitly calls `HandResolver` and `PointerMapper` and sends their results to `win_cursor_plus`.

Interpretation: the December repository likely captures an unfinished refactor or development checkpoint. Recovery should therefore treat historical versions as components, not assume `latest commit = best runnable product`.

## Recommended recovery architecture

Start with the July integrated lineage as the executable skeleton, then evaluate/port December improvements selectively:

`July integrated app/control flow`

`+ December SCOPA/palm/finger anatomy where it proves better`

`+ Revival local calibration / physical validation where historical infrastructure is dead or weak`

This hybrid is only a hypothesis until hardware smoke data confirms it.
