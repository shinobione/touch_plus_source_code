#!/usr/bin/env python3
"""TouchPlus Revival Phase 2B.9B landmark-guided distal projection evaluator.

This remains an offline/sidecar evaluation tool. It never provides metric Z and
never changes the accepted Win32 Etron runtime. 2B.9A proved that OpenCV Zoo
MediaPipe usually recognizes the hand/index on Touch+ imagery but landmark 8
can be confidently too proximal. 2B.9B therefore uses landmarks as ANATOMICAL
DIRECTION evidence, then projects that distal index axis onto a Touch+-derived
appearance silhouette. The silhouette, not the raw model tip, owns the final 2D
distal boundary hypothesis.

Touch+ stereo/Q remains the only metric XYZ source.
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
MIDDLE_MCP = 9
RING_MCP = 13
PINKY_MCP = 17
WRIST = 0

# Keep the accepted V5 appearance-only threshold as the offline silhouette
# baseline. This is deliberately conservative and does not pretend to replace
# V6 physical support bounding from the real tracker.
V5_APPEARANCE_ONLY_DELTA = 24.0


@dataclass
class DistalAxis:
    valid: bool
    dx: float = 0.0
    dy: float = 0.0
    quality: float = 0.0
    scale_px: float = 0.0
    reason: str = ""


@dataclass
class GuidedProjection:
    status: str
    tip: Optional[tuple[float, float]]
    extension_px: Optional[float]
    lateral_px: Optional[float]
    continuity: Optional[float]
    axis_quality: Optional[float]
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


def _norm2(x: float, y: float) -> float:
    return math.hypot(x, y)


def _unit(vx: float, vy: float) -> Optional[tuple[float, float]]:
    n = _norm2(vx, vy)
    if n < 1e-6:
        return None
    return vx / n, vy / n


def _dot(a: tuple[float, float], b: tuple[float, float]) -> float:
    return a[0] * b[0] + a[1] * b[1]


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


def _distal_axis_xy(landmarks) -> DistalAxis:
    """Estimate INDEX distal direction without trusting landmark 8 as endpoint.

    Weighted direction uses MCP->PIP, PIP->DIP and DIP->TIP segments. The tip
    may be too proximal on Touch+ imagery; its direction can still be useful.
    """
    import numpy as np

    pts = [landmarks[i, :2].astype(float) for i in
           (INDEX_MCP, INDEX_PIP, INDEX_DIP, INDEX_TIP)]
    segments: list[tuple[float, float]] = []
    lengths: list[float] = []
    for a, b in zip(pts[:-1], pts[1:]):
        vx = float(b[0] - a[0])
        vy = float(b[1] - a[1])
        u = _unit(vx, vy)
        if u is None:
            return DistalAxis(False, reason="degenerate index phalanx")
        segments.append(u)
        lengths.append(_norm2(vx, vy))

    # Reject a folded/contradictory chain. A real extended index should keep
    # adjacent phalanx directions broadly aligned even when landmark 8 is short.
    align01 = _dot(segments[0], segments[1])
    align12 = _dot(segments[1], segments[2])
    if min(align01, align12) < 0.35:
        return DistalAxis(False, reason="index phalanx directions disagree")

    weights = (0.20, 0.35, 0.45)
    vx = sum(w * s[0] for w, s in zip(weights, segments))
    vy = sum(w * s[1] for w, s in zip(weights, segments))
    u = _unit(vx, vy)
    if u is None:
        return DistalAxis(False, reason="distal direction cancelled")

    # Chain straightness and segment agreement form an interpretable quality.
    chain = sum(lengths)
    direct = float(np.linalg.norm(pts[-1] - pts[0]))
    straightness = direct / max(chain, 1e-6)
    direction_agreement = max(0.0, min(1.0, (align01 + align12) * 0.5))
    quality = max(0.0, min(1.0, 0.60 * straightness + 0.40 * direction_agreement))

    scale = sorted(lengths)[1]  # median phalanx length
    if scale < 4.0:
        return DistalAxis(False, reason="index phalanx scale too small")
    if quality < 0.66:
        return DistalAxis(False, quality=quality, scale_px=scale,
                          reason="distal axis quality too low")

    return DistalAxis(True, u[0], u[1], quality, scale, "ok")


def _appearance_delta_image(current_gray, background_gray):
    """Vectorized full-res analogue of appearance_delta_v4's 5-point average."""
    import cv2 as cv
    import numpy as np

    cur = current_gray.astype(np.int16)
    bg = background_gray.astype(np.int16)
    total = np.abs(cur - bg).astype(np.float32)
    for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2)):
        shifted = cv.warpAffine(
            np.abs(cur - bg).astype(np.float32),
            np.array([[1, 0, dx], [0, 1, dy]], dtype=np.float32),
            (cur.shape[1], cur.shape[0]),
            flags=cv.INTER_NEAREST,
            borderMode=cv.BORDER_REPLICATE,
        )
        total += shifted
    return total / 5.0


