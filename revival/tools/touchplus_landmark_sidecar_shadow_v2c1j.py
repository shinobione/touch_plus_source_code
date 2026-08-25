#!/usr/bin/env python3
"""Phase 2C.1J.1 diagnostic-only ungated anatomy sidecar.

This process uses completely separate named shared-memory channels from the
accepted Phase 2B.9C sidecar. It may ignore the accepted hand_valid bit only on
that shadow channel so we can answer one physical question: does OpenCV Zoo
still see the index when the accepted V8/V9 hand gate has already failed?

2C.1J.1 is intentionally low-impact: the C++ publisher only sends hand-loss
probe frames at ~5 Hz, and this shadow process caps OpenCV to one worker thread.
The result is telemetry only. It is never read by the accepted V9 anatomy
bridge, fusion, stereo, contact semantics or OS output.
"""
from __future__ import annotations

import argparse
import mmap
import pathlib
import sys
import time

import touchplus_landmark_sidecar_live as live

SHADOW_FRAME_MAP_NAME = r"Local\TouchPlusRevival2C1J_ShadowFrame_v1"
SHADOW_RESULT_MAP_NAME = r"Local\TouchPlusRevival2C1J_ShadowResult_v1"
SHADOW_OPENCV_THREADS = 1
DEFAULT_POLL_MS = 12.0


def _ungated_copy(frame: live.LiveFrame) -> live.LiveFrame:
    """Bypass accepted hand_valid only inside this shadow process."""
    return live.LiveFrame(
        frame.frame_id,
        True,
        frame.background_ready,
        frame.left,
        frame.silhouette,
    )


def _process_shadow_frame(frame, palm_detector, handpose_model):
    # Background and a non-empty Touch+-derived appearance mask are still hard
    # prerequisites. Only accepted hand_valid is deliberately ignored here.
    if not frame.background_ready or frame.silhouette is None or not frame.silhouette.any():
        return {
            "status": live.STATUS_UNAVAILABLE,
            "frame_id": frame.frame_id,
            "reason_code": 101,
        }
    return live._process_live_frame(
        _ungated_copy(frame), palm_detector, handpose_model
    )


def run_self_test() -> int:
    import numpy as np

    ok = True

    def check(condition: bool, label: str) -> None:
        nonlocal ok
        print(("[PASS] " if condition else "[FAIL] ") + label)
        ok = ok and condition

    check(SHADOW_FRAME_MAP_NAME != live.FRAME_MAP_NAME,
          "shadow frame map differs from accepted frame map")
    check(SHADOW_RESULT_MAP_NAME != live.RESULT_MAP_NAME,
          "shadow result map differs from accepted result map")
    check(SHADOW_FRAME_MAP_NAME != SHADOW_RESULT_MAP_NAME,
          "shadow frame/result maps differ")
    check(SHADOW_OPENCV_THREADS == 1,
          "shadow OpenCV worker budget is capped to one thread")
    check(DEFAULT_POLL_MS >= 10.0,
          "shadow idle polling is intentionally relaxed")

    silhouette = np.zeros((live.FRAME_HEIGHT, live.FRAME_WIDTH), dtype=np.uint8)
    silhouette[120:180, 250:390] = 1
    left = np.zeros((live.FRAME_HEIGHT, live.FRAME_WIDTH), dtype=np.uint8)
    original = live.LiveFrame(17, False, True, left, silhouette)
    ungated = _ungated_copy(original)
    check(original.hand_valid is False,
          "accepted hand_valid remains false in source frame")
    check(ungated.hand_valid is True,
          "shadow copy bypasses hand_valid only locally")
    check(ungated.frame_id == original.frame_id and
          ungated.background_ready == original.background_ready and
          ungated.silhouette is original.silhouette,
          "shadow copy preserves frame/background/mask payload")

    empty = live.LiveFrame(
        18, False, True, left,
        np.zeros_like(silhouette),
    )
    blocked = _process_shadow_frame(empty, None, None)
    check(blocked.get("status") == live.STATUS_UNAVAILABLE and
          blocked.get("reason_code") == 101,
          "empty shadow mask still fails closed before model use")

    no_background = live.LiveFrame(19, False, False, left, silhouette)
    blocked_bg = _process_shadow_frame(no_background, None, None)
    check(blocked_bg.get("status") == live.STATUS_UNAVAILABLE and
          blocked_bg.get("reason_code") == 101,
          "background-not-ready still fails closed before model use")

    print("probe_policy=HAND_LOSS_ONLY_1_IN_6")
    print("accepted_pipeline_consumes_shadow=NO")
    print("shadow_only=YES")
    print("authoritative=UNCHANGED")
    print("OS_INJECTION=DISABLED")
    if ok:
        print("Phase 2C.1J.1 lightweight ungated shadow sidecar self-test PASS")
        return 0
    print("Phase 2C.1J.1 lightweight ungated shadow sidecar self-test FAIL")
    return 1


