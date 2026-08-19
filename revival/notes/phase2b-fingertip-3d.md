# TouchPlus Revival — Phase 2B.1 geometry-first hand / fingertip 3D

Physical unit: `0101007379`.

## Goal

Build the first automatic hand/fingertip layer on top of the already physically validated stereo calibration, live depth runtime and working-surface frame.

This slice deliberately does **not** implement touch/click yet. It only proves that a hand above the fitted work plane can be isolated and that a plausible fingertip candidate can be emitted as `(Xsurface, Ysurface, H)`.

## Runtime behavior

The existing `touchplus_depth_viewer.exe` stays on the proven persistent capture path. Phase 2B adds a geometry-only tracker on top of the existing half-resolution depth workspace:

- dense points are reprojected through `Q` and transformed into the accepted surface frame;
- only points with `6 mm <= H <= 260 mm` become foreground candidates;
- a one-cell spatial bridge helps sparse valid depth samples form components;
- the largest above-plane component is treated as the current hand candidate;
- an extremity score favors points far from the component centroid and modestly favors lower `H`, which is useful for an extended pointing finger;
- the coarse extremity is refined against full-resolution stereo using the hardened NCC + LEFT/RIGHT consistency matcher around the candidate;
- low-support refinement is reported as `unknown` instead of a finite fingertip;
- accepted fingertip positions are temporally smoothed, while implausible large one-frame jumps are rejected.

The right-hand depth heatmap is overlaid with the selected hand component and a cross at the accepted fingertip candidate.

Console telemetry is printed roughly once per second:

```text
[TRACK] no above-plane hand candidate | foreground=...
[TRACK] hand=... cells | fingertip=unknown | refinement=... | confidence=LOW
[TRACK] hand=... cells | fingertip surface XYZ=(..., ..., H=...) mm | pixel=... | support=... | confidence=MEDIUM/HIGH
```

`T` toggles the Phase 2B tracker without changing the underlying depth/surface stack.

## Important safety boundary

Phase 2B.1 must never turn missing or weak stereo evidence into a fake fingertip. `unknown` is preferred over a wrong finite point.

The following remain immutable in this slice:

- per-serial `K/D/R/T/P/Q` camera calibration;
- the accepted Phase 2A surface transform;
- persistent Touch+ unlock/capture behavior;
- Phase 1C hardened cursor-depth matcher.

## Physical smoke

Use the already accepted `surface/0101007379.json`. If testing from a freshly extracted Actions artifact, copy the existing `surface` folder beside the new EXE, or extract the new kit over the existing Phase 2A folder without deleting `surface/`.

1. Keep the Touch+ base and pitch hinge fixed so the saved Phase 2A surface model remains valid.
2. Clear the work area of books/boxes raised above the plane for this first tracker smoke.
3. Start `touchplus_depth_viewer.exe` in depth mode.
4. With no hand present, expect `no above-plane hand candidate` most of the time; isolated noisy pixels must not become a confident fingertip.
5. Put one hand above the table and extend the index finger clearly away from the palm.
6. Move the index slowly left/right and toward/away from the surface.
7. Expect the selected component overlay to follow the hand and the fingertip cross to stay near the distal index region rather than jump randomly to the palm/background.
8. Console `Xsurface/Ysurface/H` should move continuously with the finger. Lowering the finger toward the table should reduce `H`.
9. Temporary low texture may produce `fingertip=unknown`; that is acceptable and preferable to a false finite point.

A short 30–60 second screen recording with the heatmap and console visible is enough for the first physical review.

## Acceptance boundary for Phase 2B.1

Do not merge until:

- synthetic component/extremity self-test passes in x64 and Win32;
- normal Win32 Revival build and Phase 1C/2A regression checks remain green;
- no-hand smoke does not create persistent confident false fingertips;
- a single extended hand produces a stable component and fingertip candidate;
- `(Xsurface, Ysurface, H)` follows deliberate hand motion with the correct direction, especially decreasing `H` as the fingertip approaches the work plane;
- low-confidence regions degrade to `unknown` rather than catastrophic coordinates.

Touch-down/click thresholds belong to Phase 2C, not this slice.
