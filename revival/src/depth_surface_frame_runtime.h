#pragma once

// Preserve the proven Phase 1C include order first: depth_probe_lock.h brings
// in depth_point_robust.h, which macro-renames the legacy point_depth before
// depth_math.h is parsed. Only after that do we replace the surface fitter.
#include "depth_probe_lock.h"
#include "surface_frame_robust.h"
#include "depth_surface_frame.h"

#ifdef point_depth
#undef point_depth
#endif

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

inline PointDepth point_depth_surface_runtime_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    surface_runtime_detail::handle_undo();
    return touchplus::surface::point_depth_surface_wrapper(
        c, left, right, cursor_x, cursor_y);
}

} // namespace touchplus::depth

// Keep depth_viewer.cpp unchanged: its existing touchplus::depth::point_depth(...)
// call is redirected to the Phase 2A wrapper in the same namespace.
#define point_depth point_depth_surface_runtime_wrapper