def _landmark_supported_component(mask, landmarks):
    """Select changed component overlapping palm/index landmarks.

    This offline gate substitutes only for V6's live physical support while we
    validate the distal-projection idea. It must fail closed if no coherent
    changed component overlaps the landmarked hand.
    """
    import cv2 as cv
    import numpy as np

    binary = (mask > 0).astype(np.uint8)
    if not binary.any():
        return None

    # V5 bridges tiny gaps. A 3x3 close approximates that behavior without
    # growing the mask aggressively.
    kernel = np.ones((3, 3), dtype=np.uint8)
    binary = cv.morphologyEx(binary, cv.MORPH_CLOSE, kernel, iterations=1)

    count, labels, stats, _ = cv.connectedComponentsWithStats(binary, 8)
    if count <= 1:
        return None

    core_ids = (WRIST, INDEX_MCP, MIDDLE_MCP, RING_MCP, PINKY_MCP,
                INDEX_PIP, INDEX_DIP)
    probe = [(int(round(float(landmarks[i, 0]))),
              int(round(float(landmarks[i, 1])))) for i in core_ids]

    best_label = -1
    best_score = -1.0
    h, w = binary.shape
    for label in range(1, count):
        area = int(stats[label, cv.CC_STAT_AREA])
        if area < 120 or area > int(w * h * 0.65):
            continue
        component = (labels == label).astype(np.uint8)
        near = cv.dilate(component, np.ones((11, 11), np.uint8), iterations=1)
        hits = 0
        for x, y in probe:
            if 0 <= x < w and 0 <= y < h and near[y, x]:
                hits += 1
        if hits < 3:
            continue
        score = hits * 100000.0 + area
        if score > best_score:
            best_score = score
            best_label = label

    if best_label < 0:
        return None
    return (labels == best_label).astype(np.uint8)


def _appearance_silhouette(image, background, landmarks):
    import cv2 as cv
    import numpy as np

    def gray(img):
        if img.ndim == 2:
            return img
        if img.shape[2] == 4:
            return cv.cvtColor(img, cv.COLOR_BGRA2GRAY)
        return cv.cvtColor(img, cv.COLOR_BGR2GRAY)

    cur = gray(image)
    bg = gray(background)
    if cur.shape != bg.shape:
        return None
    delta = _appearance_delta_image(cur, bg)
    raw = (delta >= V5_APPEARANCE_ONLY_DELTA).astype(np.uint8)
    return _landmark_supported_component(raw, landmarks)


