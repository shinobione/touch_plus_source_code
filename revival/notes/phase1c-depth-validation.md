# TouchPlus Revival — Phase 1C metric depth validation

Physical unit: `0101007379`.

## Purpose

Phase 1B.2b produced a numerically strong stereo calibration from the physical 20-pose checkerboard dataset. Phase 1C must now prove that the candidate calibration produces coherent **metric depth in millimetres** before it is promoted into the live runtime.

Candidate bundle:

`revival/calibration/candidates/0101007379.json`

Its promotion state is intentionally:

`candidate_pending_physical_depth_validation`

Do not change that state solely because matrix/RMS metrics look good.

## Accepted calibration evidence

Robust 20-pose solve:

- 20/20 stereo pairs detected the complete 9x6 inner-corner grid;
- 17/20 pairs retained after robust reprojection filtering;
- excluded gross outliers: 016, 017, 018;
- mono RMS LEFT: 0.3472 px;
- mono RMS RIGHT: 0.3912 px;
- stereo RMS: 0.3799 px;
- baseline: 59.953 mm;
- rectified vertical epipolar mean: 0.1195 px;
- rectified vertical epipolar p95: 0.3096 px.

Rectified checkerboard previews were visually reviewed and matching rows/features align horizontally without eye swap or orientation regression.

## Why the checkerboard is not the final dense-depth target

A checkerboard is excellent for geometric calibration because its corners are known precisely, but its repetitive black/white texture can be ambiguous for dense block matching. At close range, StereoSGBM may lock onto a repeated square offset instead of the true correspondence.

Therefore the physical metric-depth smoke should use a **textured, non-repeating real scene** such as book covers, product packaging, printed graphics or other surfaces with unique local detail.

## Offline validation tool

`revival/tools/touchplus-depth-sanity.py`

It:

1. loads the candidate calibration by serial;
2. rectifies one synchronized LEFT/RIGHT pair;
3. computes StereoSGBM disparity;
4. reprojects disparity through `Q` into millimetres;
5. writes rectified stereo, disparity and depth visualizations;
6. optionally compares selected image points with user-measured physical distances.

This tool is deliberately offline first. The live runtime is not modified until physical depth sanity passes.

## Physical smoke setup

Use **two textured objects** whose front faces are visible in both cameras and are at clearly different distances from the Touch+.

Suggested starting geometry:

- near object front face: approximately 300–400 mm from the camera/lens front plane;
- far object front face: approximately 600–800 mm;
- keep both objects inside both LEFT and RIGHT views;
- avoid shiny/specular surfaces and large blank areas;
- do not use the checkerboard as the main dense-depth subject.

Measure the distance from approximately the front plane of the two Touch+ lenses to each object's front face. A tape/ruler measurement within roughly 5–10 mm is sufficient for the first smoke.

Capture with the **persistent** accepted capture executable, never the deprecated one-shot PowerShell loop. To keep depth validation separate from calibration data, use a dedicated raw output directory, for example:

```powershell
.\touchplus_calibration_capture.exe --pairs 1 --output .\depth-validation\raw
```

Despite its historical name, this executable saves synchronized raw LEFT/RIGHT/full frames and does not require a checkerboard to save a pair.

Upload the resulting LEFT/RIGHT pair (or ZIP the `depth-validation` directory) and report the two measured object distances.

## Acceptance boundary

Phase 1C candidate calibration may advance toward live runtime integration only if:

- rectified views remain correctly oriented and horizontally aligned;
- near/far ordering is correct;
- dense disparity is coherent over useful textured regions;
- measured depth is in the right metric scale;
- known-distance errors are reasonable for the first diagnostic and can be explained/tuned rather than showing a gross scale/sign/eye-swap failure.

No fixed final accuracy promise is declared yet. First establish physically coherent metric Z, then characterize error over the intended Touch+ working volume.
