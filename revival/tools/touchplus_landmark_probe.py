#!/usr/bin/env python3
"""TouchPlus Revival Phase 2B.9A landmark oracle evaluation.

This is an offline/sidecar evaluation tool. It never provides metric Z.
It consumes local LEFT-eye grayscale captures plus optional geometry sidecars
written by the Win32 tracker and compares OpenCV Zoo MediaPipe 2D landmarks
against the existing geometry identity candidate.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from dataclasses import dataclass
from typing import Any, Optional

INDEX_TIP = 8
INDEX_DIP = 7
INDEX_PIP = 6
INDEX_MCP = 5
WRIST = 0


@dataclass
class OracleDecision:
    status: str
    distance_px: Optional[float]
    threshold_px: Optional[float]
    reason: str


def _read_geometry(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _geometry_tip(geometry: dict[str, Any]) -> Optional[tuple[float, float]]:
    value = geometry.get("tip_pixel")
    if isinstance(value, list) and len(value) == 2:
        try:
            x, y = float(value[0]), float(value[1])
            if x >= 0 and y >= 0:
                return x, y
        except (TypeError, ValueError):
            pass
    try:
        x = float(geometry.get("pixel_x", -1))
        y = float(geometry.get("pixel_y", -1))
        if x >= 0 and y >= 0:
            return x, y
    except (TypeError, ValueError):
        pass
    return None


def _palm_radius_full_px(geometry: dict[str, Any]) -> Optional[float]:
    for key in ("palm_radius_full_px", "palm_radius_px"):
        try:
            value = float(geometry.get(key, 0))
            if value > 0:
                return value
        except (TypeError, ValueError):
            pass
    try:
        grid_radius = float(geometry.get("palm_radius_grid", 0))
        scale = float(geometry.get("depth_scale", 2))
        if grid_radius > 0 and scale > 0:
            return grid_radius * scale
    except (TypeError, ValueError):
        pass
    return None


def _is_geometry_locked(geometry: dict[str, Any]) -> bool:
    state = str(geometry.get("identity_state", "")).upper()
    confidence = str(geometry.get("identity_confidence", "")).upper()
    return state == "LOCKED" and confidence in {"MEDIUM", "HIGH"}


def _agreement_threshold(geometry: dict[str, Any]) -> float:
    radius = _palm_radius_full_px(geometry)
    if radius is None:
        return 28.0
    return max(18.0, min(48.0, radius * 0.55))


def arbitrate_landmark(
    geometry: Optional[dict[str, Any]],
    landmark_tip: Optional[tuple[float, float]],
    *,
    landmark_confidence: float,
    index_extended: bool,
) -> OracleDecision:
    """Diagnostic policy only; it does not change runtime identity or metric Z."""
    if landmark_tip is None or landmark_confidence < 0.80:
        return OracleDecision(
            "ORACLE_UNAVAILABLE", None, None,
            "no reliable landmark index tip"
        )
    if not index_extended:
        return OracleDecision(
            "ORACLE_NON_INDEX_POSE", None, None,
            "landmarks do not support one extended index"
        )
    if not geometry:
        return OracleDecision(
            "LANDMARK_ONLY_DIAGNOSTIC", None, None,
            "no geometry sidecar supplied; landmark remains diagnostic only"
        )

    tip = _geometry_tip(geometry)
    if tip is None:
        return OracleDecision(
            "LANDMARK_ONLY_DIAGNOSTIC", None, None,
            "geometry has no 2D candidate; landmark alone cannot publish identity"
        )

    threshold = _agreement_threshold(geometry)
    distance = math.hypot(landmark_tip[0] - tip[0], landmark_tip[1] - tip[1])

    if distance <= threshold:
        if _is_geometry_locked(geometry):
            return OracleDecision(
                "AGREE_LOCKED", distance, threshold,
                "locked geometry and landmark index tip agree"
            )
        return OracleDecision(
            "AGREE_DIAGNOSTIC", distance, threshold,
            "geometry/landmark agree but geometry is not locked"
        )

    return OracleDecision(
        "DISAGREE_VETO", distance, threshold,
        "geometry and landmark index tip disagree; future live policy must prefer UNKNOWN"
    )


def _index_extended_xy(landmarks) -> bool:
    """Conservative 2D-only extension check. Landmarks are (21,3)."""
    import numpy as np

    wrist = landmarks[WRIST, :2]
    mcp = landmarks[INDEX_MCP, :2]
    pip = landmarks[INDEX_PIP, :2]
    dip = landmarks[INDEX_DIP, :2]
    tip = landmarks[INDEX_TIP, :2]

    def dist(a, b):
        return float(np.linalg.norm(a - b))

    chain = dist(mcp, pip) + dist(pip, dip) + dist(dip, tip)
    direct = dist(mcp, tip)
    wrist_tip = dist(wrist, tip)
    wrist_pip = dist(wrist, pip)

    if chain < 18.0 or direct < 12.0:
        return False
    straightness = direct / max(chain, 1e-6)
    return straightness >= 0.72 and wrist_tip >= wrist_pip + 6.0


def _load_zoo(assets: pathlib.Path):
    modules = assets / "modules"
    models = assets / "models"
    required = [
        modules / "mp_palmdet.py",
        modules / "mp_handpose.py",
        models / "palm_detection_mediapipe_2023feb.onnx",
        models / "handpose_estimation_mediapipe_2023feb.onnx",
    ]
    missing = [str(p) for p in required if not p.exists()]
    if missing:
        raise RuntimeError(
            "Landmark assets missing. Run setup-touchplus-landmark-probe.ps1 first.\n"
            + "\n".join(missing)
        )

    sys.path.insert(0, str(modules))
    from mp_palmdet import MPPalmDet  # type: ignore
    from mp_handpose import MPHandPose  # type: ignore
    return (
        MPPalmDet(str(models / "palm_detection_mediapipe_2023feb.onnx"),
                  scoreThreshold=0.50),
        MPHandPose(str(models / "handpose_estimation_mediapipe_2023feb.onnx"),
                   confThreshold=0.80),
    )


def _best_hand(image, palm_detector, handpose_model):
    palms = palm_detector.infer(image)
    if palms is None or len(palms) == 0:
        return None, None

    order = sorted(range(len(palms)), key=lambda i: float(palms[i][-1]), reverse=True)
    best = None
    best_conf = -1.0
    best_palm = None
    for i in order[:4]:
        palm = palms[i]
        hand = handpose_model.infer(image, palm)
        if hand is None:
            continue
        conf = float(hand[-1])
        if conf > best_conf:
            best_conf = conf
            best = hand
            best_palm = palm
    return best_palm, best


def _draw_result(image, landmarks, geometry, decision, output_path):
    import cv2 as cv

    display = image.copy()
    tip = tuple(int(round(v)) for v in landmarks[INDEX_TIP, :2])
    cv.circle(display, tip, 7, (0, 255, 0), 2)
    cv.putText(display, "LANDMARK INDEX_TIP", (max(5, tip[0] + 8), max(18, tip[1] - 8)),
               cv.FONT_HERSHEY_SIMPLEX, 0.45, (0, 255, 0), 1, cv.LINE_AA)

    if geometry:
        gtip = _geometry_tip(geometry)
        if gtip is not None:
            gp = (int(round(gtip[0])), int(round(gtip[1])))
            cv.drawMarker(display, gp, (255, 255, 255), cv.MARKER_CROSS, 16, 2)
            cv.line(display, gp, tip, (255, 255, 255), 1)

    cv.putText(display, decision.status, (8, 22),
               cv.FONT_HERSHEY_SIMPLEX, 0.60, (255, 255, 255), 2, cv.LINE_AA)
    cv.imwrite(str(output_path), display)


def _find_geometry_for_image(image_path: pathlib.Path) -> Optional[pathlib.Path]:
    stem = image_path.stem
    candidates = [
        image_path.with_name(stem.replace("-left", "-geometry") + ".json"),
        image_path.with_suffix(".json"),
    ]
    for path in candidates:
        if path.exists():
            return path
    return None


def evaluate_image(image_path: pathlib.Path, output_dir: pathlib.Path, assets: pathlib.Path,
                   palm_detector=None, handpose_model=None) -> dict[str, Any]:
    import cv2 as cv

    image = cv.imread(str(image_path), cv.IMREAD_UNCHANGED)
    if image is None:
        raise RuntimeError(f"Cannot read image: {image_path}")
    if image.ndim == 2:
        image = cv.cvtColor(image, cv.COLOR_GRAY2BGR)
    elif image.shape[2] == 4:
        image = cv.cvtColor(image, cv.COLOR_BGRA2BGR)

    if palm_detector is None or handpose_model is None:
        palm_detector, handpose_model = _load_zoo(assets)

    palm, hand = _best_hand(image, palm_detector, handpose_model)
    geometry_path = _find_geometry_for_image(image_path)
    geometry = _read_geometry(geometry_path) if geometry_path else None

    result: dict[str, Any] = {
        "image": str(image_path),
        "geometry_sidecar": str(geometry_path) if geometry_path else None,
        "metric_z_source": "TOUCHPLUS_STEREO_ONLY",
    }

    if hand is None:
        decision = arbitrate_landmark(
            geometry, None, landmark_confidence=0.0, index_extended=False
        )
        result.update({
            "landmark_found": False,
            "oracle_status": decision.status,
            "reason": decision.reason,
        })
        return result

    landmarks = hand[4:67].reshape(21, 3)
    conf = float(hand[-1])
    extended = _index_extended_xy(landmarks)
    tip = (float(landmarks[INDEX_TIP, 0]), float(landmarks[INDEX_TIP, 1]))
    decision = arbitrate_landmark(
        geometry, tip, landmark_confidence=conf, index_extended=extended
    )

    result.update({
        "landmark_found": True,
        "hand_confidence": conf,
        "index_extended_2d": extended,
        "index_tip": [tip[0], tip[1]],
        "handedness_score": float(hand[-2]),
        "oracle_status": decision.status,
        "distance_to_geometry_px": decision.distance_px,
        "agreement_threshold_px": decision.threshold_px,
        "reason": decision.reason,
    })

    output_dir.mkdir(parents=True, exist_ok=True)
    annotated = output_dir / f"{image_path.stem}-landmark.png"
    _draw_result(image, landmarks, geometry, decision, annotated)
    result["annotated_image"] = str(annotated)
    return result


def _collect_inputs(path: pathlib.Path) -> list[pathlib.Path]:
    if path.is_file():
        return [path]
    patterns = ("*-left.pgm", "*-left.png", "*-left.jpg", "*-left.jpeg",
                "*.pgm", "*.png", "*.jpg", "*.jpeg")
    seen = set()
    result = []
    for pattern in patterns:
        for item in sorted(path.glob(pattern)):
            if item not in seen:
                seen.add(item)
                result.append(item)
    return result


def run_self_test() -> int:
    locked = {
        "identity_state": "LOCKED",
        "identity_confidence": "HIGH",
        "tip_pixel": [100, 100],
        "palm_radius_grid": 30.0,
        "depth_scale": 2,
    }
    agree = arbitrate_landmark(
        locked, (112, 108), landmark_confidence=0.96, index_extended=True
    )
    disagree = arbitrate_landmark(
        locked, (190, 40), landmark_confidence=0.96, index_extended=True
    )
    unstable = dict(locked)
    unstable["identity_state"] = "ACQUIRING"
    acquiring = arbitrate_landmark(
        unstable, (108, 103), landmark_confidence=0.96, index_extended=True
    )
    unavailable = arbitrate_landmark(
        locked, None, landmark_confidence=0.0, index_extended=False
    )

    ok = (
        agree.status == "AGREE_LOCKED"
        and disagree.status == "DISAGREE_VETO"
        and acquiring.status == "AGREE_DIAGNOSTIC"
        and unavailable.status == "ORACLE_UNAVAILABLE"
    )
    print("Phase 2B.9A landmark arbitration self-test")
    print(f"  agree locked       : {agree.status}")
    print(f"  disagreement veto  : {disagree.status}")
    print(f"  acquiring diag     : {acquiring.status}")
    print(f"  unavailable        : {unavailable.status}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="TouchPlus Phase 2B.9A offline 2D landmark oracle evaluator"
    )
    parser.add_argument("--input", type=pathlib.Path,
                        help="LEFT-eye image or directory of local landmark captures")
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("landmark-results"))
    parser.add_argument("--assets", type=pathlib.Path,
                        default=pathlib.Path("landmark-assets"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()
    if args.input is None:
        parser.error("--input is required unless --self-test is used")

    inputs = _collect_inputs(args.input)
    if not inputs:
        raise RuntimeError(f"No input images found under {args.input}")

    palm_detector, handpose_model = _load_zoo(args.assets)
    results = []
    for image_path in inputs:
        try:
            result = evaluate_image(
                image_path, args.output, args.assets,
                palm_detector=palm_detector, handpose_model=handpose_model
            )
        except Exception as exc:
            result = {
                "image": str(image_path),
                "oracle_status": "ERROR",
                "reason": str(exc),
                "metric_z_source": "TOUCHPLUS_STEREO_ONLY",
            }
        results.append(result)
        print(f"{image_path.name}: {result.get('oracle_status')} "
              f"tip={result.get('index_tip')} "
              f"delta={result.get('distance_to_geometry_px')}")

    args.output.mkdir(parents=True, exist_ok=True)
    summary_path = args.output / "landmark-summary.json"
    summary_path.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(f"Summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
