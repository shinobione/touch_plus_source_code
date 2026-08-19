#pragma once

#include "depth_surface_frame.h"

#ifdef point_depth
#undef point_depth
#endif

namespace touchplus::depth {

inline PointDepth point_depth_surface_runtime_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {
    return touchplus::surface::point_depth_surface_wrapper(
        c, left, right, cursor_x, cursor_y);
}

} // namespace touchplus::depth

// Keep depth_viewer.cpp unchanged: its existing touchplus::depth::point_depth(...)
// call is redirected to the Phase 2A wrapper in the same namespace.
#define point_depth point_depth_surface_runtime_wrapper
