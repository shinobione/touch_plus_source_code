# TouchPlus Revival — Phase 1C metric depth validation

Physical unit: `0101007379`.

## Purpose

Phase 1B.2b produced a numerically strong stereo calibration from the physical 20-pose checkerboard dataset. Phase 1C proves that the candidate calibration produces coherent **metric depth in millimetres** before any live runtime integration.

Candidate bundle:

`revival/calibration/candidates/0101007379.json`

Current candidate state:

`candidate_physical_depth_validated`

The candidate is physically accepted for the next live Phase 1C integration slice, but it is not yet loaded by the live runtime.

## Calibration evidence carried into Phase 1C

Robust 20-pose solve:

- 20/20 stereo pairs detected the complete 9x6 grid;
- 17/20 retained after robust reprojection filtering;
- excluded gross outliers: 016, 017, 018;
- mono RMS LEFT: 0.3472 px;
- mono RMS RIGHT: 0.3912 px;
- stereo RMS: 0.3799 px;
- solved baseline: **59.953 mm**;
- rectified vertical epipolar mean: 0.1195 px;
- rectified vertical epipolar p95: 0.3096 px.

The physical center-to-center lens spacing was independently measured at approximately **60–61 mm**, strongly confirming that calibration target scale and recovered stereo baseline are correct.

## Physical depth smoke — PASS

Date: 2026-08-19.

A textured Chocapic box was captured in two synchronized stereo pairs, centered approximately on the stereo axis. Raw personal test images remain outside the public repository.

User ruler measurements from the Touch+ front/lens reference plane to the box front face:

- pair 001: **350 mm ±5 mm**;
- pair 002: **600 mm ±5 mm**.

Both manifests identify serial `0101007379`, 640x480 LEFT/RIGHT views and canonical upright orientation.

### Analysis method

The physical pair was:

1. rectified using the accepted candidate matrices;
2. checked with dense StereoSGBM for coherent near/far disparity;
3. independently matched with epipolar-constrained SIFT correspondences on the textured planar box face;
4. fit with a robust disparity plane;
5. evaluated at the rectified principal point so the comparison is made on the stereo axis rather than from an arbitrary image patch.

### Results

Camera-coordinate Z estimates:

- pair 001: **380.63 mm**;
- pair 002: **632.68 mm**.

Near/far ordering: **PASS**.

Dense textured disparity coherence: **PASS**.

Most important metric-scale check:

- physical distance change: `600 - 350 = 250 mm`;
- stereo-estimated distance change: `632.68 - 380.63 = 252.05 mm`;
- delta-scale error: **+0.82%**.

This is a strong metric-scale PASS and rules out gross scale, sign, eye-order or Q failures.

### Reference-origin clarification

`Q` reports Z in the calibrated camera coordinate system. The ruler measurements were taken from the Touch+ front/lens reference plane, not from the mathematical camera optical origin.

The implied additive origin offsets are:

- pair 001: +30.63 mm;
- pair 002: +32.68 mm.

Their mean is approximately **31.66 mm**, with only ~1.03 mm half-spread, well inside the ±5 mm physical measurement uncertainty. The two-distance result is therefore consistent with a fixed front-plane → camera-origin reference offset rather than a calibration scale error.

This inferred offset is documented only. It is **not baked into K/D/R/T/P/Q**. Runtime camera-coordinate Z remains the canonical geometric value; a future UI may optionally expose a device-front reference distance as `Z_front ≈ Z_camera - reference_offset` after that reference is characterized further.

## Acceptance decision

**PHASE 1C PHYSICAL METRIC DEPTH SMOKE: PASS**.

Acceptance gates:

- rectified orientation/alignment: PASS;
- near Z < far Z: PASS;
- useful textured dense disparity: PASS;
- physical stereo baseline vs solved baseline: PASS;
- metric delta scale: PASS (~0.82% error over a 250 mm separation);
- no gross sign / eye swap / scale / Q failure: PASS.

No final end-user accuracy claim is made yet. The next slice must characterize live depth error over the intended Touch+ working volume.

## Next canonical work

Proceed to live Phase 1C integration:

- load candidate calibration by serial `0101007379`;
- keep the accepted Etron unlock + persistent stereo session;
- rectify live LEFT/RIGHT frames;
- add disparity visualization;
- reproject to metric camera-coordinate Z with `Q`;
- add cursor/point depth readout;
- characterize depth stability and error at several known distances;
- only then begin Phase 2 hand/finger/touch-plane tracking.

The deprecated repeated PowerShell one-shot capture workflow remains forbidden.
