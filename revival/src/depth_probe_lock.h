#pragma once

// Live-viewer diagnostic wrapper layered on top of the hardened point matcher.
// Press P to lock the current cursor point. While locked, the wrapper samples
// exactly that image coordinate and reports acceptance rate, median camera-Z,
// MAD and a coarse confidence class to the console. Press P again to stop and
// print the final summary. The core calibration and Q matrices are untouched.

#include "depth_point_robust.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace touchplus::depth {
namespace locked_probe_detail {

struct State {
    bool locked = false;
    bool previous_p_down = false;
    int x = -1;
    int y = -1;
    std::uint64_t samples = 0;
    std::uint64_t valid = 0;
    std::vector<double> z_values;
};

inline State& state() {
    static thread_local State value;
    return value;
}

inline double median(std::vector<double> values) {
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    double result = values[mid];
    if ((values.size() & 1U) == 0U) {
        const auto lower = std::max_element(values.begin(), values.begin() + mid);
        result = (*lower + result) * 0.5;
    }
    return result;
}

inline double mad(const std::vector<double>& values, double med) {
    if (values.empty() || !std::isfinite(med)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (const double value : values) {
        deviations.push_back(std::abs(value - med));
    }
    return median(std::move(deviations));
}

inline const char* confidence(double valid_percent, std::uint64_t valid_count, double mad_mm) {
    if (valid_count >= 12 && valid_percent >= 50.0 && std::isfinite(mad_mm) && mad_mm <= 8.0) {
        return "HIGH";
    }
    if (valid_count >= 6 && valid_percent >= 25.0 && std::isfinite(mad_mm) && mad_mm <= 20.0) {
        return "MEDIUM";
    }
    return "LOW";
}

inline void reset_hardened_temporal_history(int x, int y) {
    auto& temporal = robust_point_detail::temporal_state();
    temporal.x = x;
    temporal.y = y;
    temporal.disparities.clear();
    temporal.z_values.clear();
}

inline void print_summary(const State& s, bool final) {
    const double valid_percent = s.samples > 0
        ? 100.0 * static_cast<double>(s.valid) / static_cast<double>(s.samples)
        : 0.0;
    const double med = median(s.z_values);
    const double mad_mm = mad(s.z_values, med);

    std::cout << (final ? "[PROBE FINAL] " : "[PROBE] ")
              << "x=" << s.x << " y=" << s.y
              << " | samples=" << s.samples
              << " | valid=" << s.valid << " ("
              << std::fixed << std::setprecision(1) << valid_percent << "%)";

    if (std::isfinite(med)) {
        std::cout << " | median Z=" << std::setprecision(1) << med << " mm"
                  << " | MAD=" << std::setprecision(1) << mad_mm << " mm"
                  << " | confidence=" << confidence(valid_percent, s.valid, mad_mm);
    } else {
        std::cout << " | median Z=n/a | MAD=n/a | confidence=LOW";
    }
    std::cout << "\n";
}

inline void toggle_if_requested(State& s, int cursor_x, int cursor_y) {
    const bool p_down = (GetAsyncKeyState('P') & 0x8000) != 0;
    const bool rising = p_down && !s.previous_p_down;
    s.previous_p_down = p_down;
    if (!rising) {
        return;
    }

    if (s.locked) {
        print_summary(s, true);
        s.locked = false;
        std::cout << "[PROBE] UNLOCKED. Move the cursor, then press P to start another sample.\n";
        return;
    }

    s.locked = true;
    s.x = cursor_x;
    s.y = cursor_y;
    s.samples = 0;
    s.valid = 0;
    s.z_values.clear();
    reset_hardened_temporal_history(s.x, s.y);
    std::cout << "\n[PROBE] LOCKED at x=" << s.x << " y=" << s.y
              << ". Keep target and cursor still; press P again to finish.\n";
}

} // namespace locked_probe_detail

inline PointDepth point_depth_probe_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    auto& s = locked_probe_detail::state();
    locked_probe_detail::toggle_if_requested(s, cursor_x, cursor_y);

    const int sample_x = s.locked ? s.x : cursor_x;
    const int sample_y = s.locked ? s.y : cursor_y;
    const PointDepth result = point_depth(c, left, right, sample_x, sample_y);

    if (s.locked) {
        ++s.samples;
        if (result.valid) {
            ++s.valid;
            s.z_values.push_back(result.z_mm);
        }
        if ((s.samples % 15U) == 0U) {
            locked_probe_detail::print_summary(s, false);
        }
    }

    return result;
}

} // namespace touchplus::depth

// depth_viewer.cpp is parsed after this force-include. Redirect only its calls
// to the wrapper above; depth_point_robust.h itself was parsed before this macro.
#define point_depth point_depth_probe_wrapper
