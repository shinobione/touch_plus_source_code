# TouchPlus Revival — Phase 1B.2 local stereo calibration

Factory identity recovery succeeded for the physical test unit:

- serial: `0101007379`
- historical cloud key: `7379`
- legacy factory CDN: unavailable (hostname no longer resolves)

The canonical fallback is therefore a new local stereo calibration generated from synchronized images captured by the actual Touch+.

## Phase 1B.2a — capture dataset

The packaged kit contains:

- `touchplus_atomic_probe.exe`
- `touchplus-calibration-capture.ps1`
- `calibration-target-9x6-25mm-a4.svg`
- `eSPAEAWBCtrl.dll`
- `eSPDI.dll`
- `EtLib.dll`

The target is a **10 × 7 square checkerboard**, giving **9 × 6 inner corners**. Each square is **25.0 mm**, so the printed board is **250 × 175 mm**.

Print the SVG on **A4 landscape at 100% / Actual size**. Never use “Fit to page”. Verify the printed 100 mm reference line with a ruler before collecting data; if it is not 100 mm, do not use that print for metric calibration.

Run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\touchplus-calibration-capture.ps1
```

Default target: 20 accepted pairs for device `0101007379`.

For every accepted pair:

- the complete checkerboard must be visible in **both** 640×480 eyes;
- hold the board still while the atomic capture opens and takes the frame;
- use clearly different poses;
- cover center, left, right, top, bottom;
- vary distance;
- include moderate yaw, pitch, and a little roll;
- reject blur, glare, clipped boards, and near-duplicate poses.

The capture helper intentionally reuses the already physically validated Phase 0C atomic capture path. Those PNGs are in the pre-Phase-1A raw orientation. The solver must apply the same historical **vertical flip** used by the modern runtime before calibration/rectification results are compared or installed.

At completion the script creates:

```text
calibration-captures/<serial>/
  session.json
  capture-summary.json
  raw/
    pair-001-full.png
    pair-001-left.png
    pair-001-right.png
    pair-001.json
    ...

touchplus-calibration-<serial>.zip
```

Dataset quality policy:

- fewer than 8 pairs: reject;
- 8–11 pairs: usable for diagnosis but capture more;
- 12+ pairs: solver may run;
- 18–25 diverse clean pairs: preferred acceptance set.

## Phase 1B.2b — solver (next slice)

The next slice will consume this dataset and produce a versioned calibration bundle containing at minimum:

- left/right camera matrices;
- left/right distortion coefficients;
- stereo rotation `R` and translation `T` in millimetres;
- rectification matrices `R1`, `R2`;
- projection matrices `P1`, `P2`;
- reprojection matrix `Q`;
- per-pair corner-detection result;
- mono RMS and stereo RMS reprojection error;
- epipolar vertical-error statistics after rectification;
- rectified preview pairs for visual acceptance.

No calibration should be promoted into the runtime until the error metrics and rectified previews pass physical review.