def project_distal_to_silhouette(
    silhouette,
    landmarks,
    *,
    hand_confidence: float,
    index_extended: bool,
) -> GuidedProjection:
    """Project the landmark INDEX axis to the real changed-silhouette boundary.

    Landmark 8 is deliberately *not* accepted as an endpoint oracle. It only
    contributes to direction. The returned point must be supported by one
    continuous silhouette corridor extending distally from INDEX_DIP.
    """
    import numpy as np

    if silhouette is None or silhouette.size == 0 or not silhouette.any():
        return GuidedProjection("GUIDED_UNAVAILABLE", None, None, None, None, None,
                                "no Touch+ appearance silhouette")
    if hand_confidence < 0.80:
        return GuidedProjection("GUIDED_UNAVAILABLE", None, None, None, None, None,
                                "landmark hand confidence below 0.80")
    if not index_extended:
        return GuidedProjection("GUIDED_REJECTED", None, None, None, None, None,
                                "landmarks do not support one extended index")

    axis = _distal_axis_xy(landmarks)
    if not axis.valid:
        return GuidedProjection("GUIDED_REJECTED", None, None, None, None,
                                axis.quality, axis.reason)

    dip = landmarks[INDEX_DIP, :2].astype(float)
    model_tip = landmarks[INDEX_TIP, :2].astype(float)
    d = np.array([axis.dx, axis.dy], dtype=float)
    n = np.array([-axis.dy, axis.dx], dtype=float)

    # Corridor relative to actual observed phalanx scale, not a magic fixed px.
    half_width = float(max(4.0, min(18.0, axis.scale_px * 0.70)))
    max_forward = float(max(36.0, min(180.0, axis.scale_px * 6.0)))
    start_back = float(max(3.0, min(12.0, axis.scale_px * 0.35)))

    ys, xs = np.nonzero(silhouette > 0)
    if len(xs) == 0:
        return GuidedProjection("GUIDED_UNAVAILABLE", None, None, None, None,
                                axis.quality, "empty selected silhouette")
    points = np.column_stack([xs.astype(float), ys.astype(float)])
    rel = points - dip[None, :]
    t = rel @ d
    lateral = np.abs(rel @ n)
    keep = (t >= -start_back) & (t <= max_forward) & (lateral <= half_width)
    if int(np.count_nonzero(keep)) < 12:
        return GuidedProjection("GUIDED_REJECTED", None, None, None, None,
                                axis.quality, "insufficient silhouette support in distal corridor")

    kt = t[keep]
    kp = points[keep]
    kl = lateral[keep]

    # Build 1px forward occupancy bins. We track the component continuously
    # from around DIP and stop after a real gap, so a detached appearance tail
    # farther along the same ray cannot steal the tip.
    lo = int(math.floor(max(-start_back, float(kt.min()))))
    hi = int(math.ceil(min(max_forward, float(kt.max()))))
    bins: dict[int, list[int]] = {}
    for idx, tv in enumerate(kt):
        b = int(round(float(tv)))
        bins.setdefault(b, []).append(idx)

    seed_window = range(max(lo, -int(round(start_back))), min(hi, int(round(axis.scale_px))) + 1)
    seed_bins = [b for b in seed_window if b in bins]
    if not seed_bins:
        return GuidedProjection("GUIDED_REJECTED", None, None, None, None,
                                axis.quality, "distal corridor does not connect to INDEX_DIP")

    current = max(seed_bins)
    last_supported = current
    occupied_count = 0
    max_gap = max(4, int(round(axis.scale_px * 0.22)))
    gap = 0
    for b in range(current, hi + 1):
        if b in bins:
            occupied_count += 1
            last_supported = b
            gap = 0
        else:
            gap += 1
            if gap > max_gap:
                break

    if last_supported <= 0:
        return GuidedProjection("GUIDED_REJECTED", None, None, None, None,
                                axis.quality, "no distal extension beyond INDEX_DIP")

    # Need a continuous-looking finger corridor rather than isolated changed px.
    span = max(1, last_supported - current + 1)
    continuity = min(1.0, occupied_count / float(span))
    if continuity < 0.58:
        return GuidedProjection("GUIDED_REJECTED", None, None, None, continuity,
                                axis.quality, "distal corridor continuity too low")

    # Final boundary point: take the most distal supported slice and choose its
    # lateral median/center, reducing sensitivity to one silhouette edge pixel.
    final_indices: list[int] = []
    for b in range(max(current, last_supported - 2), last_supported + 1):
        final_indices.extend(bins.get(b, []))
    if not final_indices:
        return GuidedProjection("GUIDED_REJECTED", None, None, None, continuity,
                                axis.quality, "no distal boundary pixels")

    final_pts = kp[final_indices]
    final_t = kt[final_indices]
    final_l = kl[final_indices]
    # Prefer most distal pixels, then smallest lateral residual.
    order = np.lexsort((final_l, -final_t))
    pick = int(order[0])
    tip = (float(final_pts[pick, 0]), float(final_pts[pick, 1]))
    lateral_px = float(final_l[pick])

    model_tip_t = float((model_tip - dip) @ d)
    extension = float(last_supported - model_tip_t)

    # The guided point may be close to an already-correct model tip; extension
    # can therefore be near zero. Large negative extension is contradictory.
    if extension < -max(5.0, axis.scale_px * 0.35):
        return GuidedProjection("GUIDED_REJECTED", None, extension, lateral_px,
                                continuity, axis.quality,
                                "silhouette boundary lies materially behind model distal direction")

    return GuidedProjection(
        "GUIDED_DISTAL",
        tip,
        extension,
        lateral_px,
        continuity,
        axis.quality,
        "landmark index axis projected to continuous Touch+ appearance boundary",
    )


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


