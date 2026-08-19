#!/usr/bin/env python3
"""TouchPlus Revival local stereo calibration solver.

Consumes a persistent-capture dataset produced by touchplus_calibration_capture.exe,
detects the checkerboard in synchronized LEFT/RIGHT images, rejects gross per-pair
reprojection outliers, solves both cameras plus the stereo rig, rectifies the
accepted pairs, and writes a versioned calibration bundle and diagnostics.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import cv2
import numpy as np

PAIR_RE = re.compile(r"pair-(\d{3})-left\.png$", re.IGNORECASE)
BUNDLE_SCHEMA = "touchplus-revival-stereo-calibration-v1"
REPORT_SCHEMA = "touchplus-revival-calibration-report-v1"


@dataclass
class Pair:
    number: int
    left_path: Path
    right_path: Path
    manifest_path: Path | None
    manifest: dict[str, Any]
    left_corners: np.ndarray | None = None
    right_corners: np.ndarray | None = None
    left_detected: bool = False
    right_detected: bool = False
    left_sharpness: float = 0.0
    right_sharpness: float = 0.0
    initial_left_rmse: float | None = None
    initial_right_rmse: float | None = None
    final_left_rmse: float | None = None
    final_right_rmse: float | None = None
    rectified_epi_mean: float | None = None
    rectified_epi_p95: float | None = None
    rectified_epi_max: float | None = None
    robust_score: float | None = None
    included: bool = False
    exclusion_reason: str | None = None


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Solve TouchPlus Revival stereo calibration from a persistent capture dataset.")
    p.add_argument("dataset", type=Path, help="Dataset directory or .zip produced by touchplus_calibration_capture.exe")
    p.add_argument("--output", type=Path, default=Path("touchplus-calibration-solved"), help="Output directory")
    p.add_argument("--corners", default="9x6", help="Inner checkerboard corners, default 9x6")
    p.add_argument("--square-mm", type=float, default=None, help="Override square size in millimetres (normally read from pair JSON)")
    p.add_argument("--min-pairs", type=int, default=12, help="Minimum accepted pair count")
    p.add_argument("--outlier-sigma", type=float, default=2.5, help="Robust MAD sigma threshold for gross pair rejection")
    p.add_argument("--no-outlier-rejection", action="store_true", help="Keep all corner-detected pairs")
    return p.parse_args()


def parse_corners(text: str) -> tuple[int, int]:
    m = re.fullmatch(r"(\d+)x(\d+)", text.strip().lower())
    if not m:
        raise ValueError("--corners must look like 9x6")
    return int(m.group(1)), int(m.group(2))


def prepare_dataset(path: Path, work: Path) -> Path:
    if path.is_dir():
        return path.resolve()
    if path.suffix.lower() != ".zip":
        raise ValueError("dataset must be a directory or .zip")
    extract = work / "_dataset"
    if extract.exists():
        shutil.rmtree(extract)
    extract.mkdir(parents=True)
    with zipfile.ZipFile(path) as zf:
        zf.extractall(extract)
    return extract


def discover_raw_root(dataset_root: Path) -> Path:
    left_images = sorted(dataset_root.rglob("pair-*-left.png"))
    if not left_images:
        raise RuntimeError("No pair-###-left.png images found")
    parents = {p.parent.resolve() for p in left_images}
    if len(parents) != 1:
        raise RuntimeError(f"Expected one raw pair directory, found {len(parents)}")
    return next(iter(parents))


def load_pairs(raw_root: Path) -> list[Pair]:
    pairs: list[Pair] = []
    for left in sorted(raw_root.glob("pair-*-left.png")):
        m = PAIR_RE.match(left.name)
        if not m:
            continue
        n = int(m.group(1))
        right = raw_root / f"pair-{n:03d}-right.png"
        manifest_path = raw_root / f"pair-{n:03d}.json"
        if not right.exists():
            raise RuntimeError(f"Missing right image for pair {n:03d}")
        manifest: dict[str, Any] = {}
        if manifest_path.exists():
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        pairs.append(Pair(n, left, right, manifest_path if manifest_path.exists() else None, manifest))
    if not pairs:
        raise RuntimeError("No complete stereo pairs found")
    return pairs


def resolve_metadata(pairs: list[Pair], corners_override: tuple[int, int], square_override: float | None) -> tuple[str, tuple[int, int], float]:
    serials = {str(p.manifest.get("serial")) for p in pairs if p.manifest.get("serial") is not None}
    if len(serials) > 1:
        raise RuntimeError(f"Multiple serials in dataset: {sorted(serials)}")
    serial = next(iter(serials)) if serials else "unknown"

    manifest_corners = {tuple(p.manifest.get("inner_corners", [])) for p in pairs if p.manifest.get("inner_corners")}
    if manifest_corners:
        if len(manifest_corners) > 1:
            raise RuntimeError(f"Inconsistent inner_corners metadata: {manifest_corners}")
        dataset_corners = next(iter(manifest_corners))
        if tuple(corners_override) != tuple(dataset_corners):
            raise RuntimeError(f"Corner pattern mismatch: CLI {corners_override}, dataset {dataset_corners}")

    square_values = {float(p.manifest["square_mm"]) for p in pairs if p.manifest.get("square_mm") is not None}
    if square_override is not None:
        square_mm = float(square_override)
    elif square_values:
        if max(square_values) - min(square_values) > 1e-6:
            raise RuntimeError(f"Inconsistent square_mm metadata: {sorted(square_values)}")
        square_mm = next(iter(square_values))
    else:
        raise RuntimeError("square size missing; pass --square-mm")
    return serial, corners_override, square_mm


def detect_one(path: Path, pattern: tuple[int, int]) -> tuple[bool, np.ndarray | None, float, tuple[int, int]]:
    img = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if img is None:
        raise RuntimeError(f"Could not read {path}")
    h, w = img.shape[:2]
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    sharpness = float(cv2.Laplacian(gray, cv2.CV_64F).var())
    sb_flags = cv2.CALIB_CB_EXHAUSTIVE | cv2.CALIB_CB_ACCURACY | cv2.CALIB_CB_NORMALIZE_IMAGE
    ok, corners = cv2.findChessboardCornersSB(gray, pattern, flags=sb_flags)
    if not ok:
        flags = cv2.CALIB_CB_ADAPTIVE_THRESH | cv2.CALIB_CB_NORMALIZE_IMAGE
        ok, corners = cv2.findChessboardCorners(gray, pattern, flags=flags)
        if ok:
            criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 100, 1e-4)
            corners = cv2.cornerSubPix(gray, corners, (11, 11), (-1, -1), criteria)
    if ok and corners is not None:
        return True, corners.astype(np.float32), sharpness, (w, h)
    return False, None, sharpness, (w, h)


def make_object_points(pattern: tuple[int, int], square_mm: float) -> np.ndarray:
    obj = np.zeros((pattern[0] * pattern[1], 3), dtype=np.float32)
    obj[:, :2] = np.mgrid[0:pattern[0], 0:pattern[1]].T.reshape(-1, 2)
    obj[:, :2] *= float(square_mm)
    return obj


def calibrate_mono(obj: np.ndarray, pairs: list[Pair], side: str, image_size: tuple[int, int]):
    image_points = [getattr(p, f"{side}_corners") for p in pairs]
    object_sets = [obj.copy() for _ in pairs]
    rms, K, D, rvecs, tvecs = cv2.calibrateCamera(object_sets, image_points, image_size, None, None)
    errors: list[float] = []
    for p, rv, tv in zip(pairs, rvecs, tvecs):
        projected, _ = cv2.projectPoints(obj, rv, tv, K, D)
        observed = getattr(p, f"{side}_corners").reshape(-1, 2)
        delta = projected.reshape(-1, 2) - observed
        errors.append(float(np.sqrt(np.mean(np.sum(delta * delta, axis=1)))))
    return float(rms), K, D, errors


def robust_inlier_mask(scores: np.ndarray, sigma_factor: float, min_pairs: int) -> tuple[np.ndarray, dict[str, float]]:
    median = float(np.median(scores))
    mad = float(np.median(np.abs(scores - median)))
    robust_sigma = 1.4826 * mad
    threshold = median + sigma_factor * robust_sigma if robust_sigma > 1e-12 else float("inf")
    mask = scores <= threshold
    if int(mask.sum()) < min_pairs:
        order = np.argsort(scores)
        mask[:] = False
        mask[order[:min(min_pairs, len(scores))]] = True
    return mask, {
        "median_px": median,
        "mad_px": mad,
        "robust_sigma_px": robust_sigma,
        "threshold_px": float(threshold),
    }


def stereo_solve(obj: np.ndarray, pairs: list[Pair], image_size: tuple[int, int], K1, D1, K2, D2):
    objects = [obj.copy() for _ in pairs]
    left = [p.left_corners for p in pairs]
    right = [p.right_corners for p in pairs]
    criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 200, 1e-7)
    rms, K1, D1, K2, D2, R, T, E, F = cv2.stereoCalibrate(
        objects, left, right, K1, D1, K2, D2, image_size,
        criteria=criteria, flags=cv2.CALIB_FIX_INTRINSIC,
    )
    R1, R2, P1, P2, Q, roi1, roi2 = cv2.stereoRectify(
        K1, D1, K2, D2, image_size, R, T,
        flags=cv2.CALIB_ZERO_DISPARITY, alpha=0,
    )
    return float(rms), K1, D1, K2, D2, R, T, E, F, R1, R2, P1, P2, Q, roi1, roi2


def epipolar_stats(pair: Pair, K1, D1, K2, D2, R1, R2, P1, P2):
    left = cv2.undistortPoints(pair.left_corners, K1, D1, R=R1, P=P1).reshape(-1, 2)
    right = cv2.undistortPoints(pair.right_corners, K2, D2, R=R2, P=P2).reshape(-1, 2)
    dy = np.abs(left[:, 1] - right[:, 1])
    return float(np.mean(dy)), float(np.percentile(dy, 95)), float(np.max(dy)), dy


def as_list(value: np.ndarray) -> list[Any]:
    return np.asarray(value).tolist()


def write_opencv_yaml(path: Path, values: dict[str, Any]) -> None:
    fs = cv2.FileStorage(str(path), cv2.FILE_STORAGE_WRITE)
    if not fs.isOpened():
        raise RuntimeError(f"Could not open {path} for writing")
    for key, value in values.items():
        fs.write(key, value)
    fs.release()


def make_rectified_previews(out_dir: Path, pairs: list[Pair], image_size, K1, D1, K2, D2, R1, R2, P1, P2) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    map1x, map1y = cv2.initUndistortRectifyMap(K1, D1, R1, P1, image_size, cv2.CV_32FC1)
    map2x, map2y = cv2.initUndistortRectifyMap(K2, D2, R2, P2, image_size, cv2.CV_32FC1)
    for p in pairs:
        left = cv2.imread(str(p.left_path), cv2.IMREAD_COLOR)
        right = cv2.imread(str(p.right_path), cv2.IMREAD_COLOR)
        rect_left = cv2.remap(left, map1x, map1y, cv2.INTER_LINEAR)
        rect_right = cv2.remap(right, map2x, map2y, cv2.INTER_LINEAR)
        combo = np.hstack([rect_left, rect_right])
        for y in range(40, image_size[1], 40):
            cv2.line(combo, (0, y), (combo.shape[1] - 1, y), (0, 255, 0), 1, cv2.LINE_AA)
        cv2.putText(combo, f"pair {p.number:03d} | rectified", (12, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 255, 0), 2, cv2.LINE_AA)
        cv2.imwrite(str(out_dir / f"pair-{p.number:03d}-rectified.png"), combo)


def main() -> int:
    args = parse_args()
    pattern = parse_corners(args.corners)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    dataset_root = prepare_dataset(args.dataset.resolve(), out)
    raw_root = discover_raw_root(dataset_root)
    pairs = load_pairs(raw_root)
    serial, pattern, square_mm = resolve_metadata(pairs, pattern, args.square_mm)

    print(f"Dataset: {raw_root}")
    print(f"Device: {serial} | checkerboard {pattern[0]}x{pattern[1]} | square {square_mm:.3f} mm")

    image_size: tuple[int, int] | None = None
    for pair in pairs:
        pair.left_detected, pair.left_corners, pair.left_sharpness, left_size = detect_one(pair.left_path, pattern)
        pair.right_detected, pair.right_corners, pair.right_sharpness, right_size = detect_one(pair.right_path, pattern)
        if left_size != right_size:
            raise RuntimeError(f"Pair {pair.number:03d} has mismatched eye dimensions: {left_size} vs {right_size}")
        if image_size is None:
            image_size = left_size
        elif image_size != left_size:
            raise RuntimeError(f"Image size changed at pair {pair.number:03d}: {left_size} vs {image_size}")
        print(f"pair {pair.number:03d}: corners L={'OK' if pair.left_detected else 'MISS'} R={'OK' if pair.right_detected else 'MISS'}")
    assert image_size is not None

    detected = [p for p in pairs if p.left_detected and p.right_detected]
    for pair in pairs:
        if not (pair.left_detected and pair.right_detected):
            pair.exclusion_reason = "corner_detection_failed"
    if len(detected) < args.min_pairs:
        raise RuntimeError(f"Only {len(detected)} complete corner-detected pairs; need at least {args.min_pairs}")

    obj = make_object_points(pattern, square_mm)
    initial_left_rms, _, _, initial_left_errors = calibrate_mono(obj, detected, "left", image_size)
    initial_right_rms, _, _, initial_right_errors = calibrate_mono(obj, detected, "right", image_size)
    scores = np.array([
        math.sqrt((left * left + right * right) / 2.0)
        for left, right in zip(initial_left_errors, initial_right_errors)
    ], dtype=np.float64)
    for pair, left, right, score in zip(detected, initial_left_errors, initial_right_errors, scores):
        pair.initial_left_rmse = left
        pair.initial_right_rmse = right
        pair.robust_score = float(score)

    if args.no_outlier_rejection:
        inlier_mask = np.ones(len(detected), dtype=bool)
        robust_meta: dict[str, Any] = {"disabled": True}
    else:
        inlier_mask, robust_meta = robust_inlier_mask(scores, args.outlier_sigma, args.min_pairs)
        robust_meta.update({"disabled": False, "sigma_factor": float(args.outlier_sigma)})

    inliers: list[Pair] = []
    for pair, keep in zip(detected, inlier_mask):
        pair.included = bool(keep)
        if keep:
            inliers.append(pair)
        else:
            pair.exclusion_reason = "robust_reprojection_outlier"

    rejected = [p.number for p in detected if not p.included]
    print("Robust pair selection:", ", ".join(f"{p.number:03d}" for p in inliers))
    if rejected:
        print("Rejected gross reprojection outliers:", ", ".join(f"{n:03d}" for n in rejected))

    mono_left_rms, K1, D1, final_left_errors = calibrate_mono(obj, inliers, "left", image_size)
    mono_right_rms, K2, D2, final_right_errors = calibrate_mono(obj, inliers, "right", image_size)
    for pair, left, right in zip(inliers, final_left_errors, final_right_errors):
        pair.final_left_rmse = left
        pair.final_right_rmse = right

    (stereo_rms, K1, D1, K2, D2, R, T, E, F,
     R1, R2, P1, P2, Q, roi1, roi2) = stereo_solve(obj, inliers, image_size, K1, D1, K2, D2)

    all_dy: list[float] = []
    inlier_dy: list[float] = []
    for pair in detected:
        mean, p95, maximum, dy = epipolar_stats(pair, K1, D1, K2, D2, R1, R2, P1, P2)
        pair.rectified_epi_mean = mean
        pair.rectified_epi_p95 = p95
        pair.rectified_epi_max = maximum
        all_dy.extend(float(x) for x in dy)
        if pair.included:
            inlier_dy.extend(float(x) for x in dy)

    baseline_mm = float(np.linalg.norm(T))
    epi_mean = float(np.mean(inlier_dy))
    epi_p95 = float(np.percentile(inlier_dy, 95))
    epi_max = float(np.max(inlier_dy))

    acceptance = {
        "enough_pairs": len(inliers) >= args.min_pairs,
        "mono_left_rms_le_1px": mono_left_rms <= 1.0,
        "mono_right_rms_le_1px": mono_right_rms <= 1.0,
        "stereo_rms_le_1px": stereo_rms <= 1.0,
        "rectified_epi_mean_le_0_5px": epi_mean <= 0.5,
        "rectified_epi_p95_le_1px": epi_p95 <= 1.0,
    }
    acceptance["pass"] = all(acceptance.values())

    pair_rows = []
    for pair in pairs:
        pair_rows.append({
            "pair": pair.number,
            "corner_detection": {"left": pair.left_detected, "right": pair.right_detected},
            "sharpness_laplacian_variance": {"left": pair.left_sharpness, "right": pair.right_sharpness},
            "initial_mono_rmse_px": {"left": pair.initial_left_rmse, "right": pair.initial_right_rmse, "combined": pair.robust_score},
            "included": pair.included,
            "exclusion_reason": pair.exclusion_reason,
            "final_mono_rmse_px": {"left": pair.final_left_rmse, "right": pair.final_right_rmse},
            "rectified_epipolar_vertical_error_px": {
                "mean": pair.rectified_epi_mean,
                "p95": pair.rectified_epi_p95,
                "max": pair.rectified_epi_max,
            },
        })

    final_metrics = {
        "mono_rms_px": {"left": mono_left_rms, "right": mono_right_rms},
        "stereo_rms_px": stereo_rms,
        "baseline_mm": baseline_mm,
        "rectified_epipolar_vertical_error_px": {"mean": epi_mean, "p95": epi_p95, "max": epi_max},
        "all_detected_pairs_epipolar_vertical_error_px": {
            "mean": float(np.mean(all_dy)),
            "p95": float(np.percentile(all_dy, 95)),
            "max": float(np.max(all_dy)),
        },
    }

    report = {
        "schema": REPORT_SCHEMA,
        "device_serial": serial,
        "image_size": list(image_size),
        "checkerboard": {"inner_corners": list(pattern), "square_mm": square_mm},
        "input_pairs": len(pairs),
        "corner_detected_pairs": len(detected),
        "accepted_pairs": len(inliers),
        "accepted_pair_numbers": [p.number for p in inliers],
        "excluded_pair_numbers": [p.number for p in pairs if not p.included],
        "initial_mono_rms_px": {"left": initial_left_rms, "right": initial_right_rms},
        "robust_outlier_selection": robust_meta,
        "final_metrics": final_metrics,
        "acceptance": acceptance,
        "pairs": pair_rows,
    }

    bundle = {
        "schema": BUNDLE_SCHEMA,
        "device_serial": serial,
        "image_size": list(image_size),
        "checkerboard": {"inner_corners": list(pattern), "square_mm": square_mm},
        "source_pair_numbers": [p.number for p in inliers],
        "excluded_pair_numbers": [p.number for p in pairs if not p.included],
        "metrics": final_metrics,
        "K1": as_list(K1), "D1": as_list(D1),
        "K2": as_list(K2), "D2": as_list(D2),
        "R": as_list(R), "T_mm": as_list(T.reshape(-1)),
        "E": as_list(E), "F": as_list(F),
        "R1": as_list(R1), "R2": as_list(R2),
        "P1": as_list(P1), "P2": as_list(P2), "Q": as_list(Q),
        "valid_roi_left": list(map(int, roi1)),
        "valid_roi_right": list(map(int, roi2)),
        "accepted": bool(acceptance["pass"]),
    }

    report_path = out / "calibration-report.json"
    bundle_path = out / f"touchplus-calibration-{serial}.json"
    yaml_path = out / f"touchplus-calibration-{serial}.yml"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    bundle_path.write_text(json.dumps(bundle, indent=2), encoding="utf-8")
    write_opencv_yaml(yaml_path, {
        "device_serial": serial,
        "image_width": image_size[0],
        "image_height": image_size[1],
        "square_mm": float(square_mm),
        "K1": K1, "D1": D1, "K2": K2, "D2": D2,
        "R": R, "T_mm": T, "E": E, "F": F,
        "R1": R1, "R2": R2, "P1": P1, "P2": P2, "Q": Q,
    })
    make_rectified_previews(out / "rectified", inliers, image_size, K1, D1, K2, D2, R1, R2, P1, P2)

    archive = out.parent / f"touchplus-calibration-{serial}-solved.zip"
    if archive.exists():
        archive.unlink()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(out.rglob("*")):
            if path.is_file() and "_dataset" not in path.parts:
                zf.write(path, path.relative_to(out))

    print("\n=== TOUCHPLUS CALIBRATION RESULT ===")
    print(f"Accepted pairs        : {len(inliers)}/{len(pairs)}")
    print(f"Rejected pairs        : {', '.join(f'{n:03d}' for n in rejected) if rejected else 'none'}")
    print(f"Mono RMS L/R          : {mono_left_rms:.4f} / {mono_right_rms:.4f} px")
    print(f"Stereo RMS            : {stereo_rms:.4f} px")
    print(f"Stereo baseline       : {baseline_mm:.3f} mm")
    print(f"Rectified epi mean    : {epi_mean:.4f} px")
    print(f"Rectified epi p95     : {epi_p95:.4f} px")
    print(f"Acceptance            : {'PASS' if acceptance['pass'] else 'REVIEW'}")
    print(f"Bundle                : {bundle_path}")
    print(f"Archive               : {archive}")
    return 0 if acceptance["pass"] else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
