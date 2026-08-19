#pragma once

// Preserve the proven Phase 1C/2A include order first: depth_probe_lock.h brings
// in the hardened point matcher, then Phase 2A replaces only the surface fitter.
#include "depth_probe_lock.h"
#include "surface_frame_robust.h"
#include "depth_surface_frame.h"
#include "fingertip_tracker_v5.h"

#ifdef point_depth
#undef point_depth
#endif

#include <cstdint>
#include <iomanip>
#include <iostream>

namespace touchplus::depth {
namespace surface_runtime_detail {

inline bool undo_requested() {
    static thread_local bool previous_u_down = false;
    const bool down = (GetAsyncKeyState('U') & 0x8000) != 0;
    const bool rising = down && !previous_u_down;
    previous_u_down = down;
    return rising;
}

inline void handle_undo() {
    if (!undo_requested()) {
        return;
    }
    auto& s = touchplus::surface::live_detail::state();
    if (s.capturing) {
        std::cout << "[SURFACE] U ignored while a C capture is running.\n";
        return;
    }
    if (s.pending_points.empty()) {
        std::cout << "[SURFACE] No pending surface point to undo.\n";
        return;
    }
    s.pending_points.pop_back();
    std::cout << "[SURFACE] UNDID last pending point. Remaining="
              << s.pending_points.size() << ".\n";
}

} // namespace surface_runtime_detail

namespace tracking_runtime_detail {

struct RuntimeState {
    bool enabled = true;
    bool previous_t_down = false;
    bool previous_b_down = false;
    bool announced = false;
    std::uint64_t report_counter = 0;
    touchplus::tracking::FingertipTrackerV5 tracker;
    touchplus::tracking::TrackingResult result;
};

inline RuntimeState& state() {
    // All tracker work is intentionally owned by the capture/depth thread.
    static thread_local RuntimeState value;
    return value;
}

inline void announce_once(RuntimeState& s) {
    if (s.announced) return;
    s.announced = true;
    std::cout << "\n[TRACK] PHASE 2B.5 RUNTIME ACTIVE"
              << " | tracker=APPEARANCE-SILHOUETTE | T toggles ON/OFF\n";
    std::cout << "[TRACK] Clear the work area, then press B once to learn 30 clean background frames.\n";
    std::cout << "[TRACK] 2D background silhouette identifies the fingertip; robust stereo is used only to measure it.\n";
}

inline bool toggle_requested(RuntimeState& s) {
    const bool down = (GetAsyncKeyState('T') & 0x8000) != 0;
    const bool rising = down && !s.previous_t_down;
    s.previous_t_down = down;
    return rising;
}

inline bool background_requested(RuntimeState& s) {
    const bool down = (GetAsyncKeyState('B') & 0x8000) != 0;
    const bool rising = down && !s.previous_b_down;
    s.previous_b_down = down;
    return rising;
}

inline void maybe_toggle(RuntimeState& s) {
    announce_once(s);
    if (!toggle_requested(s)) return;
    s.enabled = !s.enabled;
    if (!s.enabled) {
        s.result = {};
    }
    std::cout << "[TRACK] Phase 2B geometry tracker "
              << (s.enabled ? "ENABLED" : "DISABLED")
              << " (T toggles).\n";
}

inline void maybe_background(RuntimeState& s) {
    announce_once(s);
    if (!background_requested(s)) return;
    s.tracker.request_background_capture();
    s.result = {};
    std::cout << "[TRACK] BACKGROUND LEARN STARTED | remove hand / raised temporary objects and hold scene still.\n";
}

inline void overlay_coarse_candidate(
    std::vector<uint8_t>& heatmap_bgra,
    const touchplus::tracking::TrackingResult& r) {

    if (!r.hand_valid || r.fingertip_valid || r.pixel_x < 0 || r.pixel_y < 0) return;
    if (heatmap_bgra.size() < static_cast<size_t>(kDepthWidth) * kDepthHeight * 4) return;

    const int gx = r.pixel_x / kDepthScale;
    const int gy = r.pixel_y / kDepthScale;
    auto set_white = [&](int x, int y) {
        if (x < 0 || x >= kDepthWidth || y < 0 || y >= kDepthHeight) return;
        const size_t i = (static_cast<size_t>(y) * kDepthWidth + x) * 4;
        heatmap_bgra[i + 0] = 255;
        heatmap_bgra[i + 1] = 255;
        heatmap_bgra[i + 2] = 255;
        heatmap_bgra[i + 3] = 255;
    };
    for (int d = -5; d <= 5; ++d) {
        set_white(gx + d, gy);
        set_white(gx, gy + d);
    }
}

inline void maybe_report(RuntimeState& s) {
    announce_once(s);
    ++s.report_counter;
    if (s.report_counter % 30 != 0) return;

    if (!s.enabled) {
        std::cout << "[TRACK] heartbeat | tracker=DISABLED | press T to enable\n";
        return;
    }
    if (!touchplus::surface::live_surface_model().valid) {
        std::cout << "[TRACK] heartbeat | tracker=ENABLED | waiting for valid surface/<serial>.json\n";
        return;
    }
    if (s.tracker.background_learning()) {
        std::cout << "[TRACK] heartbeat | background=LEARNING "
                  << s.tracker.background_frames() << "/"
                  << touchplus::tracking::kV5BackgroundFrames
                  << " | keep work area clear and still\n";
        return;
    }
    if (!s.tracker.background_ready()) {
        std::cout << "[TRACK] heartbeat | background=NOT_READY | clear work area and press B\n";
        return;
    }

    const auto& r = s.result;
    if (!r.hand_valid) {
        std::cout << "[TRACK] heartbeat | background=READY | no supported changed hand silhouette"
                  << " | changed_cells=" << r.foreground_samples << "\n";
        return;
    }
    if (!r.fingertip_valid) {
        std::cout << "[TRACK] heartbeat | background=READY | silhouette=" << r.hand_samples
                  << " cells | tip_pixel=" << r.pixel_x << "," << r.pixel_y
                  << " | fingertip=unknown"
                  << " | refinement=" << r.refinement_support
                  << " | confidence=" << r.confidence << "\n";
        return;
    }

    std::cout << std::fixed << std::setprecision(1)
              << "[TRACK] heartbeat | background=READY | silhouette=" << r.hand_samples
              << " cells | fingertip surface XYZ=("
              << r.smoothed_tip.x_mm << ", "
              << r.smoothed_tip.y_mm << ", H="
              << r.smoothed_tip.h_mm << ") mm"
              << " | tip_pixel=" << r.pixel_x << "," << r.pixel_y
              << " | support=" << r.refinement_support
              << " | confidence=" << r.confidence << "\n";
}

} // namespace tracking_runtime_detail

inline void compute_depth_heatmap_fingertip_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    DepthWorkspace& workspace) {

    // First run the already-accepted Phase 1C dense diagnostic unchanged.
    touchplus::depth::compute_depth_heatmap(c, left, right, workspace);

    auto& runtime = tracking_runtime_detail::state();
    if (!runtime.enabled) return;

    const auto& surface = touchplus::surface::live_surface_model();
    if (!surface.valid) {
        runtime.result = {};
        return;
    }

    runtime.result = runtime.tracker.update(c, surface, left, right, workspace);
    touchplus::tracking::overlay_tracking(
        workspace.heatmap_bgra,
        runtime.tracker.selected_mask(),
        runtime.result);
    tracking_runtime_detail::overlay_coarse_candidate(
        workspace.heatmap_bgra,
        runtime.result);
}

inline PointDepth point_depth_surface_runtime_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    surface_runtime_detail::handle_undo();

    // Phase 2B controls are serviced on the every-frame point path so T/B do
    // not depend on whether the half-rate dense-depth display is currently on.
    auto& tracking = tracking_runtime_detail::state();
    tracking_runtime_detail::maybe_toggle(tracking);
    tracking_runtime_detail::maybe_background(tracking);

    PointDepth result = touchplus::surface::point_depth_surface_wrapper(
        c, left, right, cursor_x, cursor_y);
    tracking_runtime_detail::maybe_report(tracking);
    return result;
}

} // namespace touchplus::depth

// Keep depth_viewer.cpp on its proven persistent capture/render path. Redirect
// only the two extension seams needed by Phase 2A/2B.
#define point_depth point_depth_surface_runtime_wrapper
#define compute_depth_heatmap compute_depth_heatmap_fingertip_wrapper