def _draw_result(image, landmarks, geometry, projection, output_path, silhouette=None):
    import cv2 as cv
    import numpy as np

    display = image.copy()
    model_tip = tuple(int(round(v)) for v in landmarks[INDEX_TIP, :2])
    mcp = tuple(int(round(v)) for v in landmarks[INDEX_MCP, :2])
    pip = tuple(int(round(v)) for v in landmarks[INDEX_PIP, :2])
    dip = tuple(int(round(v)) for v in landmarks[INDEX_DIP, :2])

    if silhouette is not None and silhouette.any():
        overlay = np.zeros_like(display)
        overlay[:, :, 1] = (silhouette > 0).astype(np.uint8) * 90
        display = cv.add(display, overlay)

    cv.line(display, mcp, pip, (0, 215, 255), 2, cv.LINE_AA)
    cv.line(display, pip, dip, (0, 215, 255), 2, cv.LINE_AA)
    cv.line(display, dip, model_tip, (0, 215, 255), 2, cv.LINE_AA)
    cv.circle(display, model_tip, 6, (0, 255, 0), 2)
    cv.putText(display, "MODEL TIP (diagnostic only)",
               (max(5, model_tip[0] + 8), max(18, model_tip[1] - 8)),
               cv.FONT_HERSHEY_SIMPLEX, 0.42, (0, 255, 0), 1, cv.LINE_AA)

    if projection.tip is not None:
        gp = tuple(int(round(v)) for v in projection.tip)
        cv.drawMarker(display, gp, (255, 0, 255), cv.MARKER_CROSS, 18, 2)
        cv.line(display, dip, gp, (255, 0, 255), 1, cv.LINE_AA)
        cv.putText(display, "GUIDED DISTAL", (max(5, gp[0] + 8), max(18, gp[1] - 8)),
                   cv.FONT_HERSHEY_SIMPLEX, 0.45, (255, 0, 255), 1, cv.LINE_AA)

    if geometry:
        gtip = _geometry_tip(geometry)
        if gtip is not None:
            p = (int(round(gtip[0])), int(round(gtip[1])))
            cv.drawMarker(display, p, (255, 255, 255), cv.MARKER_CROSS, 14, 1)
            cv.putText(display, "V8 GEOMETRY", (max(5, p[0] + 7), max(18, p[1] - 7)),
                       cv.FONT_HERSHEY_SIMPLEX, 0.40, (255, 255, 255), 1, cv.LINE_AA)

    cv.putText(display, projection.status, (8, 22),
               cv.FONT_HERSHEY_SIMPLEX, 0.58, (255, 255, 255), 2, cv.LINE_AA)
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


def _load_image(path: pathlib.Path):
    import cv2 as cv

    image = cv.imread(str(path), cv.IMREAD_UNCHANGED)
    if image is None:
        raise RuntimeError(f"Cannot read image: {path}")
    if image.ndim == 2:
        image = cv.cvtColor(image, cv.COLOR_GRAY2BGR)
    elif image.shape[2] == 4:
        image = cv.cvtColor(image, cv.COLOR_BGRA2BGR)
    return image


