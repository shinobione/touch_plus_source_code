# Phase 2B.7 physical smoke — 2026-08-20

Physical unit: `0101007379`.

Status: **PARTIAL PASS / FINGERTIP IDENTITY FAIL / DO NOT MERGE**.

This note records the physical bench result without committing the user's raw personal video.

## Build under test

- branch: `revival/phase2b-fingertip-3d`
- PR: `#9`
- slice: **Phase 2B.7 — SCOPA-inspired palm-core finger branch**
- expected runtime banner: `PHASE 2B.7 RUNTIME ACTIVE | tracker=PALM-CORE-BRANCH`

The 2B.7 synthetic tests and Windows builds were green before this physical smoke. Synthetic PASS is not considered physical acceptance.

## What passed on hardware

### Runtime / background

- tracker launches and runs on the physical Touch+;
- the accepted Phase 2A `surface/0101007379.json` loads;
- `B` background learning transitions through `NOT_READY -> LEARNING -> READY`;
- after learning a clear scene, the no-hand baseline can report:

```text
background=READY | no palm-supported hand | changed_cells=0
```

This is a meaningful improvement over the early Phase 2B false-hand failures.

### Palm/branch diagnostics are useful

The new telemetry exposes `palm=x,y r=... | branches=N | tip_pixel=x,y`, which makes it possible to distinguish palm-core failure from finger-branch failure instead of treating the tracker as a black box.

The ambiguity path is also exercised in the physical run: some multi-branch frames correctly degrade to `ambiguous/palm-branches` or `no-dominant-palm-branch` rather than forcing a fingertip.

## What failed on hardware

The core acceptance criterion is still not met: **a single clearly extended index does not keep one anatomically stable fingertip identity**.

During short single-hand / single-index sequences, the runtime can move between substantially different candidate pixels and still emit finite MEDIUM/HIGH results. Representative console observations from the physical bench include:

```text
palm=201,44 r=31.7 | branches=2 | tip_pixel=231,193 | support=4 | confidence=MEDIUM
palm=201,40 r=31.7 | branches=1 | tip_pixel=257,207 | support=4 | confidence=MEDIUM
palm=184,39 r=30.0 | branches=2 | tip_pixel=373,245 | support=7 | confidence=HIGH
palm=183,31 r=29.0 | branches=1 | tip_pixel=373,243 | support=6 | confidence=HIGH
palm=187,41 r=34.3 | branches=1 | tip_pixel=243,167 | support=6 | confidence=HIGH
```

A later single-index sequence likewise produced:

```text
palm=190,33 r=26.3 | branches=1 | tip_pixel=263,173 | support=6 | confidence=HIGH
palm=190,34 r=26.7 | branches=1 | tip_pixel=265,177 | support=9 | confidence=HIGH
palm=187,36 r=27.7 | branches=3 | tip_pixel=189,87  | support=8 | confidence=HIGH
```

The physical imagery does not support treating all of those jumps as the same distal fingertip. Therefore **stereo refinement confidence is still capable of becoming HIGH after the 2D anatomical identity stage has selected the wrong branch/location**.

This is the same safety-class blocker identified in 2B.6, now localized more precisely: the metric matcher can be correct for the pixel it is given, while the palm/branch identity that supplies that pixel is not stable enough.

## Interpretation

2B.7 is still progress:

- learned-background segmentation is retained and works;
- the appearance silhouette keeps low-texture distal skin visible;
- physical-support bounding removes the old giant/shadow-tail failure class;
- palm-core and branch telemetry expose the next failure directly;
- ambiguous multi-branch cases can fail safe.

But **palm-core + branch extraction is not yet sufficiently constrained temporally/anatomically to promote a single-index fingertip**.

The next iteration must not simply loosen stereo refinement or relabel HIGH/MEDIUM thresholds. The identity stage itself needs stronger constraints before metric confidence can be trusted.

## Next design requirement

A successor to 2B.7 should preserve all accepted camera/depth/surface/background layers and focus only on identity. At minimum it should:

1. validate the palm core against a plausible hand region before branch scoring;
2. keep a short temporal palm track so the palm cannot relocate arbitrarily between adjacent frames;
3. track branch identity over time rather than re-electing a fingertip independently every frame;
4. require a candidate branch to leave the palm boundary with finger-like width/length geometry;
5. reject large 2D fingertip jumps unless the hand/palm motion explains them;
6. downgrade to `unknown` when 2D identity is unstable, regardless of strong stereo support;
7. only assign MEDIUM/HIGH metric confidence after both **identity confidence** and stereo refinement pass.

It is also worth comparing this controlled geometry path against a lightweight modern 2D hand-landmark fallback before investing in more ad-hoc endpoint scoring.

## Merge gate

PR #9 remains **Draft / DO NOT MERGE**.

Phase 2B is not accepted until real hardware demonstrates all of the following:

- empty scene remains no-hand after background learning;
- palm diagnostic remains anatomically plausible;
- one clearly extended index keeps the same distal fingertip identity through vertical, horizontal and diagonal motion;
- finite `(Xsurface, Ysurface, H)` stays attached to that fingertip;
- two comparable fingers may safely become `ambiguous/unknown`;
- low texture or unstable identity becomes `unknown`, never an anatomically wrong finite HIGH point.
