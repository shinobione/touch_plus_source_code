# TouchPlus Revival — pause checkpoint

Date: **2026-08-22**

This note freezes the current Revival line before a strategic Ractiv-first investigation. Nothing below is abandoned. The purpose of the pause is to preserve a physically validated modern stack while testing how much of the original Ractiv product can be recovered faster from their own code.

## Canonical status at pause

Repository: `shinobione/touch_plus_source_code`

Canonical integration branch: `revival/main`

Accepted/merged Revival milestones:

- Phase 0 — Etron/USB unlock, persistent atomic stereo capture;
- Phase 1A — stable live stereo viewer;
- Phase 1B — device serial recovery and local calibration workflow;
- Phase 1C — physically validated metric stereo depth;
- Phase 2A — physically validated working-surface frame;
- Phase 2B — physically validated fingertip 3D tracking with fail-closed identity semantics.

Physical unit facts preserved by the Revival stack:

- device serial: `0101007379`;
- stereo frame: 1280x480 MJPEG split into two 640x480 eyes;
- physical measured cadence: approximately 30 fps;
- accepted solved stereo baseline: 59.953 mm;
- physical lens-center spacing: approximately 60–61 mm;
- surface-frame physical validation: table H near 0 mm, 53 mm rigid object measured approximately 54–55 mm.

## Active experimental work being paused

PR #10 — Phase 2C touch/contact semantics

Branch: `revival/phase2c-touch-contact`

Pause head observed before this strategy change:

`36c23253e2ddfc89ff0d33d4fe022c99aba5fdfd`

Status at pause:

- OPEN;
- DRAFT;
- mergeable;
- NOT merged;
- OS injection disabled.

Latest implemented slice: **Phase 2C.1C.1 — deferred precontact identity handoff**.

The specific handoff bug it targeted is covered by synthetic CI. The subsequent real hardware smoke did not produce a false DOWN, but the bridge still did not arm reliably.

### Exact remaining blocker

The current 2C occlusion bridge stores only the two most recent metric pre-contact samples and arms only when that final pair simultaneously proves:

- last H <= 10 mm;
- terminal drop >= 5 mm;
- predicted next H <= 2 mm;
- bounded XY/tip motion;
- bounded metric spacing.

The physical Touch+ stream produces two legitimate patterns that defeat this two-sample rule:

1. **Sparse descent**: an earlier near-surface metric sample and a much lower later sample prove meaningful total descent, but are separated by too many invalid frames.
2. **Dense terminal samples**: several valid samples exist around 4–6 mm, but the final adjacent pair naturally has only a small H delta.

Example observed classes:

- approximately `9.5 -> sparse gap -> 5.2 -> 4.4 mm`;
- approximately `6.1 -> 5.7 mm`.

No threshold was relaxed to work around this. In particular, the accepted Phase 2B stack, camera calibration, surface frame, stereo matcher, DOWN threshold, release threshold and XY safety gate remain unchanged.

If Revival Phase 2C is resumed later, the next candidate slice is a **bounded short pre-contact metric trajectory** that proves a real approach over several recent valid samples while rejecting a stationary low hover.

## Strategic pause rule

Do not merge PR #10 merely to clean up branches.

Keep it as a preserved experimental branch/checkpoint until the Ractiv-first investigation has answered whether the original stack can reach usable pointer/touch behavior more directly.

The modern Revival stack remains the known-good reference and may be reused as:

- hardware unlock/capture fallback;
- local per-device calibration fallback;
- metric-depth validator;
- surface-frame validator;
- diagnostic oracle when testing historical Ractiv behavior.

No personal physical test videos or raw user data should be committed.
