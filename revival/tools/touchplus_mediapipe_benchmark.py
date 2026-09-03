#!/usr/bin/env python3
"""TouchPlus Phase 2B.10M — offline Google MediaPipe Hand Landmarker benchmark.

Diagnostic-only tool. It never produces TouchPlus metric depth, surface XYZ/H,
contact semantics, or OS input. MediaPipe z/world landmarks are recorded for
inspection only and are explicitly excluded from TouchPlus metric decisions.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.metadata
import json
import math
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

SCHEMA = "touchplus-mediapipe-hand-landmarker-benchmark-v1"
LANDMARK_NAMES = [
    "WRIST",
    "THUMB_CMC", "THUMB_MCP", "THUMB_IP", "THUMB_TIP",
    "INDEX_MCP", "INDEX_PIP", "INDEX_DIP", "INDEX_TIP",
    "MIDDLE_MCP", "MIDDLE_PIP", "MIDDLE_DIP", "MIDDLE_TIP",
    "RING_MCP", "RING_PIP", "RING_DIP", "RING_TIP",
    "PINKY_MCP", "PINKY_PIP", "PINKY_DIP", "PINKY_TIP",
]
INDEX_CHAIN = (5, 6, 7, 8)
HAND_CONNECTIONS = (
    (0, 1), (1, 2), (2, 3), (3, 4),
    (0, 5), (5, 6), (6, 7), (7, 8),
    (5, 9), (9, 10), (10, 11), (11, 12),
    (9, 13), (13, 14), (14, 15), (15, 16),
    (13, 17), (0, 17), (17, 18), (18, 19), (19, 20),
)
IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}
LEFT_TOKEN_RE = re.compile(r"(^|[-_.\s])left($|[-_.\s])", re.IGNORECASE)


@dataclass(frozen=True)
class Point2:
    x: float
    y: float


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def pixel_from_normalized(x: float, y: float, width: int, height: int) -> tuple[int, int]:
    px = int(round(clamp(x, 0.0, 1.0) * max(0, width - 1)))
    py = int(round(clamp(y, 0.0, 1.0) * max(0, height - 1)))
    return px, py


def distance(a: Point2, b: Point2) -> float:
    return math.hypot(a.x - b.x, a.y - b.y)


def angle_deg(a: Point2, vertex: Point2, c: Point2) -> float:
    ux, uy = a.x - vertex.x, a.y - vertex.y
    vx, vy = c.x - vertex.x, c.y - vertex.y
    nu = math.hypot(ux, uy)
    nv = math.hypot(vx, vy)
    if nu < 1e-9 or nv < 1e-9:
        return 0.0
    cosine = clamp((ux * vx + uy * vy) / (nu * nv), -1.0, 1.0)
    return math.degrees(math.acos(cosine))


def index_geometry(points: Sequence[Point2]) -> dict[str, Any]:
    if len(points) != 21:
        raise ValueError(f"expected 21 landmarks, got {len(points)}")
    wrist = points[0]
    mcp, pip, dip, tip = (points[i] for i in INDEX_CHAIN)
    segment_length = distance(mcp, pip) + distance(pip, dip) + distance(dip, tip)
    direct_length = distance(mcp, tip)
    straightness = direct_length / segment_length if segment_length > 1e-9 else 0.0
    pip_angle = angle_deg(mcp, pip, dip)
    dip_angle = angle_deg(pip, dip, tip)
    wrist_pip = distance(wrist, pip)
    wrist_tip = distance(wrist, tip)
    axis_dx, axis_dy = tip.x - mcp.x, tip.y - mcp.y
    axis_norm = math.hypot(axis_dx, axis_dy)
    if axis_norm > 1e-9:
        axis_dx /= axis_norm
        axis_dy /= axis_norm
    likely_extended = (
        pip_angle >= 145.0
        and dip_angle >= 145.0
        and straightness >= 0.88
        and wrist_tip > wrist_pip
    )
    return {
        "index_mcp_to_tip_px": round(direct_length, 4),
        "index_chain_length_px": round(segment_length, 4),
        "index_straightness": round(straightness, 6),
        "index_pip_angle_deg": round(pip_angle, 3),
        "index_dip_angle_deg": round(dip_angle, 3),
        "index_axis_unit": {"dx": round(axis_dx, 6), "dy": round(axis_dy, 6)},
        "index_pose_diagnostic": "LIKELY_EXTENDED" if likely_extended else "UNCERTAIN_OR_BENT",
        "index_pose_is_authoritative": False,
    }


def is_left_image(path: Path) -> bool:
    candidates = [path.stem, path.parent.name]
    return any(LEFT_TOKEN_RE.search(text) for text in candidates)


def discover_images(root: Path, eye: str) -> list[Path]:
    if root.is_file():
        if root.suffix.lower() not in IMAGE_EXTENSIONS:
            raise ValueError(f"unsupported input image extension: {root.suffix}")
        images = [root]
    elif root.is_dir():
        images = sorted(
            p for p in root.rglob("*")
            if p.is_file() and p.suffix.lower() in IMAGE_EXTENSIONS
        )
    else:
        raise FileNotFoundError(f"input not found: {root}")
    if eye == "left":
        left = [p for p in images if is_left_image(p)]
        if not left and images:
            raise RuntimeError(
                "No LEFT-tagged images found. Rename files/parent folder to include LEFT, "
                "or rerun with --eye all only if the dataset truly contains LEFT images only."
            )
        images = left
    if not images:
        raise RuntimeError("no input images found")
    return images


def package_version(name: str) -> str:
    try:
        return importlib.metadata.version(name)
    except importlib.metadata.PackageNotFoundError:
        return "unknown"


def _category_fields(category: Any) -> tuple[str, float]:
    label = getattr(category, "category_name", None) or getattr(category, "display_name", None) or "UNKNOWN"
    score = float(getattr(category, "score", 0.0) or 0.0)
    return str(label), score


def serialize_landmarks(landmarks: Sequence[Any], width: int, height: int) -> tuple[list[dict[str, Any]], list[Point2]]:
    records: list[dict[str, Any]] = []
    pixels: list[Point2] = []
    for idx, landmark in enumerate(landmarks):
        x = float(landmark.x)
        y = float(landmark.y)
        z = float(landmark.z)
        px, py = pixel_from_normalized(x, y, width, height)
        pixels.append(Point2(float(px), float(py)))
        records.append({
            "id": idx,
            "name": LANDMARK_NAMES[idx] if idx < len(LANDMARK_NAMES) else f"LANDMARK_{idx}",
            "x_norm": x,
            "y_norm": y,
            "mediapipe_image_z_diagnostic_only": z,
            "x_px": px,
            "y_px": py,
        })
    return records, pixels


def serialize_world_landmarks(world_landmarks: Sequence[Any] | None) -> list[dict[str, Any]]:
    if not world_landmarks:
        return []
    records = []
    for idx, landmark in enumerate(world_landmarks):
        records.append({
            "id": idx,
            "name": LANDMARK_NAMES[idx] if idx < len(LANDMARK_NAMES) else f"LANDMARK_{idx}",
            "x_m_diagnostic_only": float(landmark.x),
            "y_m_diagnostic_only": float(landmark.y),
            "z_m_diagnostic_only": float(landmark.z),
        })
    return records


def choose_primary(hands: list[dict[str, Any]]) -> int | None:
    if not hands:
        return None
    return max(range(len(hands)), key=lambda idx: hands[idx]["handedness_score"])


def draw_overlay(image: Any, hands: list[dict[str, Any]], primary_idx: int | None) -> Any:
    import cv2

    canvas = image.copy()
    for hand_idx, hand in enumerate(hands):
        points = [(int(lm["x_px"]), int(lm["y_px"])) for lm in hand["landmarks"]]
        for a, b in HAND_CONNECTIONS:
            cv2.line(canvas, points[a], points[b], (190, 190, 190), 1, cv2.LINE_AA)
        for idx, point in enumerate(points):
            radius = 3 if idx not in INDEX_CHAIN else 4
            color = (220, 220, 220) if idx not in INDEX_CHAIN else (255, 0, 255)
            cv2.circle(canvas, point, radius, color, -1, cv2.LINE_AA)
        for a, b in zip(INDEX_CHAIN[:-1], INDEX_CHAIN[1:]):
            cv2.line(canvas, points[a], points[b], (255, 0, 255), 2, cv2.LINE_AA)
        tip = points[8]
        cv2.drawMarker(canvas, tip, (0, 255, 255), cv2.MARKER_CROSS, 18, 2, cv2.LINE_AA)
        prefix = "PRIMARY" if hand_idx == primary_idx else f"HAND {hand_idx}"
        text = f"{prefix} {hand['handedness']} score={hand['handedness_score']:.3f} | MP #8 INDEX_TIP"
        y = 24 + 22 * hand_idx
        cv2.putText(canvas, text, (10, y), cv2.FONT_HERSHEY_SIMPLEX, 0.53, (255, 255, 255), 1, cv2.LINE_AA)
    if not hands:
        cv2.putText(canvas, "NO_HAND", (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(
        canvas,
        "DIAGNOSTIC ONLY | MediaPipe Z ignored | TouchPlus stereo remains metric authority",
        (10, max(22, canvas.shape[0] - 12)),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.42,
        (255, 255, 255),
        1,
        cv2.LINE_AA,
    )
    return canvas


def process_one(path: Path, landmarker: Any, output_dir: Path, input_root: Path) -> dict[str, Any]:
    import cv2
    import mediapipe as mp

    image_bgr = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if image_bgr is None:
        return {"input": str(path), "status": "IMAGE_READ_ERROR", "hands": []}
    height, width = image_bgr.shape[:2]
    image_rgb = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=image_rgb)
    result = landmarker.detect(mp_image)

    landmarks_sets = list(getattr(result, "hand_landmarks", []) or [])
    handedness_sets = list(getattr(result, "handedness", []) or [])
    world_sets = list(getattr(result, "hand_world_landmarks", []) or [])

    hands: list[dict[str, Any]] = []
    for idx, landmarks in enumerate(landmarks_sets):
        categories = handedness_sets[idx] if idx < len(handedness_sets) else []
        label, score = _category_fields(categories[0]) if categories else ("UNKNOWN", 0.0)
        lm_records, points = serialize_landmarks(landmarks, width, height)
        world_records = serialize_world_landmarks(world_sets[idx] if idx < len(world_sets) else None)
        geometry = index_geometry(points)
        hands.append({
            "hand_index": idx,
            "handedness": label,
            "handedness_score": score,
            "landmarks": lm_records,
            "world_landmarks_diagnostic_only": world_records,
            "index_chain_ids": list(INDEX_CHAIN),
            "index_tip_pixel": {"x": lm_records[8]["x_px"], "y": lm_records[8]["y_px"]},
            **geometry,
        })

    primary_idx = choose_primary(hands)
    status = "HAND_DETECTED" if hands else "NO_HAND"
    try:
        relative = path.relative_to(input_root) if input_root.is_dir() else Path(path.name)
    except ValueError:
        relative = Path(path.name)
    safe_rel = Path(*[part.replace(":", "_") for part in relative.parts])
    annotation_path = output_dir / "annotations" / safe_rel.parent / f"{safe_rel.stem}_mediapipe.png"
    per_image_path = output_dir / "per-image" / safe_rel.parent / f"{safe_rel.stem}.json"
    annotation_path.parent.mkdir(parents=True, exist_ok=True)
    per_image_path.parent.mkdir(parents=True, exist_ok=True)
    overlay = draw_overlay(image_bgr, hands, primary_idx)
    if not cv2.imwrite(str(annotation_path), overlay):
        raise RuntimeError(f"failed to write annotation: {annotation_path}")

    record = {
        "schema": SCHEMA,
        "input": str(path),
        "width": width,
        "height": height,
        "status": status,
        "hand_count": len(hands),
        "primary_hand_index": primary_idx,
        "annotation": str(annotation_path),
        "hands": hands,
        "metric_depth_policy": {
            "mediapipe_image_z_used": False,
            "mediapipe_world_xyz_used": False,
            "touchplus_stereo_q_surface_remain_authoritative": True,
        },
    }
    per_image_path.write_text(json.dumps(record, indent=2), encoding="utf-8")
    return record


def write_summary(records: list[dict[str, Any]], output_dir: Path, model: Path, args: argparse.Namespace) -> None:
    detected = sum(1 for r in records if r.get("status") == "HAND_DETECTED")
    no_hand = sum(1 for r in records if r.get("status") == "NO_HAND")
    primary_extended = 0
    for record in records:
        idx = record.get("primary_hand_index")
        if idx is not None and record["hands"][idx]["index_pose_diagnostic"] == "LIKELY_EXTENDED":
            primary_extended += 1
    summary = {
        "schema": SCHEMA,
        "generated_utc": utc_now(),
        "tool": "touchplus_mediapipe_benchmark.py",
        "mediapipe_package_version": package_version("mediapipe"),
        "opencv_package_version": package_version("opencv-python"),
        "model": {"path": str(model), "sha256": sha256_file(model)},
        "input": str(args.input),
        "eye_policy": args.eye,
        "thresholds": {
            "min_hand_detection_confidence": args.min_detection,
            "min_hand_presence_confidence": args.min_presence,
            "num_hands": args.num_hands,
        },
        "counts": {
            "images": len(records),
            "hand_detected": detected,
            "no_hand": no_hand,
            "image_read_error": sum(1 for r in records if r.get("status") == "IMAGE_READ_ERROR"),
            "primary_index_likely_extended_diagnostic_only": primary_extended,
        },
        "safety": {
            "diagnostic_only": True,
            "runtime_modified": False,
            "stereo_q_modified": False,
            "surface_frame_modified": False,
            "phase2c_modified": False,
            "os_injection": "DISABLED",
            "mediapipe_z_is_metric_authority": False,
        },
        "manual_review_classes": [
            "TIP_GOOD",
            "TIP_PROXIMAL_BUT_AXIS_GOOD",
            "WRONG_FINGER",
            "UNAVAILABLE",
        ],
        "records": records,
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

    with (output_dir / "summary.csv").open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=[
            "input", "status", "hand_count", "primary_handedness", "primary_handedness_score",
            "index_tip_x", "index_tip_y", "index_pose_diagnostic", "index_straightness",
            "pip_angle_deg", "dip_angle_deg", "annotation",
        ])
        writer.writeheader()
        for record in records:
            idx = record.get("primary_hand_index")
            hand = record["hands"][idx] if idx is not None else None
            writer.writerow({
                "input": record.get("input", ""),
                "status": record.get("status", ""),
                "hand_count": record.get("hand_count", 0),
                "primary_handedness": hand["handedness"] if hand else "",
                "primary_handedness_score": f"{hand['handedness_score']:.6f}" if hand else "",
                "index_tip_x": hand["index_tip_pixel"]["x"] if hand else "",
                "index_tip_y": hand["index_tip_pixel"]["y"] if hand else "",
                "index_pose_diagnostic": hand["index_pose_diagnostic"] if hand else "",
                "index_straightness": hand["index_straightness"] if hand else "",
                "pip_angle_deg": hand["index_pip_angle_deg"] if hand else "",
                "dip_angle_deg": hand["index_dip_angle_deg"] if hand else "",
                "annotation": record.get("annotation", ""),
            })


def run_self_test() -> int:
    straight = [Point2(0, 0) for _ in range(21)]
    straight[0] = Point2(10, 100)
    straight[5] = Point2(20, 70)
    straight[6] = Point2(20, 50)
    straight[7] = Point2(20, 30)
    straight[8] = Point2(20, 10)
    geometry = index_geometry(straight)
    assert geometry["index_pose_diagnostic"] == "LIKELY_EXTENDED", geometry
    bent = list(straight)
    bent[7] = Point2(35, 55)
    bent[8] = Point2(45, 65)
    geometry_bent = index_geometry(bent)
    assert geometry_bent["index_pose_diagnostic"] == "UNCERTAIN_OR_BENT", geometry_bent
    assert pixel_from_normalized(-1, 2, 640, 480) == (0, 479)
    assert is_left_image(Path("pair-001-LEFT.png"))
    assert not is_left_image(Path("pair-001-RIGHT.png"))
    print("SELF_TEST: PASS")
    return 0


def create_landmarker(model: Path, args: argparse.Namespace) -> Any:
    import mediapipe as mp

    BaseOptions = mp.tasks.BaseOptions
    HandLandmarker = mp.tasks.vision.HandLandmarker
    HandLandmarkerOptions = mp.tasks.vision.HandLandmarkerOptions
    RunningMode = mp.tasks.vision.RunningMode
    options = HandLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=str(model.resolve())),
        running_mode=RunningMode.IMAGE,
        num_hands=args.num_hands,
        min_hand_detection_confidence=args.min_detection,
        min_hand_presence_confidence=args.min_presence,
    )
    return HandLandmarker.create_from_options(options)


def run_model_smoke(model: Path, args: argparse.Namespace) -> int:
    import numpy as np
    import mediapipe as mp

    with create_landmarker(model, args) as landmarker:
        blank = np.zeros((256, 256, 3), dtype=np.uint8)
        image = mp.Image(image_format=mp.ImageFormat.SRGB, data=blank)
        result = landmarker.detect(image)
        count = len(getattr(result, "hand_landmarks", []) or [])
    print(f"MODEL_SMOKE: PASS | mediapipe={package_version('mediapipe')} | hands_on_blank={count}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Offline Google MediaPipe benchmark for TouchPlus LEFT images.")
    parser.add_argument("--input", type=Path, help="Input image or directory.")
    parser.add_argument("--output", type=Path, default=Path("mediapipe-benchmark-output"))
    parser.add_argument("--model", type=Path, help="Google Hand Landmarker .task model.")
    parser.add_argument("--eye", choices=("left", "all"), default="left", help="Default protects against accidentally benchmarking RIGHT images.")
    parser.add_argument("--num-hands", type=int, default=2)
    parser.add_argument("--min-detection", type=float, default=0.5)
    parser.add_argument("--min-presence", type=float, default=0.5)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--model-smoke", action="store_true", help="Load model and run one blank IMAGE inference, then exit.")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    if args.model is None:
        parser.error("--model is required unless --self-test is used")
    model = args.model.resolve()
    if not model.is_file():
        parser.error(f"model not found: {model}")
    if args.model_smoke:
        return run_model_smoke(model, args)
    if args.input is None:
        parser.error("--input is required")
    if not (1 <= args.num_hands <= 8):
        parser.error("--num-hands must be between 1 and 8")
    for name in ("min_detection", "min_presence"):
        value = getattr(args, name)
        if not 0.0 <= value <= 1.0:
            parser.error(f"--{name.replace('_', '-')} must be within [0,1]")

    input_path = args.input.resolve()
    output_dir = args.output.resolve()
    images = discover_images(input_path, args.eye)
    print("TouchPlus Phase 2B.10M | Google MediaPipe Hand Landmarker | DIAGNOSTIC ONLY")
    print(f"Input: {input_path}")
    print(f"Images: {len(images)} | eye={args.eye}")
    print(f"Model: {model} | sha256={sha256_file(model)}")
    print("MediaPipe image/world Z: RECORDED FOR DIAGNOSTICS ONLY; NEVER USED AS TOUCHPLUS METRIC DEPTH")

    records: list[dict[str, Any]] = []
    with create_landmarker(model, args) as landmarker:
        for index, path in enumerate(images, 1):
            record = process_one(path, landmarker, output_dir, input_path)
            records.append(record)
            print(f"[{index:03d}/{len(images):03d}] {path.name}: {record['status']} hands={record.get('hand_count', 0)}")
    write_summary(records, output_dir, model, args)
    print(f"Summary: {output_dir / 'summary.json'}")
    print(f"CSV:     {output_dir / 'summary.csv'}")
    print(f"Overlays:{output_dir / 'annotations'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
