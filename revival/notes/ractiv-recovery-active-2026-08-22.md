# Active Ractiv Recovery experiment — 2026-08-22

This note links the accepted Revival record to the isolated historical recovery line.

## Modern Revival

- canonical branch: `revival/main`
- accepted modern stack is preserved through physically validated Phase 2B;
- Phase 2C experimental work remains preserved separately in PR #10;
- PR #10 is strategically paused and must not be merged while the Ractiv-first experiment is evaluated.

## Historical recovery line

Historical immutable reference commit:

`master` at `902cb8cc3e660b0ca8d9049fabd383c34da69607` (2015-07-20)

Active recovery branch:

`ractiv-recovery/main`

CI-only base branch:

`ractiv-recovery/ci-base`

Active recovery PR:

**#12 — Ractiv Recovery R0/R1 — integrated July baseline + log-only bring-up**

The recovery line intentionally begins from the July integrated Ractiv source, not from Revival code.

## R0/R1 safety boundary

No physical historical-runtime smoke is authorized yet.

The first recovery executable must be LOG_ONLY:

- no `win_cursor_plus` launch;
- no Windows touch injection;
- no mouse injection;
- no trusted historical 3D/contact output yet;
- dead historical calibration/CDN path bypassed for initial 2D bring-up;
- diagnostic scope limited to original camera/background/hand/mono/pose behavior.

Compatibility changes are maintained as separate patches so the 2015 source remains directly auditable.

## Decision rule

Do not abandon or rewrite the accepted Revival stack based on source archaeology alone.

The Ractiv-first experiment wins only if physical hardware evidence shows a materially shorter path to a reliable interaction loop. A hybrid remains explicitly allowed: Ractiv interaction/anatomy components plus Revival calibration, metric geometry, diagnostics and fail-closed safety.
