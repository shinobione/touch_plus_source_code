# Phase 2C.1A — authoritative fingertip H characterization

Date: 2026-08-23

## Why this diagnostic exists

Phase 2C.1 first physical smoke failed conservatively: no false `TOUCH_DOWN` occurred, but no intended physical contact produced `TOUCH_DOWN` either. The accepted contact state machine currently uses the authoritative `smoothed_tip` H with first-smoke thresholds 6/4/8 mm, while the reviewed real-device smoke showed contact/near-contact H in the tens of millimetres.

This diagnostic is intentionally **measurement-only**. It does not change thresholds, the accepted surface frame, K/D/R/T/P/Q, fingertip selection, stereo, smoothing, Phase 2B, or OS injection.

## Existing telemetry is sufficient

No new runtime instrumentation is required before this characterization. The current Phase 2C.1 runtime already exposes both quantities needed for the comparison:

- `[CONTACT] heartbeat` → the exact authoritative `smoothed_tip` X/Y/H consumed by the contact state machine;
- `[HYBRID] heartbeat` → the accepted A raw metric sample (`rawXYZ`, including raw H) in the same running session.

The first characterization therefore reuses the exact implementation artifact `9490116b8c727a4c7bfca3c9f3adcf3b2d01ed79` and its existing telemetry. This avoids introducing a new software variable before measuring the physical offset.

## Required physical protocol

Use default accepted A mode. Do not enable hybrid promotion.

1. Start a fresh tracker/sidecar session.
2. With no hand visible, press `B` once and wait for `background=READY`.
3. Leave the scene empty for ~5 s.
4. Perform **three complete cycles** of the following labelled-by-order conditions:
   - HIGH HOVER: index clearly high above the table, stable for 10 s;
   - CLEAR GAP: remove the hand from view for 3 s;
   - NEAR HOVER: index as close to the table as possible without touching, stable for 10 s;
   - CLEAR GAP: remove the hand from view for 3 s;
   - PHYSICAL CONTACT: fingertip visibly resting on the table, held still for 10 s;
   - CLEAR GAP: remove the hand from view for 3 s.
5. Finish with an empty scene for ~5 s.

Do not perform taps or dragging in this diagnostic. The goal is stable per-condition H distributions, not contact-event acceptance.

## What to extract

For each of the three conditions and each repetition, review only frames where the authoritative identity/sample is current and valid. Record:

- contact-consumed `smoothed_tip H` from `[CONTACT] heartbeat`;
- accepted raw H from the adjacent `[HYBRID] heartbeat` when available;
- fingertip source label (expected A in this first characterization);
- identity confidence/currentness;
- count of valid vs UNKNOWN/stale/invalid observations.

Summarize, per condition, at minimum:

- valid sample count;
- minimum H;
- median H;
- maximum H;
- approximate spread / overlap between HIGH HOVER, NEAR HOVER and CONTACT;
- raw-vs-smoothed H offset.

## Decision rule

Do **not** change the current 6/4/8 mm thresholds until these distributions are reviewed.

The next implementation decision must be data-backed:

- if CONTACT H forms a stable band clearly separated from NEAR HOVER, threshold revision may be sufficient;
- if CONTACT and NEAR HOVER overlap heavily but a consistent geometric offset/proxy can explain the fingertip-to-contact-point difference, implement that proxy as a separate conservative slice;
- if raw H is near the physical surface while smoothed H is not, inspect the smoothing/contact-input boundary instead of touching surface calibration;
- if identity validity dominates the failure, fix contact eligibility continuity rather than H thresholds;
- never alter the accepted surface frame or K/D/R/T/P/Q merely to force contact H toward zero.

## Safety boundary

- PR #17 remains Draft;
- no merge from this diagnostic alone;
- `OS_INJECTION=DISABLED` remains mandatory;
- no mouse/touch/PointerMapper/UDP output;
- raw personal video remains evidence only and is not committed.
