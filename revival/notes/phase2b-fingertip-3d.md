# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.5 — learned 2D silhouette + robust stereo fingertip

Physical unit: `0101007379`.

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm book measured about 54–55 mm. Phase 2B sits on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

## Physical progression

1. **2B.1 wiring PASS** — tracker runtime, `T`, surface-model load and heartbeat work.
2. **2B.1 foreground FAIL** — real scene produced ~19k–26k-cell pseudo-hands.
3. **2B.2 hardened segmentation PARTIAL PASS** — stronger dense-depth gates removed the giant 20k-cell continent; real components dropped to hundreds/few-thousands and full-res XYZ could succeed.
4. **2B.2 fingertip identity FAIL** — finite candidates landed on wrist/back-of-hand.
5. **2B.3 top-entry geodesic FAIL** — no-hand pseudo-hands and jumping coarse endpoints remained.
6. **2B.4 learned background PARTIAL PASS** — physical video proved `NOT_READY -> LEARNING -> READY` works and reduced no-hand components to roughly 40–110 cells instead of hundreds/thousands. However the visible distal index was still not the tracked pixel: while the finger extended substantially lower in RECTIFIED LEFT, finite telemetry repeatedly stayed around `pixel≈(503–521,163–175)` and later around central/palm locations. Root cause: 2B.4 still built the hand shape only from cells with reliable half-resolution dense disparity, so low-texture distal skin could disappear before endpoint scoring.

## Phase 2B.5 correction

2B.5 deliberately separates **fingertip identity** from **metric depth measurement**.

### 1. Learn a clean background

The existing `B` workflow is preserved. The runtime learns 30 clean frames of grayscale appearance plus dense disparity statistics.

### 2. Build a 2D changed silhouette

At half-resolution, current grayscale is compared with the learned background. A changed component is only accepted as the controlled hand when it:

- is large enough to reject the ~40–110-cell no-hand residuals seen in 2B.4;
- enters through the configured top image band;
- contains enough physically supported dense-depth core cells;
- has a plausible vertical extent.

Appearance therefore fills low-texture fingertip pixels that dense disparity may miss, while depth core support prevents a pure lighting/shadow blob from becoming the hand.

### 3. Identify the distal index in that silhouette

The wrist/forearm entry band remains the top anchor. Geodesic distance is computed through the full changed silhouette, and the controlled desk gesture favors the endpoint that is both far from the top anchor and lower in the image.

The synthetic regression explicitly removes dense-depth support from the distal part of the index. 2B.5 must still recover that visual endpoint. Current x64 CI result:

```text
silhouette cells       : 4749
depth core cells       : 3270
silhouette tip grid    : 157,194
geodesic steps         : 153
small noise rejected   : 1
PHASE 2B.5 ... PASS
```

### 4. Measure only near the identified fingertip

The 2D fingertip pixel seeds the already-proven full-resolution NCC + LEFT↔RIGHT consistency matcher. Nearby stereo matches are constrained to the selected silhouette and a local H-consensus rejects outliers. Missing texture is still allowed to return `unknown`; it must never invent a finite anatomical point elsewhere.

## Runtime controls

- `B` — learn/relearn 30 clean background frames;
- `T` — enable/disable Phase 2B tracker;
- existing Phase 1C/2A controls remain unchanged.

Expected banner:

```text
[TRACK] PHASE 2B.5 RUNTIME ACTIVE | tracker=APPEARANCE-SILHOUETTE
```

After `background=READY`, telemetry uses `silhouette=...` and `tip_pixel=x,y` so the visual identity can be checked directly.

## Acceptance boundary

Do not merge PR #9 until real hardware shows all of the following:

1. clear scene after background learning produces no persistent accepted hand silhouette;
2. one top-entry hand produces a compact supported silhouette;
3. `tip_pixel` / white diagnostic cross stays on the visible distal index, including when dense depth is missing there;
4. finite `(Xsurface,Ysurface,H)` remains attached to that same fingertip and moves continuously;
5. lowering the index toward the work plane lowers `H`;
6. low texture degrades to `unknown`, not an anatomically wrong finite point.

Touch/click thresholds remain Phase 2C.
