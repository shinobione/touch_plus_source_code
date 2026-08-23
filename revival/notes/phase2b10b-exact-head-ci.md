# Phase 2B.10B — exact-head CI checkpoint

Date: 2026-08-23

Head at this CI checkpoint:

`80e8c64cda7560e62ec8e5ee254089330388f650`

All workflows on this exact head completed successfully:

- Revival Fingertip 3D #203 — SUCCESS
- Revival Windows Build #377 — SUCCESS
- Revival Surface Frame #221 — SUCCESS
- Revival Calibration Capture Kit #43 — SUCCESS

The Phase 2B Win32 job completed V8, V9, 2B.10A refiner self-test, full Revival Win32 build, Phase 2A regression, Phase 1C/Q regression, packaging and artifact upload.

Artifact:

- GitHub artifact id: `9483902459`
- workflow name: `touchplus-phase2b10a-hybrid-refiner-windows` (legacy workflow label)
- SHA-256: `1369523f910b5bafec0466e367a7d6764d9d21c6f103a867ae005d62ad74ac90`

The packaged tracker executable was verified to contain 2B.10B boundary strings:

- `2B.10B_SHADOW_AB`
- `authoritative=A`
- `B_only`
- `OS_INJECTION=DISABLED`

## Physical follow-up

The physical 2B.10B A/B smoke was subsequently completed on the real Touch+ and is recorded in:

`revival/notes/phase2b10b-shadow-stereo.md`

Canonical physical summary:

```text
refiner accepts / attempts = 30 / 62
shadow valid / attempted   = 28 / 30
both A+B valid             = 26
A_only                     = 1
B_only                     = 2
```

Verdict: **PHYSICAL PASS / PROMISING** while keeping A authoritative and B shadow-only.

This file remains a historical exact-head CI checkpoint; later documentation/governance commits may move the branch head without changing the tested runtime behavior.
