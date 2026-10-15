# Phase 2C.1 — REA-inspired Ractiv contact investigation (2026-10-16)

## Scope / status

Evidence-only research note for PR #17 (`revival/phase2c1-contact-semantics`).
No runtime or threshold change, no calibration update, no merger or OS injection.
Initial PR #17 physical smoke: **FAIL / safe false-negative** (0 DOWN, 0 UP).
Subsequent 2C.1A–2C.1I smokes have already tested labelled H/raw-dense/shadow contact diagnostics. The 2C.1I CSV proved that trusted FUSED/ANATOMY fingertip targeting disappeared at contact. The current live frontier is 2C.1J.1, not initial H characterization.
Branch head inspected before this note: `2cb21738398b0f81d753792297a580650b3ce508`.
This note itself does **not** claim any new BUILD, CI or PHYSICAL PASS.

## Origin and limits of REA

Investigated upstream `morluto/rea` (MIT), README, `docs/native-investigation.md`,
`docs/windows-ghidra-p0.md` and `docs/installation.md`.
REA supplies an evidence-first local CLI/MCP workflow for tracing native-binary
functions, symbols, calls and decompilation with supported providers; it is
**not** a Touch+ runtime, a fingertip/contact detector, or a substitute for a
physical smoke. No REA MCP server was connected to the hardware in this audit.

Windows native Ghidra P0 requires compatible Node, a separately installed
Ghidra/JDK and read-only bounded analysis. Native x86 (32-bit) Windows PE
support is present in the REA repository main documentation, but is not
promised by the historical npm releases up to 5.0.0. Verify the **installed
release and `rea doctor`/capabilities** before planning a Win32 PE decompile;
do not assume x86 parity from an x64 success. REA should be used for an actual
opaque native binary only when an unanswered question cannot be resolved from
the available historical source.

REA's evidence discipline is useful immediately: separate **historical
source facts**, **runtime observations**, **hypotheses**, and **unverified
inferences**; cite file/function and exact SHA for each finding.

## Historical Ractiv contact path: direct source evidence

On the July 2015 `master` tree:

1. `track_plus_core/track_plus/main.cpp`: when `pose_name == "point"`, the
   pipeline invokes `HandResolver::compute` and then
   `PointerMapper::compute`. The legacy downstream separately sends UDP cursor
   messages: never compile or run this output path in a contact investigation.
2. `track_plus_core/track_plus/pointer_mapper.cpp`,
   `PointerMapper::compute_cursor_point`: the two precise 2D eye coordinates
   reconstruct `pt_target` in 3D through `reproject_to_3d`.
3. The historical mapper projects the point onto a locally calibrated plane;
   four calibration corner samples are retained. `compute_hit_dist` uses an
   inverse-distance-weighted blend of corner-specific distances, yielding a
   location-dependent interaction adjustment.
4. The legacy decision uses unfiltered `dist_target_plane -
   hit_dist_processed` for actuation with hysteresis:
   entry at `actuation_dist + 2`, release above `actuation_dist + 10`.
   The adjusted value includes the historical processed-hit update and is
   **not** identical to Revival's perpendicular `H`.
5. That branch also performs old pose/coarse-index classification already
   known to be anatomically unreliable in Recovery. Its raw actuation logic is
   **not** proof of safe contact and cannot be imported verbatim.

Pointers:
- https://github.com/shinobione/touch_plus_source_code/blob/master/track_plus_core/track_plus/pointer_mapper.cpp
- https://github.com/shinobione/touch_plus_source_code/blob/master/track_plus_core/track_plus/main.cpp
- PR #17: https://github.com/shinobione/touch_plus_source_code/pull/17
- REA upstream: https://github.com/morluto/rea

The Ractiv source header references the Aladdin Free Public License:
review licensing before copying any implementation into modern Revival.
Implement concepts independently if required; preserve provenance.

## What this means for the current failure

PR #17's first physical smoke saw authoritative H in the tens of mm during
attempted contact while empty/hover was safe. Later 2C.1G.1/H/I work showed
raw-dense CONTACT H bands and rejected contaminated minima, but 2C.1I's
labelled replay **could not evaluate contact**: trusted FUSED/ANATOMY target
counts were 0/0; 93 of 1128 rows had valid raw-dense samples, all
GEOMETRY_ONLY, and the geometry pixel was visibly the wrong branch at
contact. This does NOT implicate the surface model or Q.
Two mechanisms remain to discriminate:

- **Contact-point geometry / representation:** the reliably measured anatomical
  pixel may sit above/behind the actual pad/table contact location. A local
  contact-interaction correction (not camera calibration) could be meaningful.
- **Upstream sampling / occlusion:** fingertip Z/identity may vanish at the
  last centimetre; a correction cannot manufacture physical contact evidence
  while frames are invalid.

Older PR #10 documented some genuinely near-surface H samples followed by
stereo/identity loss and tried an occlusion bridge. It is an *experimental,
unmerged, different baseline*, not permission to cherry-pick that bridge or
reinterpret the PR #17 evidence.

