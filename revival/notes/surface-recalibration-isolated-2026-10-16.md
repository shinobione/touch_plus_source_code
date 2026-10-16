# 2026-10-16 — isolated Phase 2A surface recalibration (Draft)

## Scope / baseline

- Base: `revival/main` at `bd9f7eb905210595837482dbd0d45410f4d92cb2`.
- Experimental Phase 2C remains in separate PR #17; this slice does **not** touch its algorithms.
- Add Win32 `touchplus_surface_calibrator.exe` which compiles the existing Phase 1C/2A capture, rectification, robust point matcher, and robust plane fit **without including** 2B tracking or 2C semantics. Hotkeys C/F/R/U/H are surface-only.
- No changes to per-device stereo K/D/R/T/P/Q, baseline, exposure/gain, camera IO, stereo matcher thresholds, fingertip authority, contact gate, or OS injection.
- The regular `touchplus_depth_viewer.exe` stays unchanged via `#ifdef TOUCHPLUS_SURFACE_ONLY` around two dedicated entry points.
- Dedicated executable is bundled with Etron DLLs and the accepted camera JSON by the existing Phase 2A GitHub Actions workflow.

## Motivation and physical evidence

Latest private bench (16 Oct 2026): a provisional surface fit reports 21 samples / 12 inliers, RMS 1.239 mm, coverage 103.6 x 203.6 mm and HIGH. Later **bare-mat verification** found 14 height probes, including **five gross mismatches** (H approximately -159, -800.6, -850.2, -876, -1063 mm). There were two more near-boundary probes at -5.1/-5.2 mm. An earlier helper incorrectly kept only three near-zero H values and dropped the bad probes. These numbers are diagnostic summary only: no private images or raw bench data are published.

The standalone Go operator wrapper is intentionally a local tool (not bundled into this public repo while licensing/distribution is reviewed); it must reject promotion if *any* declared bare-mat H check violates the already documented +/-5 mm acceptance band. This does not change the matcher or mask false results: it fails the calibration, preserves the old surface and retains the rejected H telemetry.

## Required automated gates

- Win32 compile for **both** `touchplus_depth_viewer.exe` and `touchplus_surface_calibrator.exe`.
- Phase 1C/Q `--self-test` for both binaries, plus existing Phase 2A x64 + Win32 self-tests.
- Other Revival build and fingertip regressions on the final SHA.
- Verify package includes isolated EXE, 3 Etron DLLs, reference camera JSON.
- Operator-level negative test: a record of 3 near-zero H and one H=-1063 mm **must fail**. Never count a failed probe as H=0.

## Required physical smoke (not yet executed)

1. Use only the isolated Win32 calibrator. Confirm no `[TRACK]`, `[CONTACT]`, `[HYBRID]`, `[2C]` telemetry, Etron unlock, persistent ~30 fps.
2. With camera and lamp fixed, take textured surface points with C spread across desired full mat, F fit. Never replace the accepted surface yet.
3. On the **same bare surface**, probe distinct near/far/left/right places with H, including areas where the previous matching failed. Record each H whether good, bad, or INVALID.
4. Block model promotion if any probe is bad or if expected disparities lie outside the matcher's implemented range. Do not solve this by blindly widening matcher range.
5. Relearn the background with B only in a later tracker session if and only if the surface gate is truly passed.

**Verdict now: BUILD/CI pending, PHYSICAL FAIL of historical new setup, Draft/DO NOT MERGE.** Never claim a user product PASS from the CI.
