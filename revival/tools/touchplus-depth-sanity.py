#!/usr/bin/env python3
"""TouchPlus Revival offline disparity / metric-depth sanity tool.

This is a validation tool, not yet the live runtime. It rectifies one synchronized
LEFT/RIGHT pair with a candidate calibration, runs StereoSGBM, reprojects disparity
through Q into millimetres, saves diagnostics, and can compare sampled points with
known physical distances.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

import cv2
import numpy as np


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Validate TouchPlus candidate calibration with one stereo pair")
    p.add_argument("calibration", type=Path, help="Candidate calibration JSON")
    p.add_argument("left", type=Path, help="LEFT 640x480 PNG")
    p.add_argument("right", type=Path, help="RIGHT 640x480 PNG")
    p.add_argument("--output", type=Path, default=Path("touchplus-depth-sanity"))
    p.add_argument("--num-disparities", type=int, default=160, help="StereoSGBM disparity search range; rounded up to multiple of 16")
    p.add_argument("--block-size", type=int, default=5, help="StereoSGBM odd block size")
    p.add_argument("--min-z-mm", type=float, default=80.0)
    p.add_argument("--max-z-mm", type=float, default=3000.0)
    p.add_argument("--sample", action="append", default=[], metavar="LABEL,X,Y,EXPECTED_MM", help="Known-distance sample; may be repeated")
    p.add_argument("--sample-radius", type=int, default=5, help="Median sample radius in pixels")
    p.add_argument("--max-error-percent", type=float, default=None, help="Return REVIEW if any known sample exceeds this absolute percent error")
    return p.parse_args()


def matrix(data: dict[str, Any], key: str) -> np.ndarray:
    if key not in data:
        raise ValueError(f"Calibration missing {key}")
    return np.asarray(data[key], dtype=np.float64)


def parse_sample(text: str) -> tuple[str, int, int, float]:
    parts = [x.strip() for x in text.split(",")]
    if len(parts) != 4:
        raise ValueError(f"Bad --sample {text!r}; expected LABEL,X,Y,EXPECTED_MM")
    return parts[0], int(parts[1]), int(parts[2]), float(parts[3])


def image(path: Path, expected_size: tuple[int, int]) -> np.ndarray:
    img = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if img is None:
        raise ValueError(f"Could not read {path}")
    h, w = img.shape[:2]
    if (w, h) != expected_size:
        raise ValueError(f"Unexpected image size {(w, h)} for {path}; expected {expected_size}")
    return img


def sample_depth(depth: np.ndarray, valid: np.ndarray, x: int, y: int, radius: int) -> tuple[float | None, int]:
    h, w = depth.shape
    x0, x1 = max(0, x - radius), min(w, x + radius + 1)
    y0, y1 = max(0, y - radius), min(h, y + radius + 1)
    mask = valid[y0:y1, x0:x1]
    values = depth[y0:y1, x0:x1][mask]
    if values.size == 0:
        return None, 0
    return float(np.median(values)), int(values.size)


def main() -> int:
    args = parse_args()
    cal = json.loads(args.calibration.read_text(encoding="utf-8"))
    size = tuple(int(v) for v in cal.get("image_size", [640, 480]))
    if len(size) != 2:
        raise ValueError("Calibration image_size must contain width,height")

    K1, D1 = matrix(cal, "K1"), matrix(cal, "D1")
    K2, D2 = matrix(cal, "K2"), matrix(cal, "D2")
    R1, R2 = matrix(cal, "R1"), matrix(cal, "R2")
    P1, P2, Q = matrix(cal, "P1"), matrix(cal, "P2"), matrix(cal, "Q")

    left = image(args.left, size)
    right = image(args.right, size)
    gray_left = cv2.cvtColor(left, cv2.COLOR_BGR2GRAY)
    gray_right = cv2.cvtColor(right, cv2.COLOR_BGR2GRAY)

    map1x, map1y = cv2.initUndistortRectifyMap(K1, D1, R1, P1, size, cv2.CV_32FC1)
    map2x, map2y = cv2.initUndistortRectifyMap(K2, D2, R2, P2, size, cv2.CV_32FC1)
    rect_left = cv2.remap(gray_left, map1x, map1y, cv2.INTER_LINEAR)
    rect_right = cv2.remap(gray_right, map2x, map2y, cv2.INTER_LINEAR)

    num_disp = max(16, ((int(args.num_disparities) + 15) // 16) * 16)
    block = max(3, int(args.block_size) | 1)
    matcher = cv2.StereoSGBM_create(
        minDisparity=0,
        numDisparities=num_disp,
        blockSize=block,
        P1=8 * block * block,
        P2=32 * block * block,
        disp12MaxDiff=2,
        preFilterCap=31,
        uniquenessRatio=8,
        speckleWindowSize=80,
        speckleRange=2,
        mode=cv2.STEREO_SGBM_MODE_SGBM_3WAY,
    )
    disparity = matcher.compute(rect_left, rect_right).astype(np.float32) / 16.0
    points = cv2.reprojectImageTo3D(disparity, Q)
    depth = points[:, :, 2]
    valid = (
        np.isfinite(depth)
        & (disparity > 0.5)
        & (depth >= float(args.min_z_mm))
        & (depth <= float(args.max_z_mm))
    )

    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out / "rectified-left.png"), rect_left)
    cv2.imwrite(str(out / "rectified-right.png"), rect_right)

    stereo = np.hstack([rect_left, rect_right])
    stereo = cv2.cvtColor(stereo, cv2.COLOR_GRAY2BGR)
    for y in range(40, size[1], 40):
        cv2.line(stereo, (0, y), (stereo.shape[1] - 1, y), (0, 255, 0), 1, cv2.LINE_AA)
    cv2.imwrite(str(out / "rectified-stereo.png"), stereo)

    valid_disp = disparity[valid]
    if valid_disp.size:
        lo, hi = np.percentile(valid_disp, [2, 98])
        scale = np.clip((disparity - lo) / max(1e-6, hi - lo), 0.0, 1.0)
        disp_u8 = (scale * 255).astype(np.uint8)
        disp_vis = cv2.applyColorMap(disp_u8, cv2.COLORMAP_TURBO)
        disp_vis[~valid] = 0
    else:
        disp_vis = np.zeros((size[1], size[0], 3), dtype=np.uint8)
    cv2.imwrite(str(out / "disparity.png"), disp_vis)

    depth_vis = np.zeros((size[1], size[0], 3), dtype=np.uint8)
    if np.any(valid):
        clipped = np.clip(depth, args.min_z_mm, args.max_z_mm)
        norm = 1.0 - (clipped - args.min_z_mm) / max(1e-6, args.max_z_mm - args.min_z_mm)
        depth_u8 = (np.clip(norm, 0.0, 1.0) * 255).astype(np.uint8)
        depth_vis = cv2.applyColorMap(depth_u8, cv2.COLORMAP_TURBO)
        depth_vis[~valid] = 0
    cv2.imwrite(str(out / "depth-mm.png"), depth_vis)
    np.save(out / "depth-mm.npy", depth.astype(np.float32))
    np.save(out / "disparity.npy", disparity.astype(np.float32))

    samples = []
    sample_fail = False
    for text in args.sample:
        label, x, y, expected = parse_sample(text)
        measured, count = sample_depth(depth, valid, x, y, args.sample_radius)
        error_mm = None if measured is None else measured - expected
        error_percent = None if measured is None or expected == 0 else 100.0 * error_mm / expected
        if args.max_error_percent is not None:
            if measured is None or abs(error_percent) > args.max_error_percent:
                sample_fail = True
        samples.append({
            "label": label,
            "pixel": [x, y],
            "expected_mm": expected,
            "measured_mm": measured,
            "valid_pixels": count,
            "error_mm": error_mm,
            "error_percent": error_percent,
        })

    valid_depth = depth[valid]
    report = {
        "schema": "touchplus-revival-depth-sanity-v1",
        "calibration_serial": cal.get("device_serial"),
        "calibration_promotion_state": cal.get("promotion_state"),
        "image_size": list(size),
        "stereo_sgbm": {"num_disparities": num_disp, "block_size": block},
        "valid_depth_fraction": float(np.mean(valid)),
        "valid_depth_summary_mm": None if not valid_depth.size else {
            "median": float(np.median(valid_depth)),
            "p10": float(np.percentile(valid_depth, 10)),
            "p90": float(np.percentile(valid_depth, 90)),
        },
        "known_distance_samples": samples,
        "known_distance_acceptance": None if args.max_error_percent is None else {
            "max_absolute_error_percent": float(args.max_error_percent),
            "pass": not sample_fail,
        },
        "note": "Dense stereo can be ambiguous on repetitive checkerboards; use a textured real scene for physical depth validation.",
    }
    (out / "depth-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

    print("=== TOUCHPLUS DEPTH SANITY ===")
    print(f"Calibration serial : {cal.get('device_serial')}")
    print(f"Valid depth pixels : {100.0 * float(np.mean(valid)):.1f}%")
    if valid_depth.size:
        print(f"Median valid depth : {float(np.median(valid_depth)):.1f} mm")
    for sample in samples:
        print(f"{sample['label']}: expected={sample['expected_mm']:.1f} mm measured={sample['measured_mm']} error%={sample['error_percent']}")
    print(f"Output             : {out}")
    if args.max_error_percent is not None:
        print(f"Known-distance gate: {'PASS' if not sample_fail else 'REVIEW'}")
    return 2 if sample_fail else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