## Latest exact-head physical gate: 2C.1J.1 (priority before any REA binary work)

At implementation head `2cb21738398b0f81d753792297a580650b3ce508`,
2C.1J introduced separate shadow-anatomy IPC to probe real LEFT anatomy
even when accepted hand_valid=0 and anatomy=UNAVAILABLE. Its first
physical launch produced strong viewer tearing / capture falling to ~17–23 fps.
The accepted single-sidecar launcher restored clean ~30 fps. 2C.1J.1 narrows
shadow probes to one in six frames (~5 Hz), only when accepted hand is
invalid and accepted anatomy UNAVAILABLE, bounds mask size and caps OpenCV
to one thread. CI of that implementation SHA was green, **but no post-fix
physical 2C.1J.1 PASS was found in the PR record**.

Required immediate smoke:
1. Download the exact-head `touchplus-phase2c1j-shadow-anatomy-windows`
   artifact ID `9569827500` from GitHub Actions; preserve the user's
   existing `surface/0101007379.json` and local calibration/recording assets.
   Do not reuse a historical `sandbox:/` link.
2. With a genuinely empty scene press B, wait for background READY, confirm
   stable LEFT/RIGHT and capture/source near the accepted ~30 fps baseline.
   If tearing/low rate persists, stop and classify **PHYSICAL FAIL**; do not
   use shadow anatomy as contact evidence.
3. Label HIGH / NEAR / CONTACT / NONE in the shadow kit (1 / 2 / 3 / 0),
   keep a clearly extended index visible, and inspect scheduled
   `[ANATOMY_SHADOW] probe=RUN` where `accepted_hand=0`.
4. Evaluate whether `shadow_status=GUIDED_DISTAL` lands on the **visible
   real distal index** in contact frames, as opposed to a proximal or wrong
   branch. The shadow stream is not an accepted result even if this succeeds.
5. Record original artifact SHA, scene, sample counts, false anatomical points,
   valid probes, performance, CSV and physical verdict in the PR; no footage
   in public GitHub. If the result is positive, plan a new gated adapter
   rather than changing 2B authority in this smoke.

Upstream REA can later help only if a *specific opaque native-binary question*
remains unanswered by `master` source and current diagnostic logs.

## Proposed next reversible experiment — no runtime code change yet

A. Prefer the already reviewed 2C.1I labels/CSV summary, and first complete
   2C.1J.1 physical anatomy gate above. The 2C.1I dataset alone is **not
   eligible** for a contact-offset fit because no FUSED/ANATOMY targets existed.
   Keep raw frames/CSV private; publish only derived aggregate statistics if
   authorized.
B. Create an offline, read-only table by operator label HIGH/NEAR/CONTACT/NONE:
   accepted/invalid frame counts and reasons; raw/smoothed H distribution
   (n, median, p05/p95), XY regions; identity/source and timestamp continuity.
   Record uncertainty and exact source artifact SHA. Do not reduce the data
   to one 'contact offset' value.
C. Compare **same-XY-zone** near-hover vs visually confirmed sustained
   contact to determine whether H distributions separate. Check if the
   reported fingertip point is proximal to the actual contact patch in LEFT
   overlays and whether that point remains current.
D. Only if labelled data supports a reproducible separation, add a
   **non-authoritative, diagnostic-only** experiment that estimates a
   location/pose-conditioned contact-evidence residual from labelled contact
   samples. It MUST NOT change official `H`, camera/surface calibration,
   current fingertip identity, selected A/B path, 2C DOWN events or thresholds.
   If the evidence instead shows disappearing Z/identity, target that cause
   explicitly before considering interaction correction.

### Preconditions / gates

- Freeze accepted `K/D/R/T/P/Q`, `calibration/<serial>.json`,
  `surface/<serial>.json`, identity selection and official `H`.
- Negative cases: empty scene, high hover, near-hover (including tabletop
  objects/knuckles), invalid/stale identity, stereo loss, sideways drag and
  different XY surface regions; no diagnostic correction may bypass these.
- Future authoritative proposal requires **zero false DOWN** on physically
  labelled hover/empty conditions and one DOWN/UP per real tap, single held
  contact on drag, bounded/one-time UP on identity loss.
- CI/build/physical results must be reported separately; experimental PR
  remains Draft until the *physical* gate passes; OS injection disabled.
- If a binary-specific question still remains, use REA only on a legitimately
  obtained local *copy* of a historical native EXE/DLL: hash it, inspect
  static call/data flow, export a reproducible evidence dossier, and never
  execute legacy cursor/UDP injection. Do not upload personal footage or
  proprietary vendor binaries.

## Decision (2026-10-16)

**Evidence-backed path found; not a contact fix.** The Ractiv
location-dependent interaction adjustment is a plausible design insight.
Prioritize the pending 2C.1J.1 physical retest, not retuning from the
already shown non-evaluable 2C.1I labels or a fresh REA installation. No user PC action,
git pull, or Codex quota is needed just to record this finding.
