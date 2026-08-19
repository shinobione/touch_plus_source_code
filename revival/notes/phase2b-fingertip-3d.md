# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.4 — background-gated distal fingertip

Physical unit: `0101007379`.

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm book measured about 54–55 mm. Phase 2B is only allowed to sit on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

## Physical progression

1. **2B.1 wiring PASS** — tracker runtime, `T`, surface-model load and heartbeat work.
2. **2B.1 foreground FAIL** — real scene produced ~19k–26k-cell pseudo-hands.
3. **2B.2 hardened segmentation PARTIAL PASS** — finite ROI, `H>=18 mm`, stronger dense gates and giant-component rejection removed the 20k-cell continent; real components dropped to hundreds/few-thousands and the real XYZ refinement path could succeed.
4. **2B.2 fingertip identity FAIL** — finite candidates landed on wrist/back-of-hand rather than distal index.
5. **2B.3 top-entry geodesic FAIL** — physical video still showed false `hand` components with no hand present (e.g. startup `hand=923 cells | coarse_pixel=211,475`) and coarse candidates jumping across the image / near the lower frame edge. Several finite XYZ outputs were still attached too high on the hand. A top-entry geodesic on an already-wrong foreground mask is not enough.

## Phase 2B.4 correction

2B.4 changes the boundary rather than tuning the same scorer again.

### Learned clean background

Tracking is logically gated until the user clears the work area and presses **`B`**. The runtime then learns 30 clean dense-depth frames plus a grayscale reference.

A current dense cell may become foreground only when:

- it is measurably closer than a reliable learned background disparity at that same grid cell; or
- background disparity was unavailable there, in which case both a stronger `H` and a visible appearance change are required.

This is intended to remove static table/cables/raised clutter and stable dense-depth tails before connected-component analysis.

### Explicit desk gesture

This remains deliberately controlled:

- one hand;
- forearm/wrist enters from the top of the image;
- index extends downward into the work area.

The selected component must contain enough cells in the top-entry band. Inside that cleaned component, the fingertip score combines downward image position with geodesic distance; low `H` and local boundary thinness are tie-breakers. Robust full-resolution NCC + LEFT↔RIGHT consistency still gates finite XYZ, and weak evidence must still return `unknown`.

## Runtime controls

- `B` — learn/relearn 30 clean background depth frames;
- `T` — enable/disable Phase 2B tracker;
- existing Phase 1C/2A controls remain unchanged.

Expected console sequence:

```text
[TRACK] heartbeat | background=NOT_READY | clear work area and press B
[TRACK] BACKGROUND LEARN STARTED ...
[TRACK] heartbeat | background=LEARNING n/30 ...
[TRACK] heartbeat | background=READY | no changed top-entry hand candidate ...
```

Only after `background=READY` should the hand be inserted.

## Acceptance boundary

Do not merge PR #9 until real hardware shows all of the following:

1. clear scene after background learning produces no persistent hand candidate;
2. one top-entry hand produces a compact changed component rather than static-scene blobs;
3. diagnostic coarse candidate stays near the distal index;
4. finite `(Xsurface,Ysurface,H)` remains attached to that same fingertip and moves continuously;
5. lowering the index toward the work plane lowers `H`;
6. low texture degrades to `unknown`, not an anatomically wrong finite point.

Touch/click thresholds remain Phase 2C.
