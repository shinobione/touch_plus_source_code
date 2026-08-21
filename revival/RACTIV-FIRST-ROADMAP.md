# TouchPlus Revival — Ractiv-first investigation roadmap

Date: **2026-08-22**

Status: **ACTIVE STRATEGIC EXPERIMENT**

The modern Revival line is intentionally paused, not abandoned. The goal of this track is to determine whether the original Ractiv software can be brought back to a usable state faster than finishing the current modern touch/contact stack from scratch.

The frozen Revival checkpoint is documented in:

`revival/notes/revival-pause-checkpoint-2026-08-22.md`

## 1. Why this experiment exists

The Revival work has already proved the hardware is healthy and has rebuilt the difficult physical foundations:

- Etron vendor communication and `SWUnlock(0x0107)`;
- persistent real stereo capture;
- per-device serial recovery;
- local calibration after the historical Ractiv calibration CDN disappeared;
- metric stereo depth;
- working-surface coordinates;
- physically validated 3D fingertip tracking.

The remaining Revival blocker is semantic contact acquisition in the last centimetre above the surface. It is safe but still intermittent because the fingertip can lose metric stereo evidence exactly at contact.

Meanwhile, inspection of the historical source shows that Ractiv had already implemented substantially more of the intended product than a simple camera demo.

## 2. Historical source baselines

### Baseline A — integrated July 2015 lineage

The original fork snapshot currently available as this repository's historical `master` stops at:

`902cb8cc3e660b0ca8d9049fabd383c34da69607`

That lineage visibly wires an end-to-end surface-mode pipeline including:

- camera + IMU;
- exposure adjustment;
- motion/background extraction;
- hand splitting;
- mono hand processing;
- pose detection;
- `HandResolver` high-resolution index/thumb refinement;
- `PointerMapper` 3D projection and interaction-plane mapping;
- click actuation with hysteresis;
- UDP output to `win_cursor_plus`;
- cursor/touch output and pinch-to-zoom plumbing.

This is currently the strongest candidate for a **last-known integrated pipeline**.

### Baseline B — official Ractiv final source

Official upstream:

`Ractiv/touch_plus_source_code`

Final observed commit:

`58f29eb69e7a3f6095de83bcb43cf489d7ea2cef` — 2015-12-01

This later lineage adds/refines major components including:

- `SurfaceComputer`;
- `SCOPA` anatomical hand processing;
- palm center/radius estimation using distance transform;
- skeleton/finger branch processing;
- named anatomical points (`thumb`, `index`, `middle`, `ring`, `pinky`, `palm`);
- `HandResolver` high-resolution fingertip refinement;
- `PointResolver`;
- several stereo processor variants;
- `track_plus_data_labeller`;
- Xcode/macOS project content.

However, the final `main.cpp` appears to be caught in an in-progress refactor: its active core path reaches SCOPA but does not visibly call the still-present `HandResolver` / `PointerMapper` output path. Therefore **newer does not automatically mean more runnable end-to-end**.

## 3. Ractiv capabilities already present in source

The historical code contains concrete implementations for capabilities we were independently rebuilding:

### Hardware/runtime

- Touch+ camera selection and device lifecycle;
- serial validation;
- IMU/accelerometer access;
- camera exposure/gain initialization;
- 1280x480 stereo split;
- historical vertical flip.

### Calibration and depth

- per-device calibration directory keyed by serial;
- loading `0.jpg`, `1.jpg`, `stereoCalibData.txt`, `rect0.txt`, `rect1.txt`;
- rectification;
- disparity-to-depth curve fitting;
- stereo 3D reprojection.

The original implementation assumes a now-dead Ractiv CloudFront calibration service. The Revival local calibration is therefore an important asset even on a Ractiv-first path.

### Surface interaction

`PointerMapper` already contains:

- four-point interaction-plane calibration;
- plane projection;
- mapping to normalized 0..1000 surface coordinates;
- filtered pointer coordinates;
- click actuation based on distance to the plane;
- click-release hysteresis;
- thumb/index pinch-to-zoom logic.

### Windows output

`win_cursor_plus` already contains:

- Windows touch injection;
- mouse fallback;
- TUIO output;
- index/thumb contact state handling;
- cursor overlay plumbing.

Therefore the historical project did contain an intended path from stereo hand tracking to real OS interaction.

## 4. What must NOT be assumed

The existence of these classes does **not** prove the final 2015 product was reliable or shippable.

Known/likely blockers to test rather than guess:

- old Visual Studio 2015 / Win32 toolchain assumptions;
- old OpenCV and binary dependencies;
- Etron SDK architecture requirements;
- dead per-device calibration CDN;
- UI/daemon/menu process coupling;
- December 2015 refactor appears incompletely wired in `main.cpp`;
- historical contact logic may be too noisy on real hardware;
- historical calibration/depth model is less rigorous than the physically validated Revival model;
- robustness across modern Windows and current drivers is unknown.

The Ractiv-first line must therefore be treated as a controlled recovery experiment, not as trusted production code.

## 5. Experimental strategy

### R0 — preserve modern Revival

DONE.