def evaluate_image(
    image_path: pathlib.Path,
    output_dir: pathlib.Path,
    assets: pathlib.Path,
    *,
    background=None,
    palm_detector=None,
    handpose_model=None,
) -> dict[str, Any]:
    image = _load_image(image_path)

    if palm_detector is None or handpose_model is None:
        palm_detector, handpose_model = _load_zoo(assets)

    _, hand = _best_hand(image, palm_detector, handpose_model)
    geometry_path = _find_geometry_for_image(image_path)
    geometry = _read_geometry(geometry_path) if geometry_path else None

    result: dict[str, Any] = {
        "image": str(image_path),
        "geometry_sidecar": str(geometry_path) if geometry_path else None,
        "metric_z_source": "TOUCHPLUS_STEREO_ONLY",
        "exact_tip_oracle_policy": "DISABLED_AFTER_2B9A_PHYSICAL_FAIL",
    }

    if hand is None:
        result.update({
            "landmark_found": False,
            "guided_status": "GUIDED_UNAVAILABLE",
            "reason": "no reliable hand landmarks",
        })
        return result

    landmarks = hand[4:67].reshape(21, 3)
    conf = float(hand[-1])
    extended = _index_extended_xy(landmarks)
    axis = _distal_axis_xy(landmarks)
    silhouette = _appearance_silhouette(image, background, landmarks) if background is not None else None
    projection = project_distal_to_silhouette(
        silhouette,
        landmarks,
        hand_confidence=conf,
        index_extended=extended,
    )

    def xy(idx: int) -> list[float]:
        return [float(landmarks[idx, 0]), float(landmarks[idx, 1])]

    result.update({
        "landmark_found": True,
        "hand_confidence": conf,
        "index_extended_2d": extended,
        "index_mcp": xy(INDEX_MCP),
        "index_pip": xy(INDEX_PIP),
        "index_dip": xy(INDEX_DIP),
        "index_tip_model": xy(INDEX_TIP),
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
    annotated = output_dir / f"{image_path.stem}-guided.png"
    _draw_result(image, landmarks, geometry, projection, annotated, silhouette)
    result["annotated_image"] = str(annotated)
    return result


def _collect_inputs(path: pathlib.Path, *, all_images: bool = False) -> list[pathlib.Path]:
    if path.is_file():
        return [path]

    left_patterns = ("*-left.pgm", "*-left.png", "*-left.jpg", "*-left.jpeg")
    left: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for pattern in left_patterns:
        for item in sorted(path.glob(pattern)):
            if item not in seen:
                seen.add(item)
                left.append(item)
    if left and not all_images:
        return left

    patterns = ("*.pgm", "*.png", "*.jpg", "*.jpeg")
    result = list(left)
    for pattern in patterns:
        for item in sorted(path.glob(pattern)):
            if item not in seen:
                seen.add(item)
                result.append(item)
    return result


def _synthetic_landmarks(points: dict[int, tuple[float, float]]):
    import numpy as np

    lm = np.zeros((21, 3), dtype=np.float32)
    # Plausible palm/wrist defaults.
    defaults = {
        WRIST: (30.0, 50.0), INDEX_MCP: (55.0, 50.0),
        INDEX_PIP: (75.0, 50.0), INDEX_DIP: (95.0, 50.0),
        INDEX_TIP: (112.0, 50.0), MIDDLE_MCP: (50.0, 58.0),
        RING_MCP: (46.0, 64.0), PINKY_MCP: (42.0, 70.0),
    }
    defaults.update(points)
    for idx, (x, y) in defaults.items():
        lm[idx, 0] = x
        lm[idx, 1] = y
    return lm


def run_self_test() -> int:
    import numpy as np

    print("Phase 2B.9B landmark-guided distal projection self-test")

    # Physical 2B.9A proxy: very high model confidence, but landmark 8 is
    # deliberately ~28 px proximal. The axis is correct and silhouette reaches
    # the real distal boundary. Exact-tip confidence must NOT veto that boundary.
    mask = np.zeros((110, 180), dtype=np.uint8)
    mask[43:58, 45:146] = 1
    lm = _synthetic_landmarks({INDEX_TIP: (116.0, 50.0)})
    p1 = project_distal_to_silhouette(mask, lm,
                                      hand_confidence=0.9983,
                                      index_extended=True)
    ok1 = (
        p1.status == "GUIDED_DISTAL" and p1.tip is not None
        and p1.tip[0] >= 143.0
        and (p1.extension_px or 0.0) >= 24.0
    )
    print(f"  proximal-high-confidence model tip -> {p1.status} tip={p1.tip} ext={p1.extension_px}")

    # Diagonal finger: projection should recover the distal silhouette boundary.
    mask2 = np.zeros((160, 180), dtype=np.uint8)
    for t in range(0, 90):
        x = 55 + t
        y = 55 + int(round(t * 0.55))
        mask2[max(0, y-6):min(mask2.shape[0], y+7),
              max(0, x-6):min(mask2.shape[1], x+7)] = 1
    lm2 = _synthetic_landmarks({
        INDEX_MCP: (60.0, 58.0), INDEX_PIP: (80.0, 69.0),
        INDEX_DIP: (100.0, 80.0), INDEX_TIP: (118.0, 90.0),
        WRIST: (42.0, 48.0),
    })
    p2 = project_distal_to_silhouette(mask2, lm2,
                                      hand_confidence=0.97,
                                      index_extended=True)
    ok2 = p2.status == "GUIDED_DISTAL" and p2.tip is not None and p2.tip[0] >= 137
    print(f"  diagonal distal projection          -> {p2.status} tip={p2.tip}")

    # Contradictory bent landmarks: fail closed.
    lm3 = _synthetic_landmarks({
        INDEX_MCP: (55.0, 50.0), INDEX_PIP: (75.0, 50.0),
        INDEX_DIP: (75.0, 70.0), INDEX_TIP: (55.0, 70.0),
    })
    p3 = project_distal_to_silhouette(mask, lm3,
                                      hand_confidence=0.999,
                                      index_extended=True)
    ok3 = p3.status == "GUIDED_REJECTED"
    print(f"  contradictory phalanx direction     -> {p3.status}")

    # No silhouette/background evidence: never publish model tip alone.
    p4 = project_distal_to_silhouette(None, lm,
                                      hand_confidence=0.999,
                                      index_extended=True)
    ok4 = p4.status == "GUIDED_UNAVAILABLE" and p4.tip is None
    print(f"  landmark-only without silhouette     -> {p4.status}")

    # Explicit non-index pose stays safe.
    p5 = project_distal_to_silhouette(mask, lm,
                                      hand_confidence=0.999,
                                      index_extended=False)
    ok5 = p5.status == "GUIDED_REJECTED" and p5.tip is None
    print(f"  non-index pose                       -> {p5.status}")

    ok = ok1 and ok2 and ok3 and ok4 and ok5
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="TouchPlus Phase 2B.9B offline landmark-guided distal evaluator"
    )
    parser.add_argument("--input", type=pathlib.Path,
                        help="LEFT-eye image or directory of local captures")
    parser.add_argument("--background", type=pathlib.Path,
                        help="clean LEFT-eye background frame from the same persistent session")
    parser.add_argument("--background-first", action="store_true",
                        help="use the first LEFT image in --input as background and skip it")
    parser.add_argument("--all-images", action="store_true",
                        help="evaluate full/right images too (default prefers LEFT only)")
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("landmark-guided-results"))
    parser.add_argument("--assets", type=pathlib.Path,
                        default=pathlib.Path("landmark-assets"))
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()
    if args.input is None:
        parser.error("--input is required unless --self-test is used")
    if args.background is not None and args.background_first:
        parser.error("choose only one of --background or --background-first")

    inputs = _collect_inputs(args.input, all_images=args.all_images)
    if not inputs:
        raise RuntimeError(f"No input images found under {args.input}")

    background = None
    background_source = None
    if args.background_first:
        if len(inputs) < 2:
            raise RuntimeError("--background-first requires at least two LEFT images")
        background_source = inputs[0]
        background = _load_image(background_source)
        inputs = inputs[1:]
    elif args.background is not None:
        background_source = args.background
        background = _load_image(args.background)

    palm_detector, handpose_model = _load_zoo(args.assets)
    results = []
    for image_path in inputs:
        try:
            result = evaluate_image(
                image_path, args.output, args.assets,
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
            }
        results.append(result)
        print(f"{image_path.name}: {result.get('guided_status')} "
              f"model_tip={result.get('index_tip_model')} "
              f"guided={result.get('guided_tip')} "
              f"ext={result.get('guided_extension_from_model_tip_px')}")

    args.output.mkdir(parents=True, exist_ok=True)
    summary = {
        "phase": "2B.9B",
        "metric_z_source": "TOUCHPLUS_STEREO_ONLY",
        "exact_tip_oracle_policy": "DISABLED_AFTER_2B9A_PHYSICAL_FAIL",
        "background_source": str(background_source) if background_source else None,
        "results": results,
    }
    summary_path = args.output / "landmark-guided-summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"Summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
