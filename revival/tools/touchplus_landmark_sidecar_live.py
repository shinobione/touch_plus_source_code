#!/usr/bin/env python3
"""TouchPlus Revival Phase 2B.9C.1 live anatomical sidecar.

The Win32 Touch+ runtime owns camera capture, learned-background segmentation,
physical support bounding, stereo/Q and the surface frame. This sidecar receives
only the current LEFT-eye grayscale image plus the current Touch+ silhouette via
named shared memory, estimates index anatomy with OpenCV Zoo MediaPipe, and
returns a 2D GUIDED_DISTAL candidate.

Model Z is never transmitted. Touch+ stereo/Q remains the sole metric XYZ source.
"""
from __future__ import annotations

import argparse
import mmap
import pathlib
import struct
import sys
import time
from dataclasses import dataclass
from typing import Any, Optional

import touchplus_landmark_probe as base
import touchplus_landmark_probe_b91 as b91

FRAME_MAGIC = 0x39465054
RESULT_MAGIC = 0x39525054
PROTOCOL_VERSION = 1
FRAME_WIDTH, FRAME_HEIGHT = 640, 480
MASK_WIDTH, MASK_HEIGHT = 320, 240
LEFT_BYTES = FRAME_WIDTH * FRAME_HEIGHT
MASK_BYTES = MASK_WIDTH * MASK_HEIGHT
FRAME_HEADER_BYTES = 64
FRAME_MAP_BYTES = FRAME_HEADER_BYTES + LEFT_BYTES + MASK_BYTES
RESULT_MAP_BYTES = 96
FRAME_MAP_NAME = r"Local\TouchPlusRevival2B9C_Frame_v1"
RESULT_MAP_NAME = r"Local\TouchPlusRevival2B9C_Result_v1"

STATUS_UNAVAILABLE, STATUS_GUIDED, STATUS_REJECTED, STATUS_ERROR = 0, 1, 2, 3
SOURCE_NONE, SOURCE_FULL, SOURCE_ROI1, SOURCE_ROI2, SOURCE_ROI3 = 0, 1, 2, 3, 4
POSE_UNKNOWN, POSE_STRICT, POSE_PERSPECTIVE, POSE_NO_AXIS, POSE_AXIS_WEAK, POSE_PATH_WIDE = range(6)
SOURCE_ENUM = {"FULL_FRAME": SOURCE_FULL, "ROI_1": SOURCE_ROI1, "ROI_2": SOURCE_ROI2, "ROI_3": SOURCE_ROI3}
POSE_ENUM = {"STRICT_2D": POSE_STRICT, "PERSPECTIVE_SILHOUETTE": POSE_PERSPECTIVE, "NO_COHERENT_INDEX_AXIS": POSE_NO_AXIS, "PERSPECTIVE_AXIS_TOO_WEAK": POSE_AXIS_WEAK, "PERSPECTIVE_PATH_TOO_WIDE": POSE_PATH_WIDE}

@dataclass
class LiveFrame:
    frame_id: int
    hand_valid: bool
    background_ready: bool
    left: Any
    silhouette: Any

@dataclass
class LiveEvaluation:
    hand: Any
    landmarks: Any
    confidence: float
    pose_mode: str
    axis: base.DistalAxis
    projection: base.GuidedProjection
    source: str
    quality: float


