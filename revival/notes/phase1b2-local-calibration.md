# TouchPlus Revival — Phase 1B.2 local stereo calibration

Factory identity recovery succeeded for the physical test unit:

- serial: `0101007379`
- historical cloud key: `7379`
- legacy factory CDN: unavailable (hostname no longer resolves)

The canonical fallback is therefore a new local stereo calibration generated from synchronized images captured by the actual Touch+.

## Phase 1B.2a — persistent live capture

The first guided PowerShell capture workflow was rejected during physical smoke for two reasons:

1. it incorrectly enforced a minimum of 8 pairs even for a 3-pair smoke test;
2. more importantly, it repeatedly reopened the atomic one-shot capture path and gave the operator no live preview. On the physical Touch+ this produced gray captures even though the proven persistent viewer was healthy.

The canonical capture path is now a single Win32 executable:

- `touchplus_calibration_capture.exe`
- `calibration-target-9x6-25mm-a4.svg`
- `eSPAEAWBCtrl.dll`
- `eSPDI.dll`
- `EtLib.dll`

It performs one Etron unlock, opens one persistent stereo stream, shows LEFT/RIGHT continuously, and saves the next synchronized stereo frame when **SPACE** is pressed. It never reopens the camera between calibration pairs.

### Target

The target is a **10 × 7 square checkerboard**, giving **9 × 6 inner corners**. Each square is **25.0 mm**, so the printed board is **250 × 175 mm**.

Print on **A4 landscape at 100% / Actual size**. Never use “Fit to page”. Physical acceptance on the test print passed: the 100 mm reference line measured exactly **100 mm**.

### Smoke test

Run:

```powershell
.\touchplus_calibration_capture.exe --pairs 3
```

The viewer must remain live. For three initial poses use center/front, moderate left yaw, and moderate right yaw. Keep the complete checkerboard visible in both eyes.

Controls:

- **SPACE** — save the next synchronized full/left/right frame;
- **Q / ESC** — quit;
- `--pairs N` — optional target count; values below 8 are explicitly allowed for smoke testing;
- `--output PATH` — override the raw output directory;
- `--legacy-init` — optional recovered Ractiv sensor init if ever needed.

Each accepted capture writes:

```text
calibration-captures/0101007379/raw/
  pair-001-full.png
  pair-001-left.png
  pair-001-right.png
  pair-001.json
  ...
```

The live path applies the same historical **vertical flip** as the modern Phase 1A runtime before display and saving, so the saved calibration pairs are already in the canonical runtime orientation.

A simple content guard rejects nearly uniform/gray frames instead of silently accepting them.

### Full dataset policy

After the 3-pair smoke is visually accepted:

- fewer than 8 pairs: smoke/diagnostic only;
- 8–11 pairs: usable for diagnosis but capture more;
- 12+ pairs: solver may run;
- 18–25 diverse clean pairs: preferred physical acceptance set.

For the full set, vary center/left/right/top/bottom, distance, yaw, pitch and a little roll. Reject blur, glare, clipped boards and near-duplicate poses.

## Phase 1B.2b — solver (next slice)

The next slice will consume the accepted dataset and produce a versioned calibration bundle containing at minimum:

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
