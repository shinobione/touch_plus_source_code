# TouchPlus Revival — Phase 2A working-surface frame

Physical unit: `0101007379`.

## Goal

Convert calibrated camera-coordinate 3D points into a stable working-surface frame:

- `Xsurface`, `Ysurface`: coordinates in the fitted table/work plane;
- `H`: signed perpendicular height above the plane in millimetres;
- `H = 0`: fitted working surface;
- `H > 0`: point is above the surface, toward the Touch+ cameras.

This deliberately keeps camera calibration (`K/D/R/T/P/Q`) immutable. The mechanical pitch hinge changes the camera-to-table pose, so the surface frame is a separate per-device/per-setup artifact.

## Runtime controls

The existing `touchplus_depth_viewer.exe` retains all Phase 1C controls and adds:

- `C` — capture one robust surface point at the current cursor position. The coordinate is held fixed for 45 frames (~1.5 s on the physical ~30 fps Touch+) and only accepted if enough hardened depth samples are valid.
- `F` — robustly fit a plane from the pending points. Requires at least 6; 8–12 well-spread samples are recommended. A MEDIUM/HIGH fit is saved to `surface/<serial>.json` beside the executable.
- `R` — clear only the pending calibration points. An already saved surface model is not deleted.
- `H` — print the current camera XYZ and transformed `Xsurface / Ysurface / H` for a textured point.
- `P` — unchanged locked point-depth diagnostic from Phase 1C.
- `D` / `S` / `Q` — unchanged depth / rectified stereo / quit controls.

## Recommended physical calibration

1. Do not move the Touch+ or its pitch hinge during the calibration.
2. Use the actual intended working surface.
3. Put the cursor on a textured point on the surface and press `C` once.
4. Wait for `[SURFACE] ADDED point #N` in the console.
5. Repeat over at least 8 well-spread locations: left/right, near/far, upper/lower image regions.
6. Avoid sampling the same tiny patch repeatedly; plane confidence includes spatial coverage.
7. Press `F`.

Expected fit report:

```text
======= SURFACE FRAME FIT =======
samples / inliers : 10 / 9
plane RMS         : ... mm
plane max residual: ... mm
coverage X / Y    : ... / ... mm
normal vs -Z      : ... deg
confidence        : HIGH or MEDIUM
saved             : ...\surface\0101007379.json
SURFACE FRAME RESULT: PASS / SAVED
```

The robust fit does one MAD-based outlier rejection pass. `LOW` confidence is not saved.

## Physical smoke after fit

Without moving the Touch+:

1. point at several textured locations on the bare working surface and press `H`;
2. expect `H` to stay close to 0 mm across the plane;
3. place a rigid textured object of known thickness on the surface and press `H` on its top face;
4. expect positive `H` roughly matching the object thickness;
5. moving the Touch+ pitch hinge invalidates the surface model and requires recalibration.

Raw personal imagery is not committed. The saved surface JSON contains geometry only.

## Acceptance boundary

Phase 2A can merge after:

- Windows/x64 synthetic surface self-test PASS;
- Win32 viewer build + Phase 1C depth self-test PASS;
- physical plane fit MEDIUM/HIGH with broad coverage;
- several bare-surface `H` checks are near zero with no gross outliers;
- one above-surface height check has correct sign and plausible scale.

Only after that should Phase 2B start automatic hand/fingertip extraction in surface coordinates.