- Keep `revival/main` as accepted modern reference.
- Keep PR #10 Draft and unmerged.
- Preserve all calibration/surface artifacts and physical findings.
- No destructive rewrite of accepted Revival code.

### R1 — create isolated historical recovery line

Create a dedicated branch for historical work, separate from `revival/main` and PR #10.

Recommended branch:

`ractiv-recovery/main`

First objective: reproduce historical code with **minimal compatibility changes only**.

Do not mix algorithm changes into the initial build bring-up.

### R2 — test the two historical baselines separately

#### R2A — July integrated baseline

Start from the historical integrated lineage represented by this fork's `master`.

Goal:

`camera -> background -> hand -> index/thumb -> PointerMapper -> LOG-ONLY output`

Initially disable actual mouse/touch injection while preserving the data path.

Why first: the integration from tracking to `PointerMapper` and `win_cursor_plus` is visibly wired in this lineage.

#### R2B — December final upstream baseline

Import/compare official upstream commit:

`58f29eb69e7a3f6095de83bcb43cf489d7ea2cef`

Goal:

- compile the later SCOPA/SurfaceComputer generation;
- identify the exact point where the refactored pipeline stops;
- determine which late components are improvements worth porting onto the integrated baseline.

Do not automatically replace R2A with R2B merely because R2B is newer.

### R3 — remove dead external dependencies without changing algorithms

Minimum compatibility adapters:

1. replace dead factory calibration download with local files;
2. provide the historical runtime the serial-specific calibration it expects, or write a small compatibility loader/exporter from Revival calibration;
3. make menu/daemon dependencies optional for a headless diagnostic run where practical;
4. keep Etron DLL loading explicit and Win32-compatible;
5. package all required runtime assets reproducibly.

### R4 — first physical Ractiv smoke

Acceptance sequence:

1. Touch+ detected;
2. vendor initialization/unlock succeeds;
3. persistent stereo image is real, not gray;
4. background learning stabilizes;
5. hand segmentation reacts to the real hand;
6. historical index candidate follows the actual index;
7. stereo/index 3D output is finite and monotonic with depth;
8. interaction-plane distance reacts correctly when the finger approaches the table;
9. historical DOWN state can be observed in telemetry;
10. no OS injection until semantic behavior is visually validated.

Any catastrophic false click/contact is a blocker for enabling historical injection.

### R5 — compare historical measurements against Revival oracle

Use the accepted Revival stack as a diagnostic reference, not necessarily as the runtime.

For the same physical poses compare:

- Ractiv index 2D vs Revival accepted distal tip;
- Ractiv 3D depth vs Revival metric depth;
- Ractiv distance-to-plane vs Revival `H`;
- Ractiv DOWN/UP vs physical contact;
- latency and valid-frame rate.

This establishes whether Ractiv is genuinely closer to a usable product or merely more feature-complete on paper.

### R6 — choose architecture based on evidence

Possible outcomes:

#### Outcome A — historical stack works surprisingly well

Use Ractiv as the application/runtime base and modernize it incrementally.

Likely keep from Revival:

- atomic unlock/capture knowledge;
- local calibration workflow;
- physical validation tools;
- robust safety tests.

#### Outcome B — July integration works but tracking is weak

Keep the integrated July pipeline and selectively port late Ractiv components such as SCOPA/palm/finger anatomy.

#### Outcome C — Ractiv tracking works but depth/calibration is weak

Hybridize:

`Ractiv anatomy / interaction UX + Revival metric stereo / surface geometry`.

#### Outcome D — historical stack is fundamentally unstable

Resume modern Revival at the frozen 2C checkpoint. No work is lost.

## 6. Decision gate before resuming modern Phase 2C

Do not resume tuning the modern 2C pre-contact bridge until the historical experiment answers these questions:

1. Can the July integrated Ractiv lineage build/run on the real Touch+ with modest compatibility work?
2. Does its index tracker locate the correct fingertip reliably enough for interaction?
3. Does its existing plane-distance actuation generate plausible DOWN/UP semantics on the real table?
4. Are the December SCOPA improvements meaningfully better on this physical device?
5. Is a hybrid Ractiv-anatomy + Revival-metric stack simpler than finishing modern 2C alone?

## 7. Safety / preservation rules

- Never rewrite or force-update `revival/main` for historical experiments.
- Keep PR #10 unmerged while paused.
- Never modify accepted `K/D/R/T/P/Q` merely to make Ractiv outputs look correct.
- Preserve serial `0101007379` calibration and surface artifacts.
- Keep OS injection disabled during initial historical smokes.
- Do not commit personal smoke videos/images.
- Every compatibility patch should be distinguishable from historical algorithm changes.

## 8. Immediate next action

Create the isolated historical recovery branch and perform **R2A — July integrated baseline build audit** first.

The first milestone is deliberately small:

> Build or minimally modernize the integrated historical `track_plus` enough to launch in **log-only mode**, open the real Touch+, load local calibration without the dead CDN, and report the historical fingertip/plane/contact values without injecting input into Windows.

Only after that smoke should we decide whether to port December SCOPA immediately or first test the July tracker as-is.
