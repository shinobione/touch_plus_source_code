#pragma once

// Preserve the proven Phase 1C/2A include order first: depth_probe_lock.h brings
// in the hardened point matcher, then Phase 2A replaces only the surface fitter.
#include "depth_probe_lock.h"
#include "surface_frame_robust.h"
#include "depth_surface_frame.h"
#include "fingertip_tracker.h"

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
    std::uint64_t report_counter = 0;
    touchplus::tracking::FingertipTracker tracker;
    touchplus::tracking::TrackingResult result;
};

inline RuntimeState& state() {
    static thread_local RuntimeState value;
    return value;
}

inline bool toggle_requested(RuntimeState& s) {
    const bool down = (GetAsyncKeyState('T') & 0x8000) != 0;
    const bool rising = down && !s.previous_t_down;
    s.previous_t_down = down;
    return rising;
}

inline void maybe_toggle(RuntimeState& s) {
    if (!toggle_requested(s)) return;
    s.enabled = !s.enabled;
    if (!s.enabled) {
        s.tracker.clear();
        s.result = {};
    }
    std::cout << "[TRACK] Phase 2B geometry tracker "
              << (s.enabled ? "ENABLED" : "DISABLED")
              << " (T toggles).\n";
}

inline void maybe_report(RuntimeState& s) {
    if (!s.enabled) return;
    ++s.report_counter;
    if (s.report_counter % 30 != 0) return;

    const auto& r = s.result;
    if (!touchplus::surface::live_surface_model().valid) {
        std::cout << "[TRACK] waiting for valid surface/<serial>.json\n";
        return;
    }
    if (!r.hand_valid) {
        std::cout << "[TRACK] no above-plane hand candidate"
                  << " | foreground=" << r.foreground_samples << "\n";
        return;
    }
    if (!r.fingertip_valid) {
        std::cout << "[TRACK] hand=" << r.hand_samples
                  << " cells | fingertip=unknown"
                  << " | refinement=" << r.refinement_support
                  << " | confidence=" << r.confidence << "\n";
        return;
    }

    std::cout << std::fixed << std::setprecision(1)
              << "[TRACK] hand=" << r.hand_samples
              << " cells | fingertip surface XYZ=("
              << r.smoothed_tip.x_mm << ", "
              << r.smoothed_tip.y_mm << ", H="
              << r.smoothed_tip.h_mm << ") mm"
              << " | pixel=" << r.pixel_x << "," << r.pixel_y
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
    tracking_runtime_detail::maybe_toggle(runtime);
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
}

inline PointDepth point_depth_surface_runtime_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    surface_runtime_detail::handle_undo();
    PointDepth result = touchplus::surface::point_depth_surface_wrapper(
        c, left, right, cursor_x, cursor_y);
    tracking_runtime_detail::maybe_report(tracking_runtime_detail::state());
    return result;
}

} // namespace touchplus::depth

// Keep depth_viewer.cpp on its proven persistent capture/render path. Redirect
// only the two extension seams needed by Phase 2A/2B.
#define point_depth point_depth_surface_runtime_wrapper
#define compute_depth_heatmap compute_depth_heatmap_fingertip_wrapper
