# Ractiv Recovery Status

Date: 2026-08-22

PR: #12

State: **R0/R1 ACTIVE — DO NOT MERGE — NO HARDWARE SMOKE YET**

Historical base: `master` / `902cb8cc3e660b0ca8d9049fabd383c34da69607`.

Current objective: produce a reproducible Win32 **LOG_ONLY** executable from the integrated July Ractiv lineage.

Safety boundary:

- `win_cursor_plus` must not be launched;
- no Windows touch/mouse injection;
- dead factory calibration/CDN path is bypassed for the first 2D bring-up;
- first physical smoke stops at camera/background/hand/mono/pose telemetry.

Next decision is driven by the exact `ractiv-recovery-build-probe` CI artifact, not by guesswork.
