# Phase 2C.1A — H characterization physical run #1

Date: 2026-08-23

Branch: `revival/phase2c1-contact-semantics`

Purpose: characterize the authoritative fingertip height used by Phase 2C before changing any contact threshold, surface frame, or calibration quantity.

## Evidence

Reviewed real-device screen recording duration: approximately 99.7 s.

The recording contains three clear hand-present intervals separated by no-hand gaps, corresponding to the requested ordered characterization poses: high hover, near hover, and physical fingertip contact.

Runtime remained in default A mode and `OS_INJECTION=DISABLED`.

## Observations

### High hover

The first hand-present interval produced a short burst of current/valid authoritative contact samples. Visible transition telemetry included smoothed contact-input H values approximately:

```text
60.0 mm
59.6 mm
61.6 mm
```

The state remained hover/approach only and produced no touch event.

### Near hover

The second hand-present interval again produced only sparse valid authoritative samples. One clearly readable valid transition reported approximately:

```text
H = 43.7 mm
```

No `TOUCH_DOWN` was produced.

### Physical fingertip contact

During the third hand-present interval, the tracker largely lost eligibility for Phase 2C. Contact telemetry was dominated by:

```text
contact_state=NO_FINGER
contact_reason=INVALID_SAMPLE
```

with identity/fingertip validity frequently UNKNOWN/stale/non-current. No stable current/valid authoritative H series was obtained for the physical-contact pose.

Final semantic counters remained:

```text
DOWN_total = 0
UP_total   = 0
OS_INJECTION=DISABLED
```

## Characterization verdict

**INCONCLUSIVE FOR CONTACT-H THRESHOLD TUNING / DIAGNOSTICALLY USEFUL.**

The run does show the expected direction between the two measurable non-contact poses: high-hover H was around 60 mm while the readable near-hover sample was lower, around 44 mm. However, the physical-contact condition did not provide enough current/valid authoritative samples to estimate a contact H minimum/median/maximum or to quantify overlap with near hover.

Therefore this run does **not** justify changing the current 6/4/8 mm contact thresholds.

The dominant blocker exposed by the characterization is now **authoritative identity/sample continuity at or extremely near physical contact**, not yet a proven numeric H-threshold error. Without a stable valid CONTACT distribution, threshold tuning would be guesswork.

Raw-A versus smoothed-H comparison is also not considered characterized by this recording because valid adjacent `[HYBRID]` raw-A samples were too sparse/inconsistent during the labelled poses.

## Required next action

Before any threshold change, add a narrowly scoped diagnostic that makes the next physical run self-labelling and data-producing rather than relying on sparse console heartbeats.

Recommended diagnostic behavior:

- keep the accepted tracker/contact logic unchanged;
- add explicit operator labels for `HIGH`, `NEAR`, and `CONTACT` (hotkeys or equivalent diagnostic-only markers);
- record per-frame CSV rows containing timestamp/frame, label, identity accepted/current/stale state, fingertip validity, source A/B, raw H, smoothed H, and rejection reason;
- record invalid/UNKNOWN frames too, so loss-of-identity rate per pose is measurable;
- no OS injection and no contact threshold changes;
- use the resulting CSV to compute valid-count, min/median/max and overlap per pose.

If physical CONTACT still destroys identity/sample validity, the next engineering slice should target continuity/eligibility near the surface before revisiting the contact state-machine thresholds.

## Safety boundary

- PR #17 remains Draft and must not be merged from this run;
- do not alter the accepted surface frame;
- do not alter K/D/R/T/P/Q;
- do not raise the contact thresholds from this evidence alone;
- `OS_INJECTION=DISABLED` remains mandatory.
