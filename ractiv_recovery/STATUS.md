# Ractiv Recovery Status

Date: 2026-08-22

PR: #12

State: **R0/R1 ACTIVE — DO NOT MERGE — NO HARDWARE SMOKE YET**

Historical source base: `master` / `902cb8cc3e660b0ca8d9049fabd383c34da69607`.

CI base: `ractiv-recovery/ci-base` — contains only the Ractiv Recovery workflow so the historical `master` archive remains untouched.

Current objective: produce a reproducible Win32 **LOG_ONLY** executable from the integrated July Ractiv lineage.

Safety boundary:

- `win_cursor_plus` must not be launched;
- no Windows touch/mouse injection;
- dead factory calibration/CDN path is bypassed for the first 2D bring-up;
- first physical smoke stops at camera/background/hand/mono/pose telemetry;
- packaging refuses `win_cursor_plus`, `daemon_plus` and `menu_plus` executables.

Compatibility patch series:

1. `0001-log-only-bringup.patch`
2. `0002-portable-vs-project.patch`

CI synchronization note:

- the PR-base workflow now targets `ractiv-recovery/ci-base` correctly;
- this head update intentionally retriggers PR CI so the next decision can use exact compiler/linker evidence rather than archaeology guesses.

Next decision is driven by the exact `ractiv-recovery-build-probe` CI artifact, not by guesswork.
