#!/usr/bin/env python3
"""TouchPlus Phase 2B.10M.2 — MediaPipe-advised Ractiv-style refiner benchmark.

Offline/shadow-only evaluator.

M.1 remains the safety gate: Google MediaPipe is allowed to contribute an index
axis only after its 5/6/7/8 chain independently agrees with the conservative
2B.9B.1 GUIDED_DISTAL identity baseline. M.2 then asks a narrower question:

    starting from the SAME proximal baseline model-tip seed,
    does the accepted MediaPipe axis help the Ractiv-style local refiner
    recover the already-known conservative distal reference at least as well
    as the baseline anatomy axis?

This tool cannot publish or rescue a fingertip. The 2B.9B.1 GUIDED_DISTAL point
stays the comparison reference. MediaPipe image/world Z is ignored.

The local refiner below is a Python behavioral mirror of the accepted
`revival/src/fingertip_refiner_v10.h` idea and constants, applied to the
archived full-resolution appearance component. It is deliberately NOT claimed
to be a bit-for-bit execution of the live C++ runtime, whose live V6 mask and
frame state are not present in the archived image-only dataset.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from dataclasses import asdict, dataclass
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Sequence

SCHEMA = "touchplus-mediapipe-refiner-benchmark-v1"
PHASE = "2B.10M.2"

# Mirrors fingertip_refiner_v10.h diagnostic constants.
K_BACKWARD_PX = 18
K_FORWARD_PX = 32
K_LATERAL_PX = 15
K_APPEARANCE_DELTA = 18
K_MIN_COMPONENT_PIXELS = 10
K_MAX_SHIFT_PX = 31.0
K_MAX_LATERAL_RESULT_PX = 13.0
K_MIN_FORWARD_PX = -1.5


@dataclass
class RefineResult:
    accepted: bool = False
    status: str = "NOT_RUN"
    coarse_x: int = -1
    coarse_y: int = -1
    refined_x: int = -1
    refined_y: int = -1
    component_pixels: int = 0
    shift_px: float = 0.0
    forward_px: float = 0.0
    lateral_px: float = 0.0
    axis_dx: float = 0.0
    axis_dy: float = 0.0


def _lround(value: float) -> int:
    # C++ std::lround semantics for the non-negative image coordinates used here.
    return int(math.floor(value + 0.5)) if value >= 0.0 else int(math.ceil(value - 0.5))


def _norm2(x: float, y: float) -> float:
    return math.hypot(x, y)


def _distance(a: Sequence[float], b: Sequence[float]) -> float:
    return math.hypot(float(a[0]) - float(b[0]), float(a[1]) - float(b[1]))


def _mask_near(
    mask: Sequence[int],
    width: int,
    height: int,
    full_x: int,
    full_y: int,
    scale: int,
    radius: int,
) -> bool:
    if width <= 0 or height <= 0 or scale <= 0 or len(mask) < width * height:
        return False
    gx = full_x // scale
    gy = full_y // scale
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            x = gx + dx
            y = gy + dy
            if 0 <= x < width and 0 <= y < height and int(mask[y * width + x]) != 0:
                return True
    return False


def refine_distal_tip_v10_mirror(
    current_gray: Sequence[int],
    background_gray: Sequence[int],
    image_width: int,
    image_height: int,
    hand_mask: Sequence[int],
    mask_width: int,
    mask_height: int,
    depth_scale: int,
    coarse_x: int,
    coarse_y: int,
    palm_x: float,
    palm_y: float,
    anatomy_axis_dx: float,
    anatomy_axis_dy: float,
) -> RefineResult:
    """Behavioral Python mirror of the accepted V10 local distal refiner.

    It intentionally mirrors the bounded local corridor, appearance threshold,
    anchored-component selection, distal-cap median and fail-closed motion gates.
    The archived benchmark supplies a full-resolution appearance component as the
    hand mask, so `depth_scale=1`; live V6 depth/support state is not reconstructed.
    """
    out = RefineResult(coarse_x=coarse_x, coarse_y=coarse_y)
    pixels = image_width * image_height
    mask_cells = mask_width * mask_height
    if (
        image_width <= 0
        or image_height <= 0
        or mask_width <= 0
        or mask_height <= 0
        or depth_scale <= 0
        or len(current_gray) < pixels
        or len(background_gray) < pixels
        or len(hand_mask) < mask_cells
        or coarse_x < 0
        or coarse_x >= image_width
        or coarse_y < 0
        or coarse_y >= image_height
    ):
        out.status = "INVALID_INPUT"
        return out

    axis_x = float(anatomy_axis_dx)
    axis_y = float(anatomy_axis_dy)
    axis_norm = _norm2(axis_x, axis_y)
    if not math.isfinite(axis_norm) or axis_norm < 0.50:
        axis_x = float(coarse_x) - float(palm_x)
        axis_y = float(coarse_y) - float(palm_y)
        axis_norm = _norm2(axis_x, axis_y)
        if not math.isfinite(axis_norm) or axis_norm < 8.0:
            out.status = "AXIS_INVALID"
            return out
    axis_x /= axis_norm
    axis_y /= axis_norm
    out.axis_dx = axis_x
    out.axis_dy = axis_y

    radius = K_FORWARD_PX + K_LATERAL_PX + 2
    min_x = max(0, coarse_x - radius)
    max_x = min(image_width - 1, coarse_x + radius)
    min_y = max(0, coarse_y - radius)
    max_y = min(image_height - 1, coarse_y + radius)
    local_w = max_x - min_x + 1
    local_h = max_y - min_y + 1
    if local_w <= 0 or local_h <= 0:
        out.status = "INVALID_INPUT"
        return out

    binary = [0] * (local_w * local_h)
    changed = 0
    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            dx = float(x - coarse_x)
            dy = float(y - coarse_y)
            forward = dx * axis_x + dy * axis_y
            lateral = abs(dx * axis_y - dy * axis_x)
            if forward < -K_BACKWARD_PX or forward > K_FORWARD_PX or lateral > K_LATERAL_PX:
                continue
            if not _mask_near(hand_mask, mask_width, mask_height, x, y, depth_scale, 2):
                continue
            idx = y * image_width + x
            if abs(int(current_gray[idx]) - int(background_gray[idx])) < K_APPEARANCE_DELTA:
                continue
            binary[(y - min_y) * local_w + (x - min_x)] = 1
            changed += 1

    if changed < K_MIN_COMPONENT_PIXELS:
        out.status = "NO_FOREGROUND"
        return out

    labels = [-1] * len(binary)
    next_label = 0
    chosen_label = -1
    chosen_size = 0
    chosen_anchor_d2 = math.inf
    neighbors = ((-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1))

    for sy in range(local_h):
        for sx in range(local_w):
            seed = sy * local_w + sx
            if not binary[seed] or labels[seed] >= 0:
                continue
            queue = [seed]
            labels[seed] = next_label
            head = 0
            component_size = 0
            anchor_d2 = math.inf
            inward_x = float(coarse_x) - axis_x * 5.0
            inward_y = float(coarse_y) - axis_y * 5.0

            while head < len(queue):
                flat = queue[head]
                head += 1
                ly = flat // local_w
                lx = flat - ly * local_w
                x = min_x + lx
                y = min_y + ly
                component_size += 1
                adx = float(x) - inward_x
                ady = float(y) - inward_y
                anchor_d2 = min(anchor_d2, adx * adx + ady * ady)

                for nx, ny in neighbors:
                    xx = lx + nx
                    yy = ly + ny
                    if xx < 0 or xx >= local_w or yy < 0 or yy >= local_h:
                        continue
                    ni = yy * local_w + xx
                    if not binary[ni] or labels[ni] >= 0:
                        continue
                    labels[ni] = next_label
                    queue.append(ni)

            if component_size >= K_MIN_COMPONENT_PIXELS and anchor_d2 <= 100.0:
                if (
                    anchor_d2 < chosen_anchor_d2 - 1e-6
                    or (abs(anchor_d2 - chosen_anchor_d2) <= 1e-6 and component_size > chosen_size)
                ):
                    chosen_label = next_label
                    chosen_size = component_size
                    chosen_anchor_d2 = anchor_d2
            next_label += 1

    if chosen_label < 0:
        out.status = "NO_ANCHORED_COMPONENT"
        return out

    out.component_pixels = chosen_size
    component: list[tuple[int, int, float, float]] = []
    max_forward = -math.inf
    for ly in range(local_h):
        for lx in range(local_w):
            idx = ly * local_w + lx
            if labels[idx] != chosen_label:
                continue
            x = min_x + lx
            y = min_y + ly
            dx = float(x - coarse_x)
            dy = float(y - coarse_y)
            forward = dx * axis_x + dy * axis_y
            lateral = dx * axis_y - dy * axis_x
            component.append((x, y, forward, lateral))
            max_forward = max(max_forward, forward)

    if len(component) < K_MIN_COMPONENT_PIXELS or max_forward < -1.0:
        out.status = "DISTAL_SUPPORT_WEAK"
        return out

    distal_x: list[float] = []
    distal_y: list[float] = []
    for x, y, forward, lateral in component:
        if forward < max_forward - 3.0:
            continue
        if abs(lateral) > K_MAX_LATERAL_RESULT_PX:
            continue
        distal_x.append(float(x))
        distal_y.append(float(y))

    if len(distal_x) < 2:
        out.status = "DISTAL_SUPPORT_WEAK"
        return out

    out.refined_x = _lround(float(statistics.median(distal_x)))
    out.refined_y = _lround(float(statistics.median(distal_y)))

    rx = float(out.refined_x - coarse_x)
    ry = float(out.refined_y - coarse_y)
    out.shift_px = math.hypot(rx, ry)
    out.forward_px = rx * axis_x + ry * axis_y
    out.lateral_px = abs(rx * axis_y - ry * axis_x)

    if out.shift_px > K_MAX_SHIFT_PX:
        out.status = "SHIFT_TOO_LARGE"
        return out
    if out.lateral_px > K_MAX_LATERAL_RESULT_PX:
        out.status = "LATERAL_DRIFT"
        return out
    if out.forward_px < K_MIN_FORWARD_PX:
        out.status = "MOVED_TOWARD_PALM"
        return out

    # Same safety intent as V10's final inward-support check. The archived mask
    # is full-resolution rather than the live downscaled V6 mask.
    inward_support = 0
    for step in (4, 8, 12):
        ix = _lround(float(out.refined_x) - axis_x * float(step))
        iy = _lround(float(out.refined_y) - axis_y * float(step))
        if _mask_near(hand_mask, mask_width, mask_height, ix, iy, depth_scale, 1):
            inward_support += 1
    if inward_support < 2:
        out.status = "DISTAL_SUPPORT_WEAK"
        return out

    out.accepted = True
    out.status = "ACCEPT"
    return out


def _eligible_for_m2(m1_decision: str) -> bool:
    return m1_decision == "ADVISORY_AXIS_ACCEPT"


def _classify_shadow(
    coarse_error: float,
    legacy_result: RefineResult,
    mp_result: RefineResult,
    reference: Sequence[float],
) -> tuple[str, dict[str, float | None]]:
    legacy_error = (
        _distance((legacy_result.refined_x, legacy_result.refined_y), reference)
        if legacy_result.accepted
        else None
    )
    mp_error = (
        _distance((mp_result.refined_x, mp_result.refined_y), reference)
        if mp_result.accepted
        else None
    )
    metrics: dict[str, float | None] = {
        "coarse_to_baseline_reference_px": coarse_error,
        "legacy_axis_refined_to_baseline_reference_px": legacy_error,
        "mediapipe_axis_refined_to_baseline_reference_px": mp_error,
        "mediapipe_vs_legacy_reference_gain_px": (
            legacy_error - mp_error if legacy_error is not None and mp_error is not None else None
        ),
        "mediapipe_vs_coarse_reference_gain_px": (
            coarse_error - mp_error if mp_error is not None else None
        ),
    }

    if not mp_result.accepted:
        return "MP_AXIS_REFINER_REJECT", metrics
    if mp_error is not None and mp_error > coarse_error + 3.0:
        return "MP_AXIS_SHADOW_REGRESSION", metrics
    if not legacy_result.accepted:
        return "MP_ONLY_SHADOW_ACCEPT", metrics
    assert legacy_error is not None and mp_error is not None
    if mp_error <= legacy_error - 1.5:
        return "MP_AXIS_CLOSER_TO_BASELINE_REFERENCE", metrics
    if legacy_error <= mp_error - 1.5:
        return "LEGACY_AXIS_CLOSER_TO_BASELINE_REFERENCE", metrics
    return "AXIS_PARITY_TO_BASELINE_REFERENCE", metrics


def run_self_test() -> int:
    width, height = 160, 100
    pixels = width * height
    background = [0] * pixels
    current = [0] * pixels
    mask = [0] * pixels

    # Synthetic straight finger corridor ending at x=130.
    for y in range(44, 57):
        for x in range(65, 131):
            current[y * width + x] = 220
            mask[y * width + x] = 1

    coarse = (101, 50)
    reference = (130, 50)
    legacy_axis = (1.0, 0.0)
    advisory_axis = (0.995, 0.100)

    a = refine_distal_tip_v10_mirror(
        current, background, width, height, mask, width, height, 1,
        coarse[0], coarse[1], 60.0, 50.0, legacy_axis[0], legacy_axis[1],
    )
    b = refine_distal_tip_v10_mirror(
        current, background, width, height, mask, width, height, 1,
        coarse[0], coarse[1], 60.0, 50.0, advisory_axis[0], advisory_axis[1],
    )
    outcome, metrics = _classify_shadow(_distance(coarse, reference), a, b, reference)

    assert a.accepted and b.accepted, (a, b)
    assert a.refined_x >= 127 and b.refined_x >= 126, (a, b)
    assert metrics["mediapipe_axis_refined_to_baseline_reference_px"] is not None
    assert not _eligible_for_m2("REJECT_MEDIAPIPE_IDENTITY_DISAGREEMENT")
    assert not _eligible_for_m2("BASELINE_REJECT_NO_MEDIAPIPE_RESCUE")
    assert _eligible_for_m2("ADVISORY_AXIS_ACCEPT")
    assert outcome in {
        "AXIS_PARITY_TO_BASELINE_REFERENCE",
        "LEGACY_AXIS_CLOSER_TO_BASELINE_REFERENCE",
        "MP_AXIS_CLOSER_TO_BASELINE_REFERENCE",
    }

    print("Phase 2B.10M.2 refiner mirror self-test")
    print(f"  legacy: status={a.status} refined=({a.refined_x},{a.refined_y})")
    print(f"  mp axis: status={b.status} refined=({b.refined_x},{b.refined_y})")
    print(f"  outcome={outcome}")
    print("SELF_TEST: PASS")
    return 0


def _gray(image: Any) -> Any:
    import cv2
    if len(image.shape) == 2:
        return image
    if image.shape[2] == 4:
        return cv2.cvtColor(image, cv2.COLOR_BGRA2GRAY)
    return cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)


def _draw_overlay(
    image_path: Path,
    baseline: dict[str, Any],
    m1: dict[str, Any],
    legacy_refine: RefineResult | None,
    mp_refine: RefineResult | None,
    outcome: str,
    out_path: Path,
) -> None:
    import cv2

    image = cv2.imread(str(image_path), cv2.IMREAD_COLOR)
    if image is None:
        return

    coarse = baseline.get("index_tip_model")
    reference = baseline.get("guided_tip")
    legacy_axis = baseline.get("distal_axis")
    mp_axis = m1.get("selected_mediapipe_axis")

    if coarse is not None:
        c = (_lround(float(coarse[0])), _lround(float(coarse[1])))
        cv2.circle(image, c, 6, (0, 128, 255), 2, cv2.LINE_AA)
        if legacy_axis is not None:
            e = (
                _lround(c[0] + float(legacy_axis[0]) * 38.0),
                _lround(c[1] + float(legacy_axis[1]) * 38.0),
            )
            cv2.arrowedLine(image, c, e, (0, 220, 0), 1, cv2.LINE_AA, tipLength=0.18)
        if mp_axis is not None:
            e = (
                _lround(c[0] + float(mp_axis[0]) * 38.0),
                _lround(c[1] + float(mp_axis[1]) * 38.0),
            )
            cv2.arrowedLine(image, c, e, (255, 0, 255), 1, cv2.LINE_AA, tipLength=0.18)

    if reference is not None:
        r = (_lround(float(reference[0])), _lround(float(reference[1])))
        cv2.drawMarker(image, r, (0, 255, 255), cv2.MARKER_CROSS, 20, 2, cv2.LINE_AA)

    if legacy_refine is not None and legacy_refine.accepted:
        p = (legacy_refine.refined_x, legacy_refine.refined_y)
        cv2.drawMarker(image, p, (0, 220, 0), cv2.MARKER_TILTED_CROSS, 16, 2, cv2.LINE_AA)

    if mp_refine is not None and mp_refine.accepted:
        p = (mp_refine.refined_x, mp_refine.refined_y)
        cv2.drawMarker(image, p, (255, 0, 255), cv2.MARKER_TILTED_CROSS, 18, 2, cv2.LINE_AA)

    cv2.putText(
        image,
        f"2B.10M.2 {outcome}",
        (10, 24),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.52,
        (255, 255, 255),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        image,
        "orange=coarse | yellow=2B.9B.1 reference | green=legacy-axis refine | magenta=MP-axis refine",
        (10, max(24, image.shape[0] - 12)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.36,
        (255, 255, 255),
        1,
        cv2.LINE_AA,
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out_path), image)


def _fusion_args() -> SimpleNamespace:
    # Exact M.1 diagnostic thresholds used for the archived safety replay.
    return SimpleNamespace(
        min_axis_cos=0.80,
        max_chain_rms_norm=1.25,
        max_model_tip_distance_norm=1.30,
        max_guided_lateral_norm=0.90,
        min_guided_along_norm=-4.0,
        max_guided_along_norm=0.60,
        max_multi_hand_tip_separation_norm=1.0,
        min_multi_hand_score_gap=0.20,
    )


def run_benchmark(args: argparse.Namespace) -> int:
    import touchplus_landmark_probe_b91 as legacy
    import touchplus_mediapipe_benchmark as mpbench
    import touchplus_mediapipe_fusion_benchmark as fusion

    input_path = args.input.resolve()
    background_path = args.background.resolve()
    assets = args.legacy_assets.resolve()
    model = args.mediapipe_model.resolve()
    output = args.output.resolve()

    background = legacy.base._load_image(background_path)
    background_gray = _gray(background)
    images = mpbench.discover_images(input_path, "left")
    palm_detector, handpose_model = legacy.base._load_zoo(assets)

    mp_args = SimpleNamespace(num_hands=2, min_detection=0.5, min_presence=0.5)
    fargs = _fusion_args()

    records: list[dict[str, Any]] = []
    legacy_output = output / "legacy-2b9b1"
    annotations = output / "annotations"
    per_image = output / "per-image"
    annotations.mkdir(parents=True, exist_ok=True)
    per_image.mkdir(parents=True, exist_ok=True)

    print("TouchPlus Phase 2B.10M.2 | MediaPipe advisory axis -> Ractiv-style refiner | SHADOW ONLY")
    print(f"Input:      {input_path}")
    print(f"Background: {background_path}")
    print("Reference:  2B.9B.1 GUIDED_DISTAL (comparison reference, NOT ground truth)")
    print("Seed:       2B.9B.1 raw model INDEX_TIP")
    print("Metric Z:   NOT USED")

    with mpbench.create_landmarker(model, mp_args) as landmarker:
        for index, image_path in enumerate(images, 1):
            baseline = legacy.evaluate_image(
                image_path,
                legacy_output,
                assets,
                background=background,
                palm_detector=palm_detector,
                handpose_model=handpose_model,
            )
            mp_record = fusion._mp_record_for_image(image_path, landmarker)
            m1 = fusion.fuse_one(baseline, mp_record, fargs)

            record: dict[str, Any] = {
                "image": str(image_path),
                "m1_decision": m1.get("decision"),
                "m1_advisory_axis_allowed": bool(m1.get("advisory_axis_allowed")),
                "baseline_guided_status": baseline.get("guided_status"),
                "baseline_reference_tip": baseline.get("guided_tip"),
                "coarse_seed_model_tip": baseline.get("index_tip_model"),
                "reference_is_ground_truth": False,
                "reference_role": "CONSERVATIVE_2B9B1_GUIDED_DISTAL_COMPARISON_ONLY",
                "mediapipe_can_publish_fingertip": False,
                "mediapipe_can_rescue_baseline_reject": False,
                "runtime_modified": False,
                "metric_depth_source": "NOT_RUN_IN_M2",
            }

            legacy_refine: RefineResult | None = None
            mp_refine: RefineResult | None = None

            if not _eligible_for_m2(str(m1.get("decision"))):
                outcome = "NOT_RUN_M1_GATE"
                record["reason"] = "M1_ADVISORY_AXIS_NOT_ACCEPTED"
            else:
                image = legacy.base._load_image(image_path)
                seed = legacy._primary_silhouette_seed(image, background)
                coarse = baseline.get("index_tip_model")
                reference = baseline.get("guided_tip")
                legacy_axis = baseline.get("distal_axis")
                mp_axis = m1.get("selected_mediapipe_axis")
                palm = seed.palm_center if seed.valid else None

                if (
                    not seed.valid
                    or seed.mask is None
                    or palm is None
                    or coarse is None
                    or reference is None
                    or legacy_axis is None
                    or mp_axis is None
                ):
                    outcome = "NOT_RUN_MISSING_REFINER_INPUT"
                    record["reason"] = seed.reason if not seed.valid else "MISSING_AXIS_SEED_OR_REFERENCE"
                else:
                    current_gray = _gray(image)
                    h, w = current_gray.shape[:2]
                    mask = seed.mask
                    mh, mw = mask.shape[:2]
                    coarse_xy = (_lround(float(coarse[0])), _lround(float(coarse[1])))
                    coarse_error = _distance(coarse_xy, reference)

                    legacy_refine = refine_distal_tip_v10_mirror(
                        current_gray.reshape(-1),
                        background_gray.reshape(-1),
                        w,
                        h,
                        mask.reshape(-1),
                        mw,
                        mh,
                        1,
                        coarse_xy[0],
                        coarse_xy[1],
                        float(palm[0]),
                        float(palm[1]),
                        float(legacy_axis[0]),
                        float(legacy_axis[1]),
                    )
                    mp_refine = refine_distal_tip_v10_mirror(
                        current_gray.reshape(-1),
                        background_gray.reshape(-1),
                        w,
                        h,
                        mask.reshape(-1),
                        mw,
                        mh,
                        1,
                        coarse_xy[0],
                        coarse_xy[1],
                        float(palm[0]),
                        float(palm[1]),
                        float(mp_axis[0]),
                        float(mp_axis[1]),
                    )
                    outcome, metrics = _classify_shadow(
                        coarse_error,
                        legacy_refine,
                        mp_refine,
                        reference,
                    )
                    record.update(metrics)
                    record["reason"] = "SHADOW_COMPARISON_COMPLETE"

            record["outcome"] = outcome
            record["legacy_axis_refiner"] = asdict(legacy_refine) if legacy_refine else None
            record["mediapipe_axis_refiner"] = asdict(mp_refine) if mp_refine else None
            record["selected_mediapipe_axis"] = m1.get("selected_mediapipe_axis")
            record["baseline_axis"] = baseline.get("distal_axis")

            overlay = annotations / f"{image_path.stem}_m2.png"
            _draw_overlay(image_path, baseline, m1, legacy_refine, mp_refine, outcome, overlay)
            record["annotation"] = str(overlay)
            (per_image / f"{image_path.stem}.json").write_text(
                json.dumps(record, indent=2), encoding="utf-8"
            )
            records.append(record)

            print(
                f"[{index:03d}/{len(images):03d}] {image_path.name}: "
                f"M1={record['m1_decision']} M2={outcome}"
            )

    counts: dict[str, int] = {}
    for record in records:
        counts[record["outcome"]] = counts.get(record["outcome"], 0) + 1

    summary = {
        "schema": SCHEMA,
        "phase": PHASE,
        "scope": "OFFLINE_SHADOW_AXIS_REFINER_COMPARISON",
        "input": str(input_path),
        "background": str(background_path),
        "legacy_assets": str(assets),
        "mediapipe_model": str(model),
        "method": {
            "m1_gate_required": "ADVISORY_AXIS_ACCEPT",
            "coarse_seed": "2B.9B.1 raw model INDEX_TIP",
            "comparison_reference": "2B.9B.1 GUIDED_DISTAL",
            "comparison_reference_is_ground_truth": False,
            "refiner": "Python behavioral mirror of fingertip_refiner_v10 local logic/constants",
            "archived_hand_mask": "full-resolution learned-background primary appearance component",
            "live_cpp_runtime_executed": False,
            "stereo_executed": False,
        },
        "counts": counts,
        "safety": {
            "diagnostic_only": True,
            "shadow_only": True,
            "runtime_modified": False,
            "mediapipe_can_publish_fingertip": False,
            "mediapipe_can_rescue_m1_reject": False,
            "mediapipe_z_used": False,
            "touchplus_metric_stack_modified": False,
            "phase2c_modified": False,
            "os_injection": "DISABLED",
        },
        "manual_review_gate": [
            "No M2 refiner run is allowed unless M1 == ADVISORY_AXIS_ACCEPT.",
            "Known wrong-finger M cases must therefore remain NOT_RUN_M1_GATE.",
            "Any visually wrong finite magenta MP-axis shadow candidate is a hard fail.",
            "Distance to 2B.9B.1 GUIDED_DISTAL is comparative only; it is not ground truth.",
        ],
        "records": records,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    with (output / "summary.csv").open("w", newline="", encoding="utf-8-sig") as handle:
        fields = [
            "image",
            "m1_decision",
            "outcome",
            "coarse_to_baseline_reference_px",
            "legacy_axis_refined_to_baseline_reference_px",
            "mediapipe_axis_refined_to_baseline_reference_px",
            "mediapipe_vs_legacy_reference_gain_px",
            "mediapipe_vs_coarse_reference_gain_px",
            "legacy_refiner_status",
            "mediapipe_refiner_status",
            "annotation",
        ]
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for record in records:
            writer.writerow({
                "image": record["image"],
                "m1_decision": record.get("m1_decision", ""),
                "outcome": record.get("outcome", ""),
                "coarse_to_baseline_reference_px": record.get("coarse_to_baseline_reference_px", ""),
                "legacy_axis_refined_to_baseline_reference_px": record.get(
                    "legacy_axis_refined_to_baseline_reference_px", ""
                ),
                "mediapipe_axis_refined_to_baseline_reference_px": record.get(
                    "mediapipe_axis_refined_to_baseline_reference_px", ""
                ),
                "mediapipe_vs_legacy_reference_gain_px": record.get(
                    "mediapipe_vs_legacy_reference_gain_px", ""
                ),
                "mediapipe_vs_coarse_reference_gain_px": record.get(
                    "mediapipe_vs_coarse_reference_gain_px", ""
                ),
                "legacy_refiner_status": (
                    record["legacy_axis_refiner"]["status"]
                    if record.get("legacy_axis_refiner") else ""
                ),
                "mediapipe_refiner_status": (
                    record["mediapipe_axis_refiner"]["status"]
                    if record.get("mediapipe_axis_refiner") else ""
                ),
                "annotation": record.get("annotation", ""),
            })

    print(f"Summary:  {output / 'summary.json'}")
    print(f"CSV:      {output / 'summary.csv'}")
    print(f"Overlays: {annotations}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="TouchPlus 2B.10M.2 offline MediaPipe-axis Ractiv-style refiner benchmark"
    )
    parser.add_argument("--input", type=Path)
    parser.add_argument("--background", type=Path)
    parser.add_argument("--legacy-assets", type=Path)
    parser.add_argument("--mediapipe-model", type=Path)
    parser.add_argument("--output", type=Path, default=Path("mediapipe-refiner-m2-output"))
    parser.add_argument("--self-test", action="store_true")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    for name in ("input", "background", "legacy_assets", "mediapipe_model"):
        if getattr(args, name) is None:
            parser.error(f"--{name.replace('_', '-')} is required")
    return run_benchmark(args)


if __name__ == "__main__":
    raise SystemExit(main())
