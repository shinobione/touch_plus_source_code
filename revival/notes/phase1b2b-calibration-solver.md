# TouchPlus Revival — Phase 1B.2b local calibration solver

Physical unit: `0101007379`.

## Input dataset

The accepted persistent live capture tool produced a 20-pose metric checkerboard dataset:

- 20 synchronized LEFT/RIGHT pairs;
- 640x480 per eye;
- 9x6 inner corners;
- 25.0 mm squares;
- historical Ractiv vertical flip already applied at capture time.

All **20/20 stereo pairs** produced complete checkerboard detection in both eyes (54/54 inner corners per image).

## Solver policy

`revival/tools/touchplus-calibration-solver.py` performs:

1. dataset ZIP/directory discovery and manifest validation;
2. checkerboard detection in both eyes;
3. initial independent mono calibration;
4. per-pair mono reprojection RMSE;
5. robust gross-outlier selection using combined LEFT/RIGHT RMSE with median + 2.5 robust MAD sigma;
6. final mono calibration on accepted pairs;
7. fixed-intrinsic stereo calibration;
8. stereo rectification and `Q` generation;
9. rectified epipolar vertical-error metrics;
10. rectified LEFT/RIGHT preview composites with horizontal guide lines;
11. JSON + OpenCV YAML calibration bundles and a solved ZIP archive.

The solver does not silently promote every captured pair. Gross geometric outliers remain documented in the report and are excluded from the final solve.

## Real 20-pose solve — 2026-08-19

Robust selection on the physical dataset:

- input pairs: **20**;
- complete corner-detected pairs: **20**;
- accepted calibration pairs: **17**;
- excluded gross reprojection outliers: **016, 017, 018**;
- robust combined-RMSE threshold: **1.0305 px**.

Final accepted-set metrics:

- mono RMS LEFT: **0.3472 px**;
- mono RMS RIGHT: **0.3912 px**;
- stereo RMS: **0.3799 px**;
- recovered stereo baseline: **59.953 mm**;
- rectified vertical epipolar error mean: **0.1195 px**;
- rectified vertical epipolar error p95: **0.3096 px**;
- rectified vertical epipolar error max: **1.2763 px**.

Acceptance guard result: **PASS**.

The three rejected pairs remain useful evidence: their checkerboards were detectable, but their planar reprojection residuals were grossly larger than the robust population. They are not deleted from the source dataset.

## Produced bundle

The solver writes, per device:

- `touchplus-calibration-0101007379.json`;
- `touchplus-calibration-0101007379.yml`;
- `calibration-report.json`;
- `rectified/pair-###-rectified.png` for accepted pairs;
- `touchplus-calibration-0101007379-solved.zip`.

The machine-readable bundle includes `K1`, `D1`, `K2`, `D2`, stereo `R`, metric `T_mm`, `E`, `F`, `R1`, `R2`, `P1`, `P2`, `Q`, ROIs, source pair numbers and the final quality metrics.

## Boundary before Phase 1C

Do **not** install this calibration into the live runtime solely from the numeric PASS.

Before Phase 1C is considered accepted:

1. visually review rectified previews and horizontal feature alignment;
2. run a first disparity/depth diagnostic using this bundle;
3. verify near/far direction is coherent;
4. verify at least a few known physical distances with a ruler/tape.

If those checks pass, Phase 1C can load calibration by serial and move to rectified stereo + metric depth.