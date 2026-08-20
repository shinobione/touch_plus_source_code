# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.6 — support-bounded skeleton fingertip

Physical unit: `0101007379`.

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm book measured about 54–55 mm. Phase 2B sits on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

## Physical progression

1. **2B.1 wiring PASS** — tracker runtime, `T`, surface-model load and heartbeat work.
2. **2B.1 foreground FAIL** — real scene produced ~19k–26k-cell pseudo-hands.
3. **2B.2 hardened segmentation PARTIAL PASS** — stronger dense-depth gates removed the giant 20k-cell continent; real components dropped to hundreds/few-thousands and full-res XYZ could succeed.
4. **2B.2 fingertip identity FAIL** — finite candidates landed on wrist/back-of-hand.
5. **2B.3 top-entry geodesic FAIL** — no-hand pseudo-hands and jumping coarse endpoints remained.
6. **2B.4 learned background PARTIAL PASS** — physical video proved `NOT_READY -> LEARNING -> READY` works and reduced no-hand components to roughly 40–110 cells instead of hundreds/thousands. However the visible distal index was still not the tracked pixel because the hand mask required reliable half-resolution dense disparity all the way to the distal skin.
7. **2B.5 appearance silhouette PARTIAL PASS / fingertip FAIL** — the yellow silhouette can cover the full hand and low-texture distal finger, but `tip_pixel` still jumped inside the palm/proximal finger and toward connected photometric tails.
8. **2B.6 support-bounded skeleton PARTIAL PASS / fingertip FAIL** — the newest physical benchmark shows the ambiguity guard is useful, but skeleton endpoint identity is still not reliable enough. In the two-finger segment the runtime correctly emits `ambiguous/no-dominant-skeleton-endpoint` on some frames. However, with one clearly extended index it still produces anatomically wrong candidates and can even attach HIGH confidence to them. A representative vertical-index frame reports `tip_pixel=389,201 | support=8 | confidence=HIGH` while the visible distal index is substantially lower/left in RECTIFIED LEFT. Horizontal/diagonal single-index poses likewise show candidates such as `415,283`, `441,244`, etc. far from the visible fingertip. This is a hard blocker: wrong finite/HIGH identity is worse than `unknown`.

## Phase 2B.6 architecture

2B.6 keeps learned-background segmentation, then:

- rebuilds a physically supported above-plane dense-depth core;
- retains nearby appearance-only cells so low-texture distal skin can remain visible;
- trims long appearance-only tails by bounded support distance;
- skeletonizes the bounded hand mask;
- restricts fingertip candidates to distal skeleton endpoints;
- rejects near-equal branches as ambiguous;
- only then performs robust full-resolution stereo refinement.

This improved failure safety and reduced interior-pixel eligibility, but the real benchmark proves **global skeleton branch length from the top-entry wrist is not a sufficient anatomical discriminator**. Palm topology, curled fingers and mask irregularity can still create a longer/stronger branch than the actual extended index.

## Current conclusion / next design requirement

Do **not** tune only the existing V6 geodesic weights. The next identity stage must add a palm/finger anatomical decomposition or deliberately pivot to a lightweight 2D hand-landmark detector. Any geometry-only successor should at minimum:

- estimate a palm core independently of the distal branches;
- measure finger-like branch length from the palm boundary/core, not from the top-entry wrist;
- reject candidates that do not form a thin distal branch leaving the palm;
- keep multi-branch ambiguity as `unknown`;
- use temporal persistence only after a correct anatomical candidate exists;
- continue to use the accepted stereo/Q/surface stack only for metric XYZ after 2D identity is fixed.

## Runtime controls

- `B` — learn/relearn clean background;
- `T` — enable/disable Phase 2B tracker;
- existing Phase 1C/2A controls remain unchanged.

## Acceptance boundary

PR #9 remains **DO NOT MERGE** until real hardware shows all of the following:

1. clear scene after background learning produces no persistent accepted hand;
2. one top-entry hand produces a compact supported silhouette;
3. `tip_pixel` / diagnostic cross stays on the visible distal index for vertical, horizontal and diagonal poses;
4. two comparable distal fingers may return `ambiguous/unknown` rather than arbitrary selection;
5. finite `(Xsurface,Ysurface,H)` remains attached to that same fingertip and moves continuously;
6. lowering the index toward the work plane lowers `H`;
7. low texture degrades to `unknown`, never an anatomically wrong finite/HIGH point.

Touch/click thresholds remain Phase 2C.