def _read_u32(buf, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


def _read_frame(buf) -> Optional[LiveFrame]:
    import numpy as np
    import cv2 as cv
    seq1 = _read_u32(buf, 0)
    if seq1 & 1:
        return None
    header = bytes(buf[:FRAME_HEADER_BYTES])
    left_bytes = bytes(buf[FRAME_HEADER_BYTES:FRAME_HEADER_BYTES + LEFT_BYTES])
    mask_bytes = bytes(buf[FRAME_HEADER_BYTES + LEFT_BYTES:FRAME_MAP_BYTES])
    seq2 = _read_u32(buf, 0)
    if seq1 != seq2 or (seq2 & 1):
        return None
    values = struct.unpack_from("<16I", header, 0)
    _, magic, version, frame_id, width, height, mask_width, mask_height, hand_valid, background_ready, *_ = values
    if magic != FRAME_MAGIC or version != PROTOCOL_VERSION or frame_id == 0:
        return None
    if (width, height, mask_width, mask_height) != (FRAME_WIDTH, FRAME_HEIGHT, MASK_WIDTH, MASK_HEIGHT):
        return None
    left = np.frombuffer(left_bytes, dtype=np.uint8).reshape(FRAME_HEIGHT, FRAME_WIDTH).copy()
    small = np.frombuffer(mask_bytes, dtype=np.uint8).reshape(MASK_HEIGHT, MASK_WIDTH).copy()
    silhouette = cv.resize(small, (FRAME_WIDTH, FRAME_HEIGHT), interpolation=cv.INTER_NEAREST)
    silhouette = (silhouette > 0).astype(np.uint8)
    return LiveFrame(frame_id, bool(hand_valid), bool(background_ready), left, silhouette)


def _write_result(buf, *, frame_id: int, status: int, source: int = SOURCE_NONE, pose_mode: int = POSE_UNKNOWN, candidate_count: int = 0, tip_x: float = -1.0, tip_y: float = -1.0, axis_dx: float = 0.0, axis_dy: float = 0.0, axis_quality: float = 0.0, hand_confidence: float = 0.0, continuity: float = 0.0, lateral_px: float = 0.0, extension_px: float = 0.0, reason_code: int = 0) -> None:
    current = _read_u32(buf, 0)
    odd = ((current + 1) | 1) & 0xFFFFFFFF
    even = (odd + 1) & 0xFFFFFFFF
    struct.pack_into("<I", buf, 0, odd)
    struct.pack_into("<7I", buf, 4, RESULT_MAGIC, PROTOCOL_VERSION, int(frame_id), int(status), int(source), int(pose_mode), int(candidate_count))
    struct.pack_into("<9f", buf, 32, float(tip_x), float(tip_y), float(axis_dx), float(axis_dy), float(axis_quality), float(hand_confidence), float(continuity), float(lateral_px), float(extension_px))
    struct.pack_into("<I", buf, 68, int(reason_code))
    buf[72:96] = b"\x00" * 24
    struct.pack_into("<I", buf, 0, even)


def _seed_from_live_mask(silhouette) -> b91.SilhouetteSeed:
    import cv2 as cv
    import numpy as np
    if silhouette is None or silhouette.size == 0 or not silhouette.any():
        return b91.SilhouetteSeed(False, reason="empty live Touch+ silhouette", rois=[])
    mask = (silhouette > 0).astype(np.uint8)
    mask = cv.morphologyEx(mask, cv.MORPH_CLOSE, np.ones((3, 3), np.uint8), iterations=1)
    count, labels, stats, _ = cv.connectedComponentsWithStats(mask, 8)
    h, w = mask.shape
    candidates = []
    for label in range(1, count):
        x = int(stats[label, cv.CC_STAT_LEFT]); y = int(stats[label, cv.CC_STAT_TOP]); cw = int(stats[label, cv.CC_STAT_WIDTH]); ch = int(stats[label, cv.CC_STAT_HEIGHT]); area = int(stats[label, cv.CC_STAT_AREA])
        if area < 120 or area > int(w * h * 0.65):
            continue
        component = (labels == label).astype(np.uint8)
        distance = cv.distanceTransform(component, cv.DIST_L2, 5)
        _, radius, _, palm = cv.minMaxLoc(distance)
        if radius < 8.0:
            continue
        score = float(area) * (1.0 + min(float(radius), 100.0) / 180.0)
        candidates.append((score, label, x, y, cw, ch, palm, float(radius)))
    if not candidates:
        return b91.SilhouetteSeed(False, reason="no plausible live silhouette component", rois=[])
    candidates.sort(key=lambda item: item[0], reverse=True)
    _, label, x, y, cw, ch, palm, radius = candidates[0]
    component = (labels == label).astype(np.uint8)
    bx, by = x + cw * 0.5, y + ch * 0.5
    pcx, pcy = float(palm[0]), float(palm[1])
    cx, cy = 0.85 * pcx + 0.15 * bx, 0.85 * pcy + 0.15 * by
    rois = []
    for factor in (6.5, 8.0, 10.0):
        side = max(180.0, min(420.0, radius * factor))
        x0, y0, x1, y1 = int(round(cx-side*0.5)), int(round(cy-side*0.5)), int(round(cx+side*0.5)), int(round(cy+side*0.5))
        if x0 < 0: x1 -= x0; x0 = 0
        if y0 < 0: y1 -= y0; y0 = 0
        if x1 > w: x0 -= x1-w; x1 = w
        if y1 > h: y0 -= y1-h; y1 = h
        x0, y0 = max(0, x0), max(0, y0)
        if x1-x0 >= 96 and y1-y0 >= 96:
            roi = (x0, y0, x1, y1)
            if roi not in rois: rois.append(roi)
    return b91.SilhouetteSeed(True, mask=component, bbox=(x,y,cw,ch), palm_center=(pcx,pcy), palm_radius=radius, rois=rois, reason="ok")


def _evaluate_hand_live(silhouette, hand, source: str) -> LiveEvaluation:
    landmarks = hand[4:67].reshape(21, 3)
    confidence = float(hand[-1])
    pose_ok, pose_mode, axis = b91._index_pose_evidence(landmarks, silhouette)
    projection = base.project_distal_to_silhouette(silhouette, landmarks, hand_confidence=confidence, index_extended=pose_ok)
    continuity = float(projection.continuity or 0.0)
    lateral = float(projection.lateral_px or 0.0) / max(axis.scale_px, 1.0)
    quality = confidence + 0.45*float(axis.quality) + 0.25*continuity - 0.08*lateral if projection.status == "GUIDED_DISTAL" else -1.0
    return LiveEvaluation(hand, landmarks, confidence, pose_mode, axis, projection, source, quality)


def _process_live_frame(frame: LiveFrame, palm_detector, handpose_model) -> dict[str, Any]:
    import cv2 as cv
    if not frame.background_ready or not frame.hand_valid or not frame.silhouette.any():
        return {"status": STATUS_UNAVAILABLE, "frame_id": frame.frame_id, "reason_code": 1}
    image_bgr = cv.cvtColor(frame.left, cv.COLOR_GRAY2BGR)
    seed = _seed_from_live_mask(frame.silhouette)
    evaluations: list[LiveEvaluation] = []
    _, full_hand = base._best_hand(image_bgr, palm_detector, handpose_model)
    chosen: Optional[LiveEvaluation] = None
    if full_hand is not None:
        full = _evaluate_hand_live(frame.silhouette, full_hand, "FULL_FRAME")
        evaluations.append(full)
        if full.projection.status == "GUIDED_DISTAL": chosen = full
    if chosen is None and seed.valid:
        for idx, roi in enumerate(seed.rois or []):
            hand = b91._best_hand_in_roi(image_bgr, roi, palm_detector, handpose_model)
            if hand is not None: evaluations.append(_evaluate_hand_live(frame.silhouette, hand, f"ROI_{idx+1}"))
        choice, reason = b91._choose_guided([{"hand":e.hand,"landmarks":e.landmarks,"confidence":e.confidence,"silhouette":frame.silhouette,"pose_mode":e.pose_mode,"axis":e.axis,"projection":e.projection,"source":e.source,"quality":e.quality} for e in evaluations], seed.palm_radius)
        if choice is not None:
            for e in evaluations:
                if e.source == choice["source"] and e.projection.tip == choice["projection"].tip: chosen = e; break
        elif reason == "REACQUISITION_DISAGREEMENT":
            return {"status":STATUS_REJECTED,"frame_id":frame.frame_id,"candidate_count":len(evaluations),"reason_code":2}
    if chosen is None:
        if not evaluations: return {"status":STATUS_UNAVAILABLE,"frame_id":frame.frame_id,"reason_code":3}
        diagnostic = max(evaluations, key=lambda e:e.confidence)
        status = STATUS_REJECTED if diagnostic.projection.status == "GUIDED_REJECTED" else STATUS_UNAVAILABLE
        return {"status":status,"frame_id":frame.frame_id,"source":SOURCE_ENUM.get(diagnostic.source,SOURCE_NONE),"pose_mode":POSE_ENUM.get(diagnostic.pose_mode,POSE_UNKNOWN),"candidate_count":len(evaluations),"hand_confidence":diagnostic.confidence,"axis_quality":diagnostic.axis.quality,"reason_code":4}
    p = chosen.projection
    return {"status":STATUS_GUIDED,"frame_id":frame.frame_id,"source":SOURCE_ENUM.get(chosen.source,SOURCE_NONE),"pose_mode":POSE_ENUM.get(chosen.pose_mode,POSE_UNKNOWN),"candidate_count":len(evaluations),"tip_x":float(p.tip[0]),"tip_y":float(p.tip[1]),"axis_dx":float(chosen.axis.dx),"axis_dy":float(chosen.axis.dy),"axis_quality":float(chosen.axis.quality),"hand_confidence":float(chosen.confidence),"continuity":float(p.continuity or 0.0),"lateral_px":float(p.lateral_px or 0.0),"extension_px":float(p.extension_px or 0.0),"reason_code":0}


def _write_result_dict(buf, result: dict[str, Any]) -> None:
    _write_result(buf, frame_id=int(result.get("frame_id",0)), status=int(result.get("status",STATUS_ERROR)), source=int(result.get("source",SOURCE_NONE)), pose_mode=int(result.get("pose_mode",POSE_UNKNOWN)), candidate_count=int(result.get("candidate_count",0)), tip_x=float(result.get("tip_x",-1.0)), tip_y=float(result.get("tip_y",-1.0)), axis_dx=float(result.get("axis_dx",0.0)), axis_dy=float(result.get("axis_dy",0.0)), axis_quality=float(result.get("axis_quality",0.0)), hand_confidence=float(result.get("hand_confidence",0.0)), continuity=float(result.get("continuity",0.0)), lateral_px=float(result.get("lateral_px",0.0)), extension_px=float(result.get("extension_px",0.0)), reason_code=int(result.get("reason_code",0)))


def run_self_test() -> int:
    import cv2 as cv
    import numpy as np
    print("Phase 2B.9C.1 live sidecar protocol/ROI self-test")
    frame_buf = bytearray(FRAME_MAP_BYTES)
    struct.pack_into("<16I", frame_buf, 0, 2, FRAME_MAGIC, PROTOCOL_VERSION, 17, FRAME_WIDTH, FRAME_HEIGHT, MASK_WIDTH, MASK_HEIGHT, 1, 1, 0,0,0,0,0,0)
    left = np.zeros((FRAME_HEIGHT,FRAME_WIDTH),dtype=np.uint8); small=np.zeros((MASK_HEIGHT,MASK_WIDTH),dtype=np.uint8)
    cv.circle(small,(180,115),28,1,-1); cv.rectangle(small,(100,110),(180,120),1,-1)
    frame_buf[FRAME_HEADER_BYTES:FRAME_HEADER_BYTES+LEFT_BYTES]=left.tobytes(); frame_buf[FRAME_HEADER_BYTES+LEFT_BYTES:FRAME_MAP_BYTES]=small.tobytes()
    parsed=_read_frame(frame_buf); ok1=parsed is not None and parsed.frame_id==17 and parsed.hand_valid and parsed.silhouette.any()
    seed=_seed_from_live_mask(parsed.silhouette if parsed is not None else None); ok2=seed.valid and seed.palm_radius>=20.0 and bool(seed.rois)
    result_buf=bytearray(RESULT_MAP_BYTES); _write_result(result_buf,frame_id=17,status=STATUS_GUIDED,source=SOURCE_ROI2,pose_mode=POSE_STRICT,candidate_count=2,tip_x=311.0,tip_y=211.0,axis_dx=-0.94,axis_dy=-0.34,axis_quality=0.71,hand_confidence=0.998,continuity=1.0,lateral_px=17.7,extension_px=61.5)
    ok3=_read_u32(result_buf,4)==RESULT_MAGIC and _read_u32(result_buf,12)==17 and _read_u32(result_buf,16)==STATUS_GUIDED and abs(struct.unpack_from("<f",result_buf,32)[0]-311.0)<0.01
    print(f"  shared-frame ABI          : {ok1}"); print(f"  live silhouette ROI seed  : {ok2} palm={seed.palm_center} r={seed.palm_radius:.1f}"); print(f"  result packet ABI         : {ok3}")
    ok=ok1 and ok2 and ok3; print("PASS" if ok else "FAIL"); return 0 if ok else 1


def run_live(assets: pathlib.Path, poll_ms: float) -> int:
    if sys.platform != "win32": raise RuntimeError("Phase 2B.9C.1 live sidecar currently requires Windows named shared memory")
    palm_detector, handpose_model = base._load_zoo(assets)
    frame_map=mmap.mmap(-1,FRAME_MAP_BYTES,tagname=FRAME_MAP_NAME,access=mmap.ACCESS_WRITE); result_map=mmap.mmap(-1,RESULT_MAP_BYTES,tagname=RESULT_MAP_NAME,access=mmap.ACCESS_WRITE)
    print("[ANATOMY] Phase 2B.9C.1 sidecar online | model Z disabled | Touch+ stereo owns XYZ",flush=True)
    last_frame_id=0
    try:
        while True:
            frame=_read_frame(frame_map)
            if frame is None or frame.frame_id==last_frame_id: time.sleep(max(0.001,poll_ms/1000.0)); continue
            last_frame_id=frame.frame_id
            try: result=_process_live_frame(frame,palm_detector,handpose_model)
            except Exception as exc:
                result={"status":STATUS_ERROR,"frame_id":frame.frame_id,"reason_code":255}; print(f"[ANATOMY] frame={frame.frame_id} ERROR {exc}",flush=True)
            _write_result_dict(result_map,result)
            if result.get("status")==STATUS_GUIDED and frame.frame_id%10==0:
                print(f"[ANATOMY] frame={frame.frame_id} GUIDED_DISTAL src={result.get('source')} tip={result.get('tip_x'):.1f},{result.get('tip_y'):.1f} aq={result.get('axis_quality'):.2f} conf={result.get('hand_confidence'):.3f}",flush=True)
    except KeyboardInterrupt: return 0
    finally: frame_map.close(); result_map.close()


def main() -> int:
    parser=argparse.ArgumentParser(description="TouchPlus Phase 2B.9C.1 live anatomical sidecar")
    parser.add_argument("--assets",type=pathlib.Path,default=pathlib.Path("landmark-assets")); parser.add_argument("--poll-ms",type=float,default=4.0); parser.add_argument("--self-test",action="store_true")
    args=parser.parse_args(); return run_self_test() if args.self_test else run_live(args.assets,args.poll_ms)

if __name__ == "__main__":
    raise SystemExit(main())
