# TouchPlus Revival — Codex handoff

This document is the short operational entry point for Codex. `revival/REVIVAL-ROADMAP.md` remains the canonical accepted-stack reference; the active PR + this handoff carry the current experimental slice.

## Working model

Use a split workflow:

- **Codex = primary repo engineer**: code search, implementation, CMake/MSVC, tests, CI, branches/PRs, packaging and code review.
- **Physical bench loop = hardware authority**: real Touch+ execution, visual/anatomical judgement, metric/contact smoke tests and final physical PASS/FAIL.

Do not blur these roles. CI can prove software consistency; only the real device can close a hardware gate.

## Canonical base

Repository: `shinobione/touch_plus_source_code`

Revival integration branch:

`revival/main`

Read `AGENTS.md` before modifying anything.

## Active/paused work

### Active experiment

PR #14

Branch:

`revival/hybrid-ractiv-refiner`

2B.10A physically validated the Ractiv-inspired local full-resolution distal refiner when driven by accepted modern Revival fingertip identity.

2B.10B then evaluated that refined pixel through the existing robust Touch+ stereo primitives in a **shadow A/B** path while keeping A authoritative.

Physical 2B.10B result recorded on 2026-08-23:

```text
refiner accepts / attempts = 30 / 62
shadow valid / attempted   = 28 / 30
both A+B valid             = 26
A_only                     = 1
B_only                     = 2
```

Verdict: **PHYSICAL PASS / PROMISING**. A remains authoritative. B remains shadow-only.

The next minimal slice is **Phase 2B.10C — counterfactual promotion gate**. It must not promote B into runtime output. It should only compute whether a frame would have selected B under a strict gate.

Before continuing, fetch the current PR #14 body and exact head SHA; do not rely on a head recorded in an old chat.

### Paused contact experiment

PR #10

Branch:

`revival/phase2c-touch-contact`

This work is intentionally paused while fingertip refinement is being evaluated. Do not resume or merge it implicitly as part of PR #14 work.

### Historical recovery reference

Ractiv Recovery work exists as a separate historical/experimental line. It demonstrated that parts of the July 2015 pipeline still run and that the local full-resolution refiner is useful when seeded with the correct finger identity. It did **not** establish Ractiv's own coarse index identity as reliable enough to replace Revival.

## Protected accepted stack

Treat these as frozen unless the task explicitly says otherwise:

`Touch+ USB/Etron -> persistent stereo capture -> accepted local camera calibration -> rectification -> robust stereo/depth -> surface frame -> accepted modern fingertip identity`

The following remain authoritative:

- camera calibration matrices and Q;
- metric depth from Touch+ stereo;
- surface-frame coordinates and H;
- modern fingertip identity/fusion safety;
- fail-closed behavior on uncertain anatomy.

## Hybrid refiner ownership rule

The Ractiv-inspired refiner may only answer:

> Given a fingertip identity already accepted by Revival, can a local full-resolution search move the candidate toward the true visible distal boundary without switching to another digit?

It may **not** answer:

> Which finger is the index?

That identity remains the responsibility of the accepted modern pipeline.

## 2B.10C counterfactual promotion gate

2B.10C must remain diagnostic and output only a selection decision such as:

```text
KEEP_A
WOULD_SELECT_B
```

`WOULD_SELECT_B` may be emitted only when:

- A and B are both valid;
- modern identity/fusion remains current and accepted;
- B improves the confidence/support evidence strictly enough to justify evaluation;
- refined 2D displacement remains within explicit bounded limits;
- A/B metric delta is finite and within explicit coherence bounds.

Hard exclusions:

- `B_only` is never promotable in 2B.10C;
- UNKNOWN/stale identity is never promotable;
- non-finite or excessive metric deltas are never promotable;
- a rejected/inward hybrid refinement is never promotable;
- official runtime output, smoothing and XYZ/H remain A-only;
- Phase 2C and OS injection remain untouched.

The later physical gate should inspect only `WOULD_SELECT_B` frames. Any anatomically wrong finite candidate remains a BLOCKER.

## Promotion rule

Do not promote B into the authoritative path on CI evidence alone.

A real-device review remains binding. The core safety rule is:

**wrong finite/HIGH fingertip = BLOCKER; UNKNOWN is acceptable.**

If B is useful only on a subset of poses, it may remain shadow diagnostic or be promoted only behind a strict confidence gate in a later slice.

## Recommended Codex session start

At the beginning of a Codex session:

1. inspect `git status` and current branch;
2. read `AGENTS.md`;
3. read `revival/REVIVAL-ROADMAP.md`;
4. fetch/inspect the active PR and exact head;
5. read the relevant `revival/notes/phase2b10*.md` files;
6. inspect exact-head workflow results;
7. summarize current authoritative path, shadow path, blocker and next smallest slice before editing.

## Recommended end-of-slice handoff

Before returning work to the physical bench:

- commit the smallest coherent change;
- report exact branch + head SHA;
- report exact tests/builds run and their result;
- report exact-head CI state;
- state whether the build is diagnostic or authoritative;
- provide one compact physical smoke protocol;
- state the exact PASS criterion and blocker;
- never claim the physical result in advance.
