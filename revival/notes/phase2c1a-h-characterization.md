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
4. Perform the labelled-by-order conditions:
   - HIGH HOVER: index clearly high above the table and stable;
   - CLEAR GAP: remove the hand from view;
   - NEAR HOVER: index as close to the table as possible without touching and stable;
   - CLEAR GAP: remove the hand from view;
   - PHYSICAL CONTACT: fingertip visibly resting on the table and held still;
   - CLEAR GAP: remove the hand from view.
5. Finish with an empty scene.

Do not perform taps or dragging in this diagnostic. The goal is stable per-condition H distributions, not contact-event acceptance.

## What to extract

For each condition, review only frames where the authoritative identity/sample is current and valid. Record:

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

## Physical run #1 result

A real-device recording of approximately 99.7 s was reviewed on 2026-08-23.

Readable current/valid transition telemetry during the first (high-hover) hand-present interval included smoothed H values around:

```text
60.0 mm
59.6 mm
61.6 mm
```

The second (near-hover) interval produced only sparse valid samples; one clearly readable valid transition reported approximately:

```text
H = 43.7 mm
```

During the third (physical-contact) interval, Phase 2C telemetry was dominated by `NO_FINGER` / `INVALID_SAMPLE` with identity/fingertip state frequently UNKNOWN, stale or non-current. No stable current/valid CONTACT H series was obtained.

Final semantic counters remained:

```text
DOWN_total = 0
UP_total   = 0
OS_INJECTION=DISABLED
```

The run is therefore **inconclusive for CONTACT-H threshold tuning but diagnostically useful**. High versus near hover moved in the expected direction, but the physical-contact pose did not produce enough eligible samples to compute a contact distribution or near/contact overlap. Raw-A versus smoothed-H offset also remains uncharacterized because adjacent valid raw-A telemetry was too sparse.

The dominant blocker exposed by this run is now authoritative identity/sample continuity at or extremely near physical contact. Do not change the 6/4/8 mm thresholds from this evidence alone.

Detailed run note: `revival/notes/phase2c1a-h-characterization-smoke-2026-08-23.md`.

## Decision rule

Do **not** change the current 6/4/8 mm thresholds until physical-contact samples are measurable.

The next implementation decision must be data-backed:

- if CONTACT H forms a stable band clearly separated from NEAR HOVER, threshold revision may be sufficient;
- if CONTACT and NEAR HOVER overlap heavily but a consistent geometric offset/proxy can explain the fingertip-to-contact-point difference, implement that proxy as a separate conservative slice;
- if raw H is near the physical surface while smoothed H is not, inspect the smoothing/contact-input boundary instead of touching surface calibration;
- if identity validity dominates the failure, fix contact eligibility continuity rather than H thresholds;
- never alter the accepted surface frame or K/D/R/T/P/Q merely to force contact H toward zero.

## Required next diagnostic

The next physical run should be self-labelling and machine-readable rather than relying on sparse console heartbeats. Add diagnostic-only operator labels (`HIGH`, `NEAR`, `CONTACT`) and per-frame CSV capture of label, identity/current/stale state, fingertip validity, source, raw H, smoothed H and rejection reason. Invalid frames must also be recorded so validity rate per pose can be measured.

This instrumentation must not alter accepted tracking/contact behavior, thresholds, calibration, surface geometry or OS output.

## Diagnostic implementation

The Phase 2C.1A runtime diagnostic is self-labelled and machine-readable:

- `H` selects `HIGH`;
- `N` selects `NEAR`;
- `C` selects `CONTACT`;
- `0` selects `NONE` for gaps and unlabelled frames.

The current label is printed when it changes. A timestamped
`touchplus-phase2c1a-YYYYMMDD-HHMMSS.csv` is created beside the runtime
executable. It contains one row per processed frame, including invalid,
UNKNOWN, stale and non-current observations, with this schema:

```text
timestamp_utc,frame,physical_label,identity_id,identity_accepted,identity_current,identity_stale,fingertip_valid,fingertip_source,raw_h_mm,smoothed_h_mm,contact_state,contact_event,rejection_reason
```

Invalid H values are emitted as `nan`. The file header is flushed immediately,
rows are flushed every 30 frames and on label changes, and the stream is flushed
and closed during normal runtime shutdown.

The physical smoke must use accepted default A mode:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\start-touchplus-phase2b9c.ps1
```

Press `B` once on a clear scene and wait for `background=READY`, then record
stable `HIGH`, `NONE`, `NEAR`, `NONE`, `CONTACT`, `NONE` intervals. Close with
`Q` or `ESC` so the CSV is closed cleanly. Do not enable hybrid promotion for
this characterization.

The implementation is diagnostic-only. The 6/4/8 mm thresholds, contact state
machine, tracking and identity/fusion behavior, A/B selection, calibration,
surface frame and OS output remain unchanged.

## Safety boundary

- PR #17 remains Draft;
- no merge from this diagnostic alone;
- `OS_INJECTION=DISABLED` remains mandatory;
- no mouse/touch/PointerMapper/UDP output;
- raw personal video remains evidence only and is not committed.
