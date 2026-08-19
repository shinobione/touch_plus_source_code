# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.3 — geodesic fingertip identity

Physical unit: `0101007379`.

Phase 2A proved that the accepted surface frame is physically useful on the real Touch+: the bare work surface measures near `H=0`, while a 53 mm book measured about 54–55 mm. Phase 2B adds the first automatic single-hand / fingertip layer on top of that accepted metric stack.

### What the physical smokes established

1. **2B.1 activation / wiring:** tracker runtime, `T` toggle, saved `surface/0101007379.json`, and heartbeat reporting all work on the real device.
2. **2B.1 foreground blocker:** the first hand smoke produced giant ~19k–26k-cell pseudo-hands. This came from trusting the half-resolution dense depth too aggressively and selecting the largest grown component.
3. **2B.2 hardened segmentation:** finite surface ROI, `H>=18 mm`, tighter dense cost/uniqueness, local H/disparity consistency, no unconditional 3x3 growth, and implausibly giant/wide component rejection removed the 20k-cell failure. Real selected components are now generally hundreds to a few thousand cells.
4. **2B.2 real XYZ path:** the hardware run intermittently reached full-resolution robust stereo refinement and emitted finite surface-space fingertip XYZ with MEDIUM/HIGH confidence.
5. **2B.2 fingertip identity blocker:** video review showed those finite candidates repeatedly landing around the wrist / back-of-hand side. Example real telemetry around the extended-index sequence reported `pixel≈(441–447,83–89)` while the visible distal index was substantially lower in the rectified-left image. The radial-from-centroid extremity scorer was choosing the wrong anatomical endpoint.

### Phase 2B.3 correction

For the canonical Touch+ desk setup, the forearm enters from the **top of the camera image** and the extended index points into the work area. 2B.3 makes that physical setup explicit instead of pretending orientation independence:

- keep the accepted 2B.2 hardened foreground segmentation unchanged;
- treat the selected component's top entry band as a wrist/forearm anchor;
- walk through only that already-selected component;
- choose the distal **geodesic** endpoint opposite the wrist, with low-H and boundary thinness only as tie-breakers;
- robust full-resolution NCC + LEFT↔RIGHT consistency still gates the final finite XYZ;
- failed refinement still degrades to `unknown`, never a fabricated point;
- when refinement is unknown, the coarse candidate pixel is still reported and a white diagnostic cross is drawn on the depth heatmap so fingertip identity can be checked visually.

This is deliberately a controlled **single hand + extended index + top-entry forearm** slice. Orientation-independent pose understanding, multiple fingertips and touch/click semantics are later boundaries.

## CI gate

The synthetic regression now includes:

- a top-entry wrist/forearm;
- a broad palm;
- one long index extending downward toward the plane;
- two shorter folded-finger branches;
- the prior giant false component that is larger than the real hand.

Phase 2B.3 must first reject the giant distractor, then select the long distal index endpoint geodesically rather than the wrist side. Both x64 and Win32 must pass, followed by the normal Revival Win32 build plus Phase 2A and Phase 1C regressions before packaging.

## Physical smoke required before merge

1. Preserve the accepted `surface/0101007379.json` and do not move the Touch+ base/hinge.
2. Start with several seconds of no hand; no persistent finite fingertip is allowed.
3. Insert one hand from the top of the image with the index clearly extended into the work area.
4. Check the diagnostic coarse cross even on `fingertip=unknown`: it should sit near the distal index, not the wrist/knuckles.
5. Move the index slowly left/right, then down/up relative to the work surface.
6. Finite `fingertip surface XYZ=(X,Y,H)` outputs should stay anatomically attached to the distal index and move continuously; `H` should fall as the fingertip approaches the plane.
7. Low texture may produce `unknown`; catastrophic or anatomically wrong finite points are blockers.

**Do not merge PR #9 until fingertip identity passes on the real Touch+.**
