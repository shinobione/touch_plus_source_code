# TouchPlus — Codex operating rules

This repository contains both the historical Ractiv Touch+ source and the modern TouchPlus Revival effort. Codex may do substantial engineering work here, but it must preserve the hardware-validated boundaries documented below.

## 1. Read this before changing code

For Revival work, inspect in this order:

1. `AGENTS.md`
2. `revival/REVIVAL-ROADMAP.md`
3. `README.md`
4. `revival/README.md`
5. the active PR body and exact head SHA
6. the relevant `revival/notes/*.md`
7. exact-head GitHub Actions results

Do not treat an old chat summary, stale artifact link, or previous branch head as canonical.

Repository: `shinobione/touch_plus_source_code`
Canonical Revival integration branch: `revival/main`

## 2. Current engineering focus

At the time this file was introduced, the active Revival experiment is PR #14 on:

`revival/hybrid-ractiv-refiner`

Purpose: evaluate a Ractiv-inspired local full-resolution distal refiner on top of the already accepted modern Revival fingertip identity.

PR #14 is experimental and Draft. Phase 2C PR #10 remains paused/Draft while the hybrid fingertip work is evaluated.

Always re-read the live PRs before acting because this section can become stale.

## 3. Hardware-validated boundaries — do not casually modify

The following layers have been physically validated and must be treated as protected architecture unless the user explicitly opens a new regression/revalidation boundary:

- Etron/USB Touch+ unlock and persistent capture path;
- real stereo split: 1280x480 -> LEFT/RIGHT 640x480;
- accepted per-device camera calibration for unit `0101007379`;
- accepted `K / D / R / T / P / Q` and metric depth geometry;
- Phase 2A working-surface frame model and `Xsurface / Ysurface / H` semantics;
- accepted Phase 2B modern fingertip identity/fusion safety behavior;
- Touch+ stereo remains the authority for metric Z/H;
- landmark/DNN outputs are 2D anatomical guidance only;
- OS input injection remains disabled unless a later explicit phase enables it.

Never silently rescale calibration, replace Q, change baseline/focal scale, or reinterpret H to make a downstream test pass.

## 4. Safety invariants

These are binding project rules:

- **wrong finite/high-confidence fingertip = BLOCKER**;
- **UNKNOWN is acceptable when identity is uncertain**;
- stale, ambiguous, contradictory or unsupported anatomy must fail closed;
- CI PASS is permission to perform a physical smoke test, not proof of hardware success;
- a physical FAIL must not be rewritten as PASS because synthetic tests are green;
- personal test videos/frames must not be committed unless the user explicitly requests it;
- do not merge an experimental runtime slice before its required physical gate passes;
- do not enable mouse/touch/PointerMapper/UDP/OS injection during diagnostic phases.

## 5. Codex ownership vs physical-bench ownership

Codex is the primary engineering environment for:

- repository archaeology and code search;
- C/C++ implementation and refactors;
- CMake/MSVC/Win32 work;
- self-tests and regressions;
- GitHub Actions workflows;
- branch/PR maintenance;
- packaging diagnostic artifacts;
- reviewing diffs and keeping docs in sync.

Physical hardware judgement remains external to Codex. The user/ChatGPT bench loop owns:

- running the real Touch+;
- clean-background procedure and physical setup;
- inspecting overlays/video;
- deciding whether a pixel is anatomically the real fingertip;
- validating real metric H/contact behavior;
- final PHYSICAL PASS / FAIL decisions.

Codex must not infer a hardware PASS from CI alone.

## 6. Change discipline

Before implementation:

1. state the exact branch/PR/head being modified;
2. identify protected layers touched by the proposed diff;
3. prefer the smallest reversible experimental slice;
4. define the synthetic regression and the physical acceptance gate before promotion.

During implementation:

- preserve existing telemetry where it is needed for physical diagnosis;
- add explicit diagnostic reasons rather than hiding rejection behind one generic state;
- keep authoritative and shadow/experimental outputs separate;
- when evaluating a replacement path, run it in shadow first whenever practical;
- do not let a shadow path influence the accepted runtime until promoted by an explicit follow-up slice.

After implementation:

1. run relevant x64 and Win32 self-tests;
2. run Phase 1C calibration/Q and Phase 2A surface regressions when the metric path is implicated;
3. build the actual Win32 runtime;
4. inspect the final diff;
5. verify exact-head CI;
6. package only the intended diagnostic executables/dependencies;
7. update the PR body and relevant `revival/notes/` file;
8. stop before merge if a physical smoke is still required.

## 7. Current hybrid Ractiv-refiner constraints

For the PR #14 family of experiments:

- modern Revival identity stays authoritative;
- the Ractiv-inspired refiner may only refine an already accepted modern fingertip candidate;
- it may not create/reacquire hand or finger identity;
- the hybrid B path must remain shadow-only until explicitly promoted;
- existing A path remains the only authoritative metric fingertip;
- B-only evidence cannot create a final fingertip;
- rejected hybrid output falls closed;
- do not modify smoothing/contact semantics/OS output as part of a refiner evaluation;
- preserve K/D/R/T/P/Q and the surface frame unchanged.

## 8. Historical Ractiv code

Historical Ractiv code is valuable as an implementation reference, not as an automatically trusted final runtime.

Recovery work proved that useful historical pieces can still run, but Ractiv coarse index identity/pose classification is not sufficiently reliable to own the final fingertip identity. Port individual useful concepts into Revival behind modern safety gates rather than resurrecting the complete 2015 product stack.

## 9. Artifacts and local physical data

Do not assume old `sandbox:/...` links remain valid. Prefer current GitHub Actions artifacts.

Do not delete or overwrite the user's local physical assets. In particular, per-setup surface/calibration captures and other bench datasets may live outside the repo and are not recoverable from Git alone.

## 10. Definition of done for a Codex coding slice

A coding slice is complete when:

- the intended code/doc delta is committed on the correct branch;
- tests/builds relevant to the slice pass;
- exact-head CI is checked when available;
- the PR clearly states what is authoritative vs diagnostic;
- the physical smoke protocol and blocker rule are explicit;
- Codex has **not** claimed physical acceptance without real hardware evidence.
