# TouchPlus Revival — Phase 1C.2 live depth viewer

Physical unit: `0101007379`.

## State

PR #6 is merged. The per-serial calibration is physically depth-validated and remains canonical in camera coordinates.

Active branch: `revival/phase1c-live-depth-viewer`
Active PR: #7

## Runtime candidate

`touchplus_depth_viewer.exe`

Pipeline:

`Etron SWUnlock → serial flash read → persistent 1280x480 stereo → historical vertical flip → calibrated rectification → diagnostic disparity → Q → camera-coordinate Z mm`

The executable:

- reads the hardware serial from flash;
- loads `calibration/<serial>.json` beside the EXE;
- refuses a serial mismatch or a calibration not in `candidate_physical_depth_validated` state;
- displays rectified LEFT + live depth heatmap by default;
- exposes rectified LEFT/RIGHT with `S`, depth view with `D`;
- reports a full-resolution subpixel cursor disparity and camera-coordinate Z in millimetres;
- keeps the accepted one-unlock / one-persistent-stream behavior.

The live dense heatmap is a dependency-free CPU diagnostic matcher. It is not a replacement for the offline StereoSGBM reference diagnostic.

## CI candidate

GitHub Actions `Revival Windows Build` run `32275561918` is green on the PR candidate.

Win32 self-test PASS evidence:

- serial: `0101007379`;
- state: `candidate_physical_depth_validated`;
- inferred Q baseline: `59.953 mm`;
- rectification-map source coverage: `100%` LEFT and RIGHT;
- synthetic Q near test (`d=82`): `380.455 mm`;
- synthetic Q far test (`d=49.5`): `630.249 mm`.

Artifact:

- `touchplus-phase1c-live-depth-viewer-windows-x86`
- artifact id: `9373892168`
- SHA-256: `44e39c255aa88ecc26c59569ac4888ea81275885962b49e6a8e87d9f89550981`

## Physical smoke boundary — required before merge

1. Close Windows Camera and every other Touch+ process.
2. Keep the packaged `calibration/0101007379.json` directory beside the EXE.
3. Run `touchplus_depth_viewer.exe` with no `--legacy-init` first.
4. Confirm stable `RECTIFIED LEFT | DEPTH HEATMAP` live output with no gray reopen/flicker/freeze.
5. Move the mouse over textured near/far surfaces and confirm Z ordering and plausible camera-coordinate values.
6. Press `S` and confirm corresponding features in rectified LEFT/RIGHT are horizontally aligned; press `D` to return to depth.
7. Run for at least ~30 seconds and record capture/source/depth rates plus cursor-Z stability.

For the previously validated centered cereal-box geometry, front-plane ruler readings of ~350 mm and ~600 mm corresponded to camera-coordinate Z of ~381 mm and ~633 mm because the front-plane reference has an approximately fixed ~31.7 mm offset from the calibrated camera-coordinate origin. Do not bake this offset into K/D/R/T/P/Q.

Do not merge PR #7 until this physical live smoke passes. Do not begin hand/finger tracking before live metric geometry is stable.
