#!/usr/bin/env python3
"""TouchPlus Phase 2B.10M.1 — conservative MediaPipe advisory fusion benchmark.

Offline/shadow-only evaluator. The existing 2B.9B.1 Touch+ guided-distal path is
kept as the baseline identity. Google MediaPipe may contribute an anatomical
axis only when it agrees with that baseline. It can never create, replace, or
rescue a published fingertip in this tool.

MediaPipe image/world Z is ignored. Touch+ stereo/Q remains the only metric
authority outside this benchmark.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Sequence

import touchplus_landmark_probe_b91 as legacy
import touchplus_mediapipe_benchmark as mpbench

SCHEMA = "touchplus-mediapipe-advisory-fusion-benchmark-v1"
BASELINE_NAME = "2B.9B.1_GUIDED_DISTAL"
INDEX_CHAIN = (5, 6, 7, 8)


@dataclass(frozen=True)
class Vec2:
    x: float
    y: float

    def __add__(self, other: "Vec2") -> "Vec2":
        return Vec2(self.x + other.x, self.y + other.y)

    def __sub__(self, other: "Vec2") -> "Vec2":
        return Vec2(self.x - other.x, self.y - other.y)

    def __mul__(self, scalar: float) -> "Vec2":
        return Vec2(self.x * scalar, self.y * scalar)


def _dot(a: Vec2, b: Vec2) -> float:
    return a.x * b.x + a.y * b.y


def _cross(a: Vec2, b: Vec2) -> float:
    return a.x * b.y - a.y * b.x


def _norm(v: Vec2) -> float:
    return math.hypot(v.x, v.y)


def _unit(v: Vec2) -> Vec2 | None:
    n = _norm(v)
    if n < 1e-6:
        return None
    return Vec2(v.x / n, v.y / n)


def _dist(a: Vec2, b: Vec2) -> float:
    return _norm(a - b)


def _v(value: Sequence[float]) -> Vec2:
    return Vec2(float(value[0]), float(value[1]))


def _mp_point(hand: dict[str, Any], idx: int, width: int, height: int) -> Vec2:
    lm = hand["landmarks"][idx]
    return Vec2(
        float(lm["x_norm"]) * max(1, width - 1),
        float(lm["y_norm"]) * max(1, height - 1),
    )


def _legacy_chain(result: dict[str, Any]) -> list[Vec2] | None:
    keys = ("index_mcp", "index_pip", "index_dip", "index_tip_model")
    if any(result.get(key) is None for key in keys):
        return None
    return [_v(result[key]) for key in keys]


def _mp_chain(hand: dict[str, Any], width: int, height: int) -> list[Vec2]:
    return [_mp_point(hand, idx, width, height) for idx in INDEX_CHAIN]


def _rms_dist(a: Sequence[Vec2], b: Sequence[Vec2]) -> float:
    return math.sqrt(sum(_dist(x, y) ** 2 for x, y in zip(a, b)) / max(1, len(a)))


def _monotonic_along(chain: Sequence[Vec2], axis: Vec2, scale: float) -> bool:
    projections = [_dot(point, axis) for point in chain]
    tolerance = 0.25 * scale
    return all(b + tolerance >= a for a, b in zip(projections, projections[1:]))


def _candidate_metrics(
    legacy_result: dict[str, Any],
    mp_hand: dict[str, Any],
    width: int,
    height: int,
) -> dict[str, Any]:
    lchain = _legacy_chain(legacy_result)
    if lchain is None:
        raise ValueError("legacy index chain missing")
    guided = _v(legacy_result["guided_tip"])
    laxis_raw = legacy_result.get("distal_axis")
    if laxis_raw is None:
        raise ValueError("legacy distal axis missing")
    laxis = _unit(_v(laxis_raw))
    if laxis is None:
        raise ValueError("legacy distal axis degenerate")

    scale = max(
        float(legacy_result.get("distal_scale_px") or 0.0),
        _dist(lchain[2], lchain[3]),
        8.0,
    )
    mchain = _mp_chain(mp_hand, width, height)
    maxis = _unit(mchain[3] - mchain[1])
    axis_cos = _dot(laxis, maxis) if maxis is not None else -1.0
    chain_rms_norm = _rms_dist(lchain, mchain) / scale
    model_tip_norm = _dist(lchain[3], mchain[3]) / scale

    rel = mchain[3] - guided
    along_norm = _dot(rel, laxis) / scale
    lateral_norm = abs(_cross(rel, laxis)) / scale
    monotonic = _monotonic_along(mchain, laxis, scale)

    return {
        "axis_cos": axis_cos,
        "axis_angle_deg": math.degrees(math.acos(max(-1.0, min(1.0, axis_cos)))) if maxis else 180.0,
        "chain_rms_norm": chain_rms_norm,
        "model_tip_distance_norm": model_tip_norm,
        "guided_tip_along_norm": along_norm,
        "guided_tip_lateral_norm": lateral_norm,
        "mp_chain_monotonic_along_legacy_axis": monotonic,
        "legacy_scale_px": scale,
        "mp_index_tip": [mchain[3].x, mchain[3].y],
        "mp_index_axis": [maxis.x, maxis.y] if maxis else None,
    }


def _passes(metrics: dict[str, Any], args: argparse.Namespace) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    if metrics["axis_cos"] < args.min_axis_cos:
        reasons.append("AXIS_DISAGREEMENT")
    if metrics["chain_rms_norm"] > args.max_chain_rms_norm:
        reasons.append("CHAIN_SPATIAL_DISAGREEMENT")
    if metrics["model_tip_distance_norm"] > args.max_model_tip_distance_norm:
        reasons.append("MODEL_TIP_TOO_FAR_FROM_BASELINE_INDEX")
    if metrics["guided_tip_lateral_norm"] > args.max_guided_lateral_norm:
        reasons.append("TIP_OUTSIDE_BASELINE_DISTAL_CORRIDOR")
    if metrics["guided_tip_along_norm"] < args.min_guided_along_norm:
        reasons.append("TIP_TOO_PROXIMAL_FOR_BASELINE_CHAIN")
    if metrics["guided_tip_along_norm"] > args.max_guided_along_norm:
        reasons.append("TIP_PAST_BASELINE_DISTAL_BOUND")
    if not metrics["mp_chain_monotonic_along_legacy_axis"]:
        reasons.append("INDEX_CHAIN_ORDER_DISAGREES")
    return not reasons, reasons


def _agreement_score(metrics: dict[str, Any]) -> float:
    return (
        1.50 * (1.0 - max(-1.0, min(1.0, metrics["axis_cos"])))
        + 0.55 * metrics["chain_rms_norm"]
        + 0.35 * metrics["model_tip_distance_norm"]
        + 0.25 * metrics["guided_tip_lateral_norm"]
        + 0.10 * abs(min(0.0, metrics["guided_tip_along_norm"]))
    )


def fuse_one(
    legacy_result: dict[str, Any],
    mp_record: dict[str, Any],
    args: argparse.Namespace,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "baseline": BASELINE_NAME,
        "baseline_guided_status": legacy_result.get("guided_status"),
        "baseline_guided_tip": legacy_result.get("guided_tip"),
        "baseline_reason": legacy_result.get("reason"),
        "mediapipe_status": mp_record.get("status"),
        "mediapipe_hand_count": mp_record.get("hand_count", 0),
        "mediapipe_can_publish_fingertip": False,
        "mediapipe_can_rescue_baseline_reject": False,
        "metric_depth_source": "TOUCHPLUS_STEREO_Q_ONLY",
    }

    baseline_ok = (
        legacy_result.get("guided_status") == "GUIDED_DISTAL"
        and legacy_result.get("guided_tip") is not None
        and legacy_result.get("distal_axis_valid") is True
        and _legacy_chain(legacy_result) is not None
    )
    if not baseline_ok:
        result.update({
            "decision": "BASELINE_REJECT_NO_MEDIAPIPE_RESCUE",
            "advisory_axis_allowed": False,
            "selected_mediapipe_hand": None,
            "reason": "BASELINE_IDENTITY_NOT_AUTHORITATIVE",
            "candidates": [],
        })
        return result

    hands = list(mp_record.get("hands") or [])
    if not hands:
        result.update({
            "decision": "KEEP_BASELINE_MEDIAPIPE_UNAVAILABLE",
            "advisory_axis_allowed": False,
            "selected_mediapipe_hand": None,
            "reason": "MEDIAPIPE_UNAVAILABLE",
            "candidates": [],
        })
        return result

    candidates = []
    for index, hand in enumerate(hands):
        metrics = _candidate_metrics(
            legacy_result,
            hand,
            int(mp_record["width"]),
            int(mp_record["height"]),
        )
        passed, reasons = _passes(metrics, args)
        candidates.append({
            "hand_index": index,
            "handedness_diagnostic_only": hand.get("handedness"),
            "handedness_score_diagnostic_only": hand.get("handedness_score"),
            "passed": passed,
            "reject_reasons": reasons,
            "agreement_score": _agreement_score(metrics),
            **metrics,
        })

    valid = sorted(
        (candidate for candidate in candidates if candidate["passed"]),
        key=lambda candidate: candidate["agreement_score"],
    )
    if not valid:
        result.update({
            "decision": "REJECT_MEDIAPIPE_IDENTITY_DISAGREEMENT",
            "advisory_axis_allowed": False,
            "selected_mediapipe_hand": None,
            "reason": "NO_MEDIAPIPE_INDEX_CHAIN_AGREES_WITH_BASELINE",
            "candidates": candidates,
        })
        return result

    best = valid[0]
    if len(valid) > 1:
        second = valid[1]
        scale = max(best["legacy_scale_px"], 1.0)
        tip_sep_norm = _dist(_v(best["mp_index_tip"]), _v(second["mp_index_tip"])) / scale
        score_gap = second["agreement_score"] - best["agreement_score"]
        if (
            tip_sep_norm > args.max_multi_hand_tip_separation_norm
            and score_gap < args.min_multi_hand_score_gap
        ):
            result.update({
                "decision": "REJECT_MEDIAPIPE_AMBIGUOUS",
                "advisory_axis_allowed": False,
                "selected_mediapipe_hand": None,
                "reason": "MULTIPLE_AGREEING_HANDS_DISAGREE_SPATIALLY",
                "candidates": candidates,
            })
            return result

    result.update({
        "decision": "ADVISORY_AXIS_ACCEPT",
        "advisory_axis_allowed": True,
        "selected_mediapipe_hand": best["hand_index"],
        "selected_mediapipe_axis": best["mp_index_axis"],
        "reason": "MEDIAPIPE_INDEX_CHAIN_AGREES_WITH_BASELINE",
        "candidates": candidates,
    })
    return result


def _mp_record_for_image(path: Path, landmarker: Any) -> dict[str, Any]:
    import cv2
    import mediapipe as mp

    image_bgr = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if image_bgr is None:
        return {
            "input": str(path),
            "status": "IMAGE_READ_ERROR",
            "width": 0,
            "height": 0,
            "hand_count": 0,
            "hands": [],
        }
    height, width = image_bgr.shape[:2]
    rgb = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
    raw = landmarker.detect(mp_image)

    landmark_sets = list(getattr(raw, "hand_landmarks", []) or [])
    handedness_sets = list(getattr(raw, "handedness", []) or [])
    world_sets = list(getattr(raw, "hand_world_landmarks", []) or [])
    hands = []
    for idx, landmarks in enumerate(landmark_sets):
        categories = handedness_sets[idx] if idx < len(handedness_sets) else []
        label, score = mpbench._category_fields(categories[0]) if categories else ("UNKNOWN", 0.0)
        lm_records, points = mpbench.serialize_landmarks(landmarks, width, height)
        hands.append({
            "hand_index": idx,
            "handedness": label,
            "handedness_score": score,
            "landmarks": lm_records,
            "world_landmarks_diagnostic_only": mpbench.serialize_world_landmarks(
                world_sets[idx] if idx < len(world_sets) else None
            ),
            **mpbench.index_geometry(points),
        })
    return {
        "input": str(path),
        "status": "HAND_DETECTED" if hands else "NO_HAND",
        "width": width,
        "height": height,
        "hand_count": len(hands),
        "hands": hands,
    }


def _draw_fusion_overlay(
    path: Path,
    legacy_result: dict[str, Any],
    mp_record: dict[str, Any],
    fused: dict[str, Any],
    out: Path,
) -> None:
    import cv2

    image = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if image is None:
        return

    lchain = _legacy_chain(legacy_result)
    if lchain:
        points = [(int(round(point.x)), int(round(point.y))) for point in lchain]
        for a, b in zip(points[:-1], points[1:]):
            cv2.line(image, a, b, (0, 220, 0), 2, cv2.LINE_AA)
        for point in points:
            cv2.circle(image, point, 4, (0, 220, 0), -1, cv2.LINE_AA)

    guided = legacy_result.get("guided_tip")
    if guided is not None:
        point = (int(round(guided[0])), int(round(guided[1])))
        cv2.drawMarker(image, point, (0, 255, 255), cv2.MARKER_CROSS, 20, 2, cv2.LINE_AA)

    selected = fused.get("selected_mediapipe_hand")
    for idx, hand in enumerate(mp_record.get("hands") or []):
        chain = _mp_chain(hand, int(mp_record["width"]), int(mp_record["height"]))
        points = [(int(round(point.x)), int(round(point.y))) for point in chain]
        color = (255, 0, 255) if idx == selected else (170, 100, 170)
        for a, b in zip(points[:-1], points[1:]):
            cv2.line(image, a, b, color, 2, cv2.LINE_AA)
        for point in points:
            cv2.circle(image, point, 3, color, -1, cv2.LINE_AA)

    decision = fused.get("decision", "UNKNOWN")
    cv2.putText(
        image,
        f"2B.10M.1 {decision}",
        (10, 24),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.55,
        (255, 255, 255),
        2,
        cv2.LINE_AA,
    )
    cv2.putText(
        image,
        "GREEN=2B.9B.1 baseline | MAGENTA=MediaPipe advisory | YELLOW=baseline distal",
        (10, 46),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.40,
        (255, 255, 255),
        1,
        cv2.LINE_AA,
    )
    cv2.putText(
        image,
        "DIAGNOSTIC ONLY | MediaPipe never publishes | TouchPlus stereo/Q remains metric authority",
        (10, image.shape[0] - 12),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.38,
        (255, 255, 255),
        1,
        cv2.LINE_AA,
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(out), image):
        raise RuntimeError(f"failed to write overlay: {out}")


def _write_outputs(records: list[dict[str, Any]], args: argparse.Namespace) -> None:
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    counts: dict[str, int] = {}
    for record in records:
        key = record.get("fusion", {}).get("decision", "ERROR")
        counts[key] = counts.get(key, 0) + 1

    summary = {
        "schema": SCHEMA,
        "phase": "2B.10M.1",
        "scope": "OFFLINE_SHADOW_ONLY",
        "baseline": BASELINE_NAME,
        "input": str(args.input.resolve()),
        "background": str(args.background.resolve()),
        "legacy_assets": str(args.legacy_assets.resolve()),
        "mediapipe_model": str(args.mediapipe_model.resolve()),
        "thresholds": {
            "min_axis_cos": args.min_axis_cos,
            "max_axis_angle_deg_equivalent": math.degrees(
                math.acos(max(-1.0, min(1.0, args.min_axis_cos)))
            ),
            "max_chain_rms_norm": args.max_chain_rms_norm,
            "max_model_tip_distance_norm": args.max_model_tip_distance_norm,
            "max_guided_lateral_norm": args.max_guided_lateral_norm,
            "min_guided_along_norm": args.min_guided_along_norm,
            "max_guided_along_norm": args.max_guided_along_norm,
            "max_multi_hand_tip_separation_norm": args.max_multi_hand_tip_separation_norm,
            "min_multi_hand_score_gap": args.min_multi_hand_score_gap,
        },
        "counts": counts,
        "safety": {
            "mediapipe_may_publish_fingertip": False,
            "mediapipe_may_rescue_baseline_reject": False,
            "mediapipe_z_used": False,
            "runtime_modified": False,
            "stereo_q_modified": False,
            "surface_frame_modified": False,
            "phase2c_modified": False,
            "os_injection": "DISABLED",
        },
        "records": records,
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    fields = [
        "image",
        "baseline_status",
        "mediapipe_status",
        "mediapipe_hands",
        "decision",
        "selected_hand",
        "axis_allowed",
        "reason",
        "selected_axis_angle_deg",
        "selected_chain_rms_norm",
        "selected_model_tip_distance_norm",
        "selected_lateral_norm",
        "selected_along_norm",
        "annotation",
    ]
    with (output / "summary.csv").open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for record in records:
            fusion = record["fusion"]
            selected = fusion.get("selected_mediapipe_hand")
            candidate = None
            if selected is not None:
                candidate = next(
                    (
                        item
                        for item in fusion.get("candidates") or []
                        if item.get("hand_index") == selected
                    ),
                    None,
                )
            writer.writerow({
                "image": record["image"],
                "baseline_status": fusion.get("baseline_guided_status"),
                "mediapipe_status": fusion.get("mediapipe_status"),
                "mediapipe_hands": fusion.get("mediapipe_hand_count"),
                "decision": fusion.get("decision"),
                "selected_hand": selected if selected is not None else "",
                "axis_allowed": fusion.get("advisory_axis_allowed"),
                "reason": fusion.get("reason"),
                "selected_axis_angle_deg": candidate.get("axis_angle_deg") if candidate else "",
                "selected_chain_rms_norm": candidate.get("chain_rms_norm") if candidate else "",
                "selected_model_tip_distance_norm": candidate.get("model_tip_distance_norm") if candidate else "",
                "selected_lateral_norm": candidate.get("guided_tip_lateral_norm") if candidate else "",
                "selected_along_norm": candidate.get("guided_tip_along_norm") if candidate else "",
                "annotation": record.get("annotation"),
            })


def run_self_test() -> int:
    base_result = {
        "guided_status": "GUIDED_DISTAL",
        "guided_tip": [160.0, 50.0],
        "distal_axis_valid": True,
        "distal_axis": [1.0, 0.0],
        "distal_scale_px": 20.0,
        "index_mcp": [70.0, 50.0],
        "index_pip": [95.0, 50.0],
        "index_dip": [120.0, 50.0],
        "index_tip_model": [140.0, 50.0],
        "reason": "ok",
    }
    args = SimpleNamespace(
        min_axis_cos=0.80,
        max_chain_rms_norm=1.25,
        max_model_tip_distance_norm=1.30,
        max_guided_lateral_norm=0.90,
        min_guided_along_norm=-4.0,
        max_guided_along_norm=0.60,
        max_multi_hand_tip_separation_norm=1.0,
        min_multi_hand_score_gap=0.20,
    )

    def hand(points: list[tuple[float, float]]) -> dict[str, Any]:
        landmarks = []
        for idx in range(21):
            x, y = points[idx] if idx < len(points) else (0.0, 0.0)
            landmarks.append({"x_norm": x / 639.0, "y_norm": y / 479.0})
        return {
            "landmarks": landmarks,
            "handedness": "Left",
            "handedness_score": 0.99,
        }

    points = [(0.0, 0.0)] * 21
    for idx, point in zip(INDEX_CHAIN, [(72, 51), (97, 51), (120, 51), (142, 51)]):
        points[idx] = point
    good_mp = {
        "status": "HAND_DETECTED",
        "width": 640,
        "height": 480,
        "hand_count": 1,
        "hands": [hand(points)],
    }
    good = fuse_one(base_result, good_mp, args)
    assert good["decision"] == "ADVISORY_AXIS_ACCEPT", good

    wrong_points = [(0.0, 0.0)] * 21
    for idx, point in zip(INDEX_CHAIN, [(80, 75), (92, 90), (105, 105), (120, 120)]):
        wrong_points[idx] = point
    wrong_mp = {
        "status": "HAND_DETECTED",
        "width": 640,
        "height": 480,
        "hand_count": 1,
        "hands": [hand(wrong_points)],
    }
    wrong = fuse_one(base_result, wrong_mp, args)
    assert wrong["decision"] == "REJECT_MEDIAPIPE_IDENTITY_DISAGREEMENT", wrong

    unavailable = fuse_one(
        base_result,
        {"status": "NO_HAND", "width": 640, "height": 480, "hand_count": 0, "hands": []},
        args,
    )
    assert unavailable["decision"] == "KEEP_BASELINE_MEDIAPIPE_UNAVAILABLE", unavailable

    rejected_base = dict(base_result)
    rejected_base.update({"guided_status": "GUIDED_REJECTED", "guided_tip": None})
    no_rescue = fuse_one(rejected_base, good_mp, args)
    assert no_rescue["decision"] == "BASELINE_REJECT_NO_MEDIAPIPE_RESCUE", no_rescue

    print("2B.10M.1 SELF_TEST: PASS")
    print("  correct proximal/same-axis advisory : ACCEPT")
    print("  wrong-finger synthetic chain        : REJECT")
    print("  MediaPipe unavailable               : KEEP_BASELINE")
    print("  baseline reject                     : NO_RESCUE")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="TouchPlus 2B.10M.1 conservative MediaPipe advisory fusion benchmark"
    )
    parser.add_argument("--input", type=Path)
    parser.add_argument("--background", type=Path)
    parser.add_argument("--legacy-assets", type=Path)
    parser.add_argument("--mediapipe-model", type=Path)
    parser.add_argument("--output", type=Path, default=Path("mediapipe-fusion-output"))
    parser.add_argument("--num-hands", type=int, default=2)
    parser.add_argument("--min-detection", type=float, default=0.5)
    parser.add_argument("--min-presence", type=float, default=0.5)
    parser.add_argument("--min-axis-cos", type=float, default=0.80)
    parser.add_argument("--max-chain-rms-norm", type=float, default=1.25)
    parser.add_argument("--max-model-tip-distance-norm", type=float, default=1.30)
    parser.add_argument("--max-guided-lateral-norm", type=float, default=0.90)
    parser.add_argument("--min-guided-along-norm", type=float, default=-4.0)
    parser.add_argument("--max-guided-along-norm", type=float, default=0.60)
    parser.add_argument("--max-multi-hand-tip-separation-norm", type=float, default=1.0)
    parser.add_argument("--min-multi-hand-score-gap", type=float, default=0.20)
    parser.add_argument("--self-test", action="store_true")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()

    for name in ("input", "background", "legacy_assets", "mediapipe_model"):
        value = getattr(args, name)
        if value is None:
            parser.error(f"--{name.replace('_', '-')} is required")
    if not args.input.exists():
        parser.error(f"input not found: {args.input}")
    if not args.background.is_file():
        parser.error(f"background not found: {args.background}")
    if not args.legacy_assets.is_dir():
        parser.error(f"legacy assets not found: {args.legacy_assets}")
    if not args.mediapipe_model.is_file():
        parser.error(f"MediaPipe model not found: {args.mediapipe_model}")

    images = mpbench.discover_images(args.input.resolve(), "left")
    background = legacy.base._load_image(args.background.resolve())
    palm_detector, handpose_model = legacy.base._load_zoo(args.legacy_assets.resolve())

    mp_args = SimpleNamespace(
        num_hands=args.num_hands,
        min_detection=args.min_detection,
        min_presence=args.min_presence,
    )
    output = args.output.resolve()
    legacy_output = output / "legacy-2b9b1"
    records: list[dict[str, Any]] = []

    print("TouchPlus Phase 2B.10M.1 | conservative MediaPipe advisory fusion | OFFLINE SHADOW ONLY")
    print(f"Input:      {args.input.resolve()}")
    print(f"Background: {args.background.resolve()}")
    print(f"Baseline:   {BASELINE_NAME}")
    print("Policy: MediaPipe may advise an axis only after agreement; it can never publish or rescue a tip")
    print("Metric: TouchPlus stereo/Q remains sole authority; MediaPipe Z ignored")

    with mpbench.create_landmarker(args.mediapipe_model.resolve(), mp_args) as landmarker:
        for index, path in enumerate(images, 1):
            try:
                legacy_result = legacy.evaluate_image(
                    path,
                    legacy_output,
                    args.legacy_assets.resolve(),
                    background=background,
                    palm_detector=palm_detector,
                    handpose_model=handpose_model,
                )
                mp_record = _mp_record_for_image(path, landmarker)
                fusion = fuse_one(legacy_result, mp_record, args)
                annotation = output / "annotations" / f"{path.stem}_fused.png"
                _draw_fusion_overlay(path, legacy_result, mp_record, fusion, annotation)
                record = {
                    "image": str(path),
                    "legacy": legacy_result,
                    "mediapipe": mp_record,
                    "fusion": fusion,
                    "annotation": str(annotation),
                }
            except Exception as exc:
                record = {
                    "image": str(path),
                    "fusion": {
                        "decision": "ERROR",
                        "reason": str(exc),
                        "mediapipe_can_publish_fingertip": False,
                        "mediapipe_can_rescue_baseline_reject": False,
                    },
                }
            records.append(record)
            print(
                f"[{index:02d}/{len(images):02d}] {path.name}: "
                f"{record['fusion'].get('decision')} | {record['fusion'].get('reason')}"
            )

    _write_outputs(records, args)
    print(f"Summary:  {output / 'summary.json'}")
    print(f"CSV:      {output / 'summary.csv'}")
    print(f"Overlays: {output / 'annotations'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
