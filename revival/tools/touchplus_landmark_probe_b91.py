#!/usr/bin/env python3
"""TouchPlus Revival Phase 2B.9B.1 ROI-guided landmark reacquisition.

This is an offline follow-up to 2B.9B. It imports the existing 2B.9B probe and
adds only two recall-oriented layers that fail closed:

1. learned-background silhouette -> palm-core ROI -> PalmDet/HandPose retry;
2. a perspective-aware extended-index proof that requires a narrow Touch+
   silhouette corridor instead of merely relaxing the old wrist-distance test.

The raw MediaPipe INDEX_TIP remains diagnostic only. Model Z remains ignored.
Touch+ stereo/Q remains the sole metric XYZ source.
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
from dataclasses import dataclass
from typing import Any, Optional

import touchplus_landmark_probe as base


@dataclass
class SilhouetteSeed:
    valid: bool
    mask: Any = None
    bbox: Optional[tuple[int, int, int, int]] = None
    palm_center: Optional[tuple[float, float]] = None
    palm_radius: float = 0.0
    rois: Optional[list[tuple[int, int, int, int]]] = None
    reason: str = ""


def _primary_silhouette_seed(image, background) -> SilhouetteSeed:
    """Find a hand-sized changed component without requiring landmarks.

    This component is a *reacquisition seed only*. Final publication still
    requires landmark-supported silhouette overlap and 2B.9B distal gates.
    """
    import cv2 as cv
    import numpy as np

    if background is None:
        return SilhouetteSeed(False, reason="no background for ROI reacquisition", rois=[])

    def gray(img):
        if img.ndim == 2:
            return img
        if img.shape[2] == 4:
            return cv.cvtColor(img, cv.COLOR_BGRA2GRAY)
        return cv.cvtColor(img, cv.COLOR_BGR2GRAY)

    cur = gray(image)
    bg = gray(background)
    if cur.shape != bg.shape:
        return SilhouetteSeed(False, reason="background shape mismatch", rois=[])

    delta = base._appearance_delta_image(cur, bg)
    raw = (delta >= base.V5_APPEARANCE_ONLY_DELTA).astype(np.uint8)
    raw = cv.morphologyEx(raw, cv.MORPH_CLOSE, np.ones((3, 3), np.uint8), iterations=1)

    count, labels, stats, _ = cv.connectedComponentsWithStats(raw, 8)
    h, w = raw.shape
    candidates = []
    for label in range(1, count):
        x = int(stats[label, cv.CC_STAT_LEFT])
        y = int(stats[label, cv.CC_STAT_TOP])
        cw = int(stats[label, cv.CC_STAT_WIDTH])
        ch = int(stats[label, cv.CC_STAT_HEIGHT])
        area = int(stats[label, cv.CC_STAT_AREA])
        if area < 120 or area > int(w * h * 0.65):
            continue
        component = (labels == label).astype(np.uint8)
        distance = cv.distanceTransform(component, cv.DIST_L2, 5)
        _, radius, _, palm = cv.minMaxLoc(distance)
        if radius < 5.0:
            continue
        score = float(area) * (1.0 + min(float(radius), 60.0) / 120.0)
        candidates.append((score, label, x, y, cw, ch, palm, float(radius)))

    if not candidates:
        return SilhouetteSeed(False, reason="no plausible changed component", rois=[])

    candidates.sort(key=lambda item: item[0], reverse=True)
    _, label, x, y, cw, ch, palm, radius = candidates[0]
    component = (labels == label).astype(np.uint8)

    # Multi-scale square retries. 6.5x/8x/10x the inscribed palm radius give a
    # useful zoom for the small/far 2B.9B cases without trusting one crop size.
    bx = x + cw * 0.5
    by = y + ch * 0.5
    pcx = float(palm[0])
    pcy = float(palm[1])
    cx = 0.85 * pcx + 0.15 * bx
    cy = 0.85 * pcy + 0.15 * by
    rois: list[tuple[int, int, int, int]] = []

    for factor in (6.5, 8.0, 10.0):
        side = max(180.0, min(420.0, radius * factor))
        x0 = int(round(cx - side * 0.5))
        y0 = int(round(cy - side * 0.5))
        x1 = int(round(cx + side * 0.5))
        y1 = int(round(cy + side * 0.5))

        if x0 < 0:
            x1 -= x0
            x0 = 0
        if y0 < 0:
            y1 -= y0
            y0 = 0
        if x1 > w:
            x0 -= x1 - w
            x1 = w
        if y1 > h:
            y0 -= y1 - h
            y1 = h
        x0 = max(0, x0)
        y0 = max(0, y0)

        if x1 - x0 < 96 or y1 - y0 < 96:
            continue
        roi = (x0, y0, x1, y1)
        if roi not in rois:
            rois.append(roi)

    return SilhouetteSeed(
        True,
        mask=component,
        bbox=(x, y, cw, ch),
        palm_center=(pcx, pcy),
        palm_radius=radius,
        rois=rois,
        reason="ok",
    )


def _section_width(mask, origin, direction, forward_t: float, max_lateral: float = 90.0) -> float:
    import numpy as np

    if mask is None or mask.size == 0:
        return float("inf")
    d = np.array(direction, dtype=float)
    n = np.array([-d[1], d[0]], dtype=float)
    center = np.array(origin, dtype=float) + d * float(forward_t)
    h, w = mask.shape

    total = 1.0
    for sign in (-1.0, 1.0):
        extent = 0.0
        for step in range(1, int(max_lateral) + 1):
            p = center + n * (sign * float(step))
            x = int(round(float(p[0])))
            y = int(round(float(p[1])))
            if x < 0 or x >= w or y < 0 or y >= h or mask[y, x] == 0:
                break
            extent = float(step)
        total += extent
    return total


def _index_pose_evidence(landmarks, silhouette) -> tuple[bool, str, base.DistalAxis]:
    """Strict 2D evidence first; perspective evidence only with finger-width proof."""
    import numpy as np

    axis = base._distal_axis_xy(landmarks)
    strict = base._index_extended_xy(landmarks)
    if strict and axis.valid:
        return True, "STRICT_2D", axis
    if not axis.valid or silhouette is None or not silhouette.any():
        return False, "NO_COHERENT_INDEX_AXIS", axis

    mcp = landmarks[base.INDEX_MCP, :2].astype(float)
    pip = landmarks[base.INDEX_PIP, :2].astype(float)
    dip = landmarks[base.INDEX_DIP, :2].astype(float)
    tip = landmarks[base.INDEX_TIP, :2].astype(float)
    chain = float(
        np.linalg.norm(pip - mcp)
        + np.linalg.norm(dip - pip)
        + np.linalg.norm(tip - dip)
    )
    direct = float(np.linalg.norm(tip - mcp))
    straightness = direct / max(chain, 1e-6)
    if axis.quality < 0.78 or straightness < 0.78:
        return False, "PERSPECTIVE_AXIS_TOO_WEAK", axis

    d = np.array([axis.dx, axis.dy], dtype=float)
    model_t = float((tip - dip) @ d)
    width_dip = _section_width(silhouette, dip, d, 0.0)
    width_tip = _section_width(silhouette, dip, d, max(0.0, model_t))
    width_ratio = max(width_dip, width_tip) / max(axis.scale_px, 1e-6)

    # Real successful 2B.9B cases were roughly 0.5-1.3 phalanx scales wide.
    # The rejected frontal fist path was ~9x. Keep a large safety margin at 2.2.
    if width_ratio > 2.20:
        return False, "PERSPECTIVE_PATH_TOO_WIDE", axis

    return True, "PERSPECTIVE_SILHOUETTE", axis


def _remap_hand_from_crop(hand, x0: int, y0: int):
    import numpy as np

    out = np.array(hand, dtype=float, copy=True)
    if out.size < 132:
        return out
    out[0] += x0
    out[1] += y0
    out[2] += x0
    out[3] += y0
    screen = out[4:67].reshape(21, 3)
    screen[:, 0] += x0
    screen[:, 1] += y0
    out[4:67] = screen.reshape(-1)
    return out


def _best_hand_in_roi(image, roi, palm_detector, handpose_model):
    x0, y0, x1, y1 = roi
    crop = image[y0:y1, x0:x1]
    if crop.size == 0:
        return None

    # The ROI is already constrained by real Touch+ appearance change. PalmDet
    # can therefore retry at 0.35; this score never publishes by itself.
    old_threshold = getattr(palm_detector, "score_threshold", 0.50)
    try:
        palm_detector.score_threshold = min(float(old_threshold), 0.35)
        _, hand = base._best_hand(crop, palm_detector, handpose_model)
    finally:
        palm_detector.score_threshold = old_threshold

    if hand is None:
        return None
    return _remap_hand_from_crop(hand, x0, y0)


def _evaluate_hand(image, background, hand, source: str):
    landmarks = hand[4:67].reshape(21, 3)
    conf = float(hand[-1])
    silhouette = base._appearance_silhouette(image, background, landmarks) if background is not None else None
    pose_ok, pose_mode, axis = _index_pose_evidence(landmarks, silhouette)
    projection = base.project_distal_to_silhouette(
        silhouette,
        landmarks,
        hand_confidence=conf,
        index_extended=pose_ok,
    )
    continuity = float(projection.continuity or 0.0)
    lateral = float(projection.lateral_px or 0.0) / max(axis.scale_px, 1.0)
    quality = (
        float(conf) + 0.45 * float(axis.quality) + 0.25 * continuity - 0.08 * lateral
        if projection.status == "GUIDED_DISTAL" else -1.0
    )
    return {
        "hand": hand,
        "landmarks": landmarks,
        "confidence": conf,
        "silhouette": silhouette,
        "pose_mode": pose_mode,
        "axis": axis,
        "projection": projection,
        "source": source,
        "quality": quality,
    }


def _choose_guided(evaluations, palm_radius: float):
    guided = [e for e in evaluations if e["projection"].status == "GUIDED_DISTAL"]
    if not guided:
        return None, "NO_GUIDED_CANDIDATE"
    guided.sort(key=lambda e: e["quality"], reverse=True)
    best = guided[0]

    if len(guided) >= 2:
        a = best["projection"].tip
        b = guided[1]["projection"].tip
        if a is not None and b is not None:
            distance = math.hypot(a[0] - b[0], a[1] - b[1])
            threshold = max(18.0, min(46.0, palm_radius * 0.85 if palm_radius > 0 else 30.0))
            if distance > threshold and guided[1]["quality"] >= best["quality"] - 0.10:
                return None, "REACQUISITION_DISAGREEMENT"
    return best, "OK"


def evaluate_image(image_path, output_dir, assets, *, background, palm_detector, handpose_model):
    image = base._load_image(image_path)
    geometry_path = base._find_geometry_for_image(image_path)
    geometry = base._read_geometry(geometry_path) if geometry_path else None
    seed = _primary_silhouette_seed(image, background)

    result = {
        "image": str(image_path),
        "geometry_sidecar": str(geometry_path) if geometry_path else None,
        "metric_z_source": "TOUCHPLUS_STEREO_ONLY",
        "exact_tip_oracle_policy": "DISABLED_AFTER_2B9A_PHYSICAL_FAIL",
        "reacquisition_policy": "FULL_THEN_SILHOUETTE_ROI",
        "roi_seed_valid": seed.valid,
        "roi_seed_bbox": list(seed.bbox) if seed.bbox else None,
        "roi_seed_palm": list(seed.palm_center) if seed.palm_center else None,
        "roi_seed_radius": seed.palm_radius if seed.valid else None,
        "roi_candidates": [list(r) for r in (seed.rois or [])],
    }

    evaluations = []
    _, full_hand = base._best_hand(image, palm_detector, handpose_model)
    chosen = None
    if full_hand is not None:
        full = _evaluate_hand(image, background, full_hand, "FULL_FRAME")
        evaluations.append(full)
        if full["projection"].status == "GUIDED_DISTAL":
            chosen = full

    reacquisition_attempted = chosen is None and seed.valid
    if reacquisition_attempted:
        for idx, roi in enumerate(seed.rois or []):
            hand = _best_hand_in_roi(image, roi, palm_detector, handpose_model)
            if hand is None:
                continue
            evaluations.append(_evaluate_hand(image, background, hand, f"ROI_{idx + 1}"))
        choice, reason = _choose_guided(evaluations, seed.palm_radius)
        if choice is not None:
            chosen = choice
        elif reason == "REACQUISITION_DISAGREEMENT":
            result.update({
                "landmark_found": bool(evaluations),
                "guided_status": "GUIDED_REJECTED",
                "reason": "multiple strong reacquisition candidates disagree",
                "reacquisition_attempted": True,
                "candidate_count": len(evaluations),
                "guided_candidate_count": sum(
                    1 for e in evaluations if e["projection"].status == "GUIDED_DISTAL"
                ),
            })
            return result

    result["reacquisition_attempted"] = reacquisition_attempted
    result["candidate_count"] = len(evaluations)
    result["guided_candidate_count"] = sum(
        1 for e in evaluations if e["projection"].status == "GUIDED_DISTAL"
    )

    if chosen is None:
        if not evaluations:
            result.update({
                "landmark_found": False,
                "guided_status": "GUIDED_UNAVAILABLE",
                "reason": "no reliable hand after full-frame + silhouette-ROI reacquisition",
            })
            return result
        diag = max(evaluations, key=lambda e: e["confidence"])
        landmarks = diag["landmarks"]
        projection = diag["projection"]
        result.update({
            "landmark_found": True,
            "hand_confidence": diag["confidence"],
            "index_extended_2d": base._index_extended_xy(landmarks),
            "index_pose_mode": diag["pose_mode"],
            "landmark_source": diag["source"],
            "guided_status": projection.status,
            "guided_tip": None,
            "reason": projection.reason,
        })
        output_dir.mkdir(parents=True, exist_ok=True)
        annotated = output_dir / f"{image_path.stem}-guided-b91.png"
        base._draw_result(image, landmarks, geometry, projection, annotated, diag["silhouette"])
        result["annotated_image"] = str(annotated)
        return result

    hand = chosen["hand"]
    landmarks = chosen["landmarks"]
    axis = chosen["axis"]
    projection = chosen["projection"]

    def xy(idx: int):
        return [float(landmarks[idx, 0]), float(landmarks[idx, 1])]

    result.update({
        "landmark_found": True,
        "hand_confidence": chosen["confidence"],
        "index_extended_2d": base._index_extended_xy(landmarks),
        "index_pose_mode": chosen["pose_mode"],
        "landmark_source": chosen["source"],
        "index_mcp": xy(base.INDEX_MCP),
        "index_pip": xy(base.INDEX_PIP),
        "index_dip": xy(base.INDEX_DIP),
        "index_tip_model": xy(base.INDEX_TIP),
        "handedness_score": float(hand[-2]),
        "distal_axis_valid": axis.valid,
        "distal_axis": [axis.dx, axis.dy] if axis.valid else None,
        "distal_axis_quality": axis.quality,
        "distal_scale_px": axis.scale_px,
        "guided_status": projection.status,
        "guided_tip": list(projection.tip) if projection.tip is not None else None,
        "guided_extension_from_model_tip_px": projection.extension_px,
        "guided_lateral_px": projection.lateral_px,
        "guided_continuity": projection.continuity,
        "reason": projection.reason,
    })

    output_dir.mkdir(parents=True, exist_ok=True)
    annotated = output_dir / f"{image_path.stem}-guided-b91.png"
    base._draw_result(image, landmarks, geometry, projection, annotated, chosen["silhouette"])
    result["annotated_image"] = str(annotated)
    return result


def run_self_test() -> int:
    import cv2 as cv
    import numpy as np

    print("Phase 2B.9B.1 ROI-guided landmark reacquisition self-test")

    # Existing 2B.9B proximal-tip regression must remain green.
    mask = np.zeros((110, 180), dtype=np.uint8)
    mask[43:58, 45:146] = 1
    lm = base._synthetic_landmarks({base.INDEX_TIP: (116.0, 50.0)})
    p1 = base.project_distal_to_silhouette(mask, lm, hand_confidence=0.9983, index_extended=True)
    ok1 = p1.status == "GUIDED_DISTAL" and p1.tip is not None and p1.tip[0] >= 143.0

    # Foreshortened narrow index: old strict test fails but silhouette proof passes.
    narrow = np.zeros((140, 160), dtype=np.uint8)
    for t in range(68):
        x = 62 + int(round(t * 0.55))
        y = 48 + t
        narrow[max(0, y-6):min(140, y+7), max(0, x-6):min(160, x+7)] = 1
    lm2 = base._synthetic_landmarks({
        base.WRIST: (95.0, 103.0),
        base.INDEX_MCP: (65.0, 53.0), base.INDEX_PIP: (71.0, 65.0),
        base.INDEX_DIP: (78.0, 78.0), base.INDEX_TIP: (85.0, 91.0),
    })
    strict2 = base._index_extended_xy(lm2)
    pose2, mode2, _ = _index_pose_evidence(lm2, narrow)
    ok2 = (not strict2) and pose2 and mode2 == "PERSPECTIVE_SILHOUETTE"

    # Frontal fist/knuckle direction must remain rejected instead of becoming a
    # fake recall win. This models the physical pair-011 failure class.
    broad = np.zeros((150, 180), dtype=np.uint8)
    broad[35:135, 35:155] = 1
    lm3 = base._synthetic_landmarks({
        base.WRIST: (80.0, 100.0),
        base.INDEX_MCP: (72.0, 60.0), base.INDEX_PIP: (75.0, 67.0),
        base.INDEX_DIP: (84.0, 82.0), base.INDEX_TIP: (98.0, 94.0),
    })
    pose3, mode3, _ = _index_pose_evidence(lm3, broad)
    ok3 = (not pose3) and mode3 == "PERSPECTIVE_PATH_TOO_WIDE"

    # Background-derived ROI seed must zoom around a plausible palm and include
    # the attached distal finger without any landmark input.
    bg = np.zeros((240, 320, 3), dtype=np.uint8)
    cur = bg.copy()
    cv.circle(cur, (190, 125), 30, (255, 255, 255), -1)
    cv.rectangle(cur, (80, 116), (190, 134), (255, 255, 255), -1)
    seed = _primary_silhouette_seed(cur, bg)
    roi_ok = False
    if seed.valid:
        for x0, y0, x1, y1 in seed.rois or []:
            if x0 <= 80 and x1 >= 220 and y0 <= 95 and y1 >= 155:
                roi_ok = True
                break
    ok4 = seed.valid and seed.palm_radius >= 20.0 and roi_ok

    # Crop->full coordinate remap shifts only bbox/screen XY. Relative/model Z
    # and confidence are untouched.
    fake = np.zeros(132, dtype=float)
    fake[0:4] = [10, 20, 100, 120]
    scr = fake[4:67].reshape(21, 3)
    scr[base.INDEX_TIP] = [50, 60, 7]
    fake[4:67] = scr.reshape(-1)
    fake[-1] = 0.95
    remap = _remap_hand_from_crop(fake, 200, 100)
    rlm = remap[4:67].reshape(21, 3)
    ok5 = (
        remap[0] == 210 and remap[1] == 120
        and rlm[base.INDEX_TIP, 0] == 250 and rlm[base.INDEX_TIP, 1] == 160
        and rlm[base.INDEX_TIP, 2] == 7 and remap[-1] == 0.95
    )

    print(f"  proximal model tip retained         : {p1.status} tip={p1.tip}")
    print(f"  foreshortened narrow perspective    : strict={strict2} mode={mode2}")
    print(f"  broad fist/knuckle safety           : {mode3}")
    print(f"  silhouette ROI seed                 : valid={seed.valid} palm={seed.palm_center} r={seed.palm_radius:.1f}")
    print(f"  crop coordinate remap               : tip={rlm[base.INDEX_TIP].tolist()}")

    ok = ok1 and ok2 and ok3 and ok4 and ok5
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="TouchPlus Phase 2B.9B.1 ROI-guided landmark reacquisition evaluator"
    )
    parser.add_argument("--input", type=pathlib.Path)
    parser.add_argument("--background", type=pathlib.Path)
    parser.add_argument("--background-first", action="store_true")
    parser.add_argument("--all-images", action="store_true")
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("landmark-guided-reacquisition-results"))
    parser.add_argument("--assets", type=pathlib.Path, default=pathlib.Path("landmark-assets"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()
    if args.input is None:
        parser.error("--input is required unless --self-test is used")
    if args.background is not None and args.background_first:
        parser.error("choose only one of --background or --background-first")

    inputs = base._collect_inputs(args.input, all_images=args.all_images)
    if not inputs:
        raise RuntimeError(f"No input images found under {args.input}")

    background = None
    background_source = None
    if args.background_first:
        if len(inputs) < 2:
            raise RuntimeError("--background-first requires at least two LEFT images")
        background_source = inputs[0]
        background = base._load_image(background_source)
        inputs = inputs[1:]
    elif args.background is not None:
        background_source = args.background
        background = base._load_image(args.background)

    palm_detector, handpose_model = base._load_zoo(args.assets)
    results = []
    for image_path in inputs:
        try:
            result = evaluate_image(
                image_path,
                args.output,
                args.assets,
                background=background,
                palm_detector=palm_detector,
                handpose_model=handpose_model,
            )
        except Exception as exc:
            result = {
                "image": str(image_path),
                "guided_status": "ERROR",
                "reason": str(exc),
                "metric_z_source": "TOUCHPLUS_STEREO_ONLY",
                "exact_tip_oracle_policy": "DISABLED_AFTER_2B9A_PHYSICAL_FAIL",
                "reacquisition_policy": "FULL_THEN_SILHOUETTE_ROI",
            }
        results.append(result)
        print(
            f"{image_path.name}: {result.get('guided_status')} "
            f"source={result.get('landmark_source')} "
            f"pose={result.get('index_pose_mode')} "
            f"guided={result.get('guided_tip')} "
            f"candidates={result.get('candidate_count')}"
        )

    args.output.mkdir(parents=True, exist_ok=True)
    summary = {
        "phase": "2B.9B.1",
        "metric_z_source": "TOUCHPLUS_STEREO_ONLY",
        "exact_tip_oracle_policy": "DISABLED_AFTER_2B9A_PHYSICAL_FAIL",
        "reacquisition_policy": "FULL_THEN_SILHOUETTE_ROI",
        "background_source": str(background_source) if background_source else None,
        "results": results,
    }
    summary_path = args.output / "landmark-guided-reacquisition-summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"Summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