def run_live(assets: pathlib.Path, poll_ms: float) -> int:
    if sys.platform != "win32":
        raise RuntimeError(
            "Phase 2C.1J.1 shadow sidecar currently requires Windows named shared memory"
        )

    import cv2 as cv
    cv.setNumThreads(SHADOW_OPENCV_THREADS)
    palm_detector, handpose_model = live.base._load_zoo(assets)
    frame_map = mmap.mmap(
        -1,
        live.FRAME_MAP_BYTES,
        tagname=SHADOW_FRAME_MAP_NAME,
        access=mmap.ACCESS_WRITE,
    )
    result_map = mmap.mmap(
        -1,
        live.RESULT_MAP_BYTES,
        tagname=SHADOW_RESULT_MAP_NAME,
        access=mmap.ACCESS_WRITE,
    )
    print(
        "[ANATOMY_SHADOW_SIDECAR] Phase 2C.1J.1 online"
        " | IPC=SEPARATE"
        " | publisher=HAND_LOSS_ONLY_1_IN_6"
        f" | opencv_threads={SHADOW_OPENCV_THREADS}"
        " | accepted_hand_valid_ignored=SHADOW_ONLY"
        " | accepted_pipeline_consumes_shadow=NO"
        " | model_Z_metric_use=DISABLED"
        " | OS_INJECTION=DISABLED",
        flush=True,
    )

    last_frame_id = 0
    try:
        while True:
            frame = live._read_frame(frame_map)
            if frame is None or frame.frame_id == last_frame_id:
                time.sleep(max(0.001, poll_ms / 1000.0))
                continue
            last_frame_id = frame.frame_id
            accepted_hand = frame.hand_valid
            try:
                result = _process_shadow_frame(
                    frame, palm_detector, handpose_model
                )
            except Exception as exc:
                result = {
                    "status": live.STATUS_ERROR,
                    "frame_id": frame.frame_id,
                    "reason_code": 255,
                }
                print(
                    f"[ANATOMY_SHADOW_SIDECAR] frame={frame.frame_id} ERROR {exc}"
                    " | shadow_only=YES",
                    flush=True,
                )

            live._write_result_dict(result_map, result)

            status = int(result.get("status", live.STATUS_ERROR))
            if status == live.STATUS_GUIDED:
                print(
                    f"[ANATOMY_SHADOW_SIDECAR] frame={frame.frame_id}"
                    f" accepted_hand={1 if accepted_hand else 0}"
                    " status=GUIDED_DISTAL"
                    f" src={result.get('source')}"
                    f" tip={result.get('tip_x'):.1f},{result.get('tip_y'):.1f}"
                    f" aq={result.get('axis_quality'):.2f}"
                    f" conf={result.get('hand_confidence'):.3f}"
                    " accepted_pipeline_consumes_shadow=NO"
                    " shadow_only=YES",
                    flush=True,
                )
            elif frame.frame_id % 30 == 0:
                print(
                    f"[ANATOMY_SHADOW_SIDECAR] frame={frame.frame_id}"
                    f" accepted_hand={1 if accepted_hand else 0}"
                    f" status={status}"
                    f" reason_code={result.get('reason_code', 0)}"
                    " accepted_pipeline_consumes_shadow=NO"
                    " shadow_only=YES",
                    flush=True,
                )
    except KeyboardInterrupt:
        return 0
    finally:
        frame_map.close()
        result_map.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="TouchPlus Phase 2C.1J.1 lightweight ungated shadow anatomy sidecar"
    )
    parser.add_argument(
        "--assets", type=pathlib.Path, default=pathlib.Path("landmark-assets")
    )
    parser.add_argument("--poll-ms", type=float, default=DEFAULT_POLL_MS)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    return run_self_test() if args.self_test else run_live(args.assets, args.poll_ms)


if __name__ == "__main__":
    raise SystemExit(main())
