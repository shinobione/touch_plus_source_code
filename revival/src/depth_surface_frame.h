#pragma once

// Phase 2A wrapper layered on top of the hardened cursor matcher and the P-key
// locked diagnostic. It turns camera XYZ into a persistent working-surface
// frame without changing K/D/R/T/P/Q.
//
// C: capture one surface point (~1.5 s at a fixed image coordinate)
// F: robustly fit/save the working plane from >=6 points
// R: reset pending surface points (saved model is left untouched)
// H: print camera XYZ plus surface X/Y/height for the current point
//
// Surface height H is signed perpendicular distance from the fitted plane.
// The normal is oriented toward the camera, so H > 0 means above the table.

#include "depth_probe_lock.h"
#include "surface_frame_math.h"

#ifdef point_depth
#undef point_depth
#endif

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace touchplus::surface {
namespace live_detail {

constexpr std::uint64_t kCaptureFrames = 45;
constexpr std::uint64_t kMinCaptureValid = 12;
constexpr double kMinCaptureValidPercent = 25.0;

struct State {
    bool initialized = false;
    bool capturing = false;
    bool previous_c_down = false;
    bool previous_f_down = false;
    bool previous_r_down = false;
    bool previous_h_down = false;
    int capture_x = -1;
    int capture_y = -1;
    std::uint64_t capture_frames = 0;
    std::uint64_t capture_valid = 0;
    std::vector<Vec3> capture_points;
    std::vector<Vec3> pending_points;
    SurfaceModel model;
    std::filesystem::path model_path;
};

inline State& state() {
    static thread_local State value;
    return value;
}

inline std::filesystem::path executable_directory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed for surface model path");
    }
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

inline std::filesystem::path surface_model_path(const std::string& serial) {
    return executable_directory() / L"surface" /
        (std::wstring(serial.begin(), serial.end()) + L".json");
}

inline bool rising(int key, bool& previous) {
    const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
    const bool result = down && !previous;
    previous = down;
    return result;
}

inline void ensure_initialized(State& s, const touchplus::depth::Calibration& c) {
    if (s.initialized) return;
    s.initialized = true;
    s.model_path = surface_model_path(c.serial);

    if (std::filesystem::exists(s.model_path)) {
        try {
            s.model = load_surface_model(s.model_path, c.serial);
            std::cout << "[SURFACE] Loaded saved surface frame: " << s.model_path.string() << "\n";
            std::cout << "[SURFACE] H is now available. Press H on a textured point to print Xsurface/Ysurface/H.\n";
        } catch (const std::exception& error) {
            std::cout << "[SURFACE] Existing surface model rejected: " << error.what() << "\n";
        }
    } else {
        std::cout << "[SURFACE] No saved surface frame for serial " << c.serial << ".\n";
        std::cout << "[SURFACE] Sample >=8 well-spread textured table points with C, then press F.\n";
        std::cout << "[SURFACE] Keys: C=capture point | F=fit/save | R=reset pending | H=measure height.\n";
    }
}

inline Vec3 median_point(const std::vector<Vec3>& points) {
    std::vector<double> xs, ys, zs;
    xs.reserve(points.size()); ys.reserve(points.size()); zs.reserve(points.size());
    for (const Vec3& p : points) {
        xs.push_back(p.x); ys.push_back(p.y); zs.push_back(p.z);
    }
    return {median(std::move(xs)), median(std::move(ys)), median(std::move(zs))};
}

inline void begin_capture(State& s, int x, int y) {
    if (s.capturing) {
        std::cout << "[SURFACE] A point capture is already running.\n";
        return;
    }
    s.capturing = true;
    s.capture_x = x;
    s.capture_y = y;
    s.capture_frames = 0;
    s.capture_valid = 0;
    s.capture_points.clear();

    auto& temporal = touchplus::depth::robust_point_detail::temporal_state();
    temporal.x = x;
    temporal.y = y;
    temporal.disparities.clear();
    temporal.z_values.clear();

    std::cout << "\n[SURFACE] Capturing point at x=" << x << " y=" << y
              << " for " << kCaptureFrames << " frames. Keep camera + surface still...\n";
}

inline void finish_capture(State& s) {
    const double valid_percent = s.capture_frames > 0
        ? 100.0 * static_cast<double>(s.capture_valid) / static_cast<double>(s.capture_frames)
        : 0.0;

    if (s.capture_valid < kMinCaptureValid || valid_percent < kMinCaptureValidPercent ||
        s.capture_points.empty()) {
        std::cout << "[SURFACE] REJECTED point x=" << s.capture_x << " y=" << s.capture_y
                  << " | valid=" << s.capture_valid << "/" << s.capture_frames
                  << " (" << std::fixed << std::setprecision(1) << valid_percent
                  << "%). Move onto stronger texture and press C again.\n";
    } else {
        const Vec3 point = median_point(s.capture_points);
        s.pending_points.push_back(point);
        std::cout << "[SURFACE] ADDED point #" << s.pending_points.size()
                  << " | valid=" << s.capture_valid << "/" << s.capture_frames
                  << " (" << std::fixed << std::setprecision(1) << valid_percent << "%)"
                  << " | camera XYZ=(" << std::setprecision(1)
                  << point.x << ", " << point.y << ", " << point.z << ") mm\n";
        if (s.pending_points.size() < 8) {
            std::cout << "[SURFACE] Keep sampling across LEFT/RIGHT/TOP/BOTTOM of the intended working area.\n";
        } else {
            std::cout << "[SURFACE] Enough points for a good first fit. Press F when coverage is broad.\n";
        }
    }

    s.capturing = false;
    s.capture_frames = 0;
    s.capture_valid = 0;
    s.capture_points.clear();
}

inline void fit_and_save(State& s, const touchplus::depth::Calibration& c) {
    if (s.capturing) {
        std::cout << "[SURFACE] Wait for the current C capture to finish before fitting.\n";
        return;
    }
    try {
        SurfaceModel model = fit_surface_robust(c.serial, s.pending_points);
        const char* grade = confidence(model);
        const double angle_from_optical_deg =
            std::acos(std::clamp(dot(model.normal_camera, Vec3{0.0, 0.0, -1.0}), -1.0, 1.0))
            * 180.0 / 3.14159265358979323846;

        std::cout << "\n======= SURFACE FRAME FIT =======\n"
                  << "samples / inliers : " << model.sample_count << " / " << model.inlier_count << "\n"
                  << std::fixed << std::setprecision(3)
                  << "plane RMS         : " << model.fit_rms_mm << " mm\n"
                  << "plane max residual: " << model.fit_max_mm << " mm\n"
                  << std::setprecision(1)
                  << "coverage X / Y    : " << model.spread_x_mm << " / " << model.spread_y_mm << " mm\n"
                  << "normal vs -Z      : " << angle_from_optical_deg << " deg\n"
                  << "confidence        : " << grade << "\n";

        if (std::string(grade) == "LOW") {
            std::cout << "SURFACE FRAME RESULT: NOT SAVED — spread points wider / replace bad samples.\n"
                      << "Press R to reset pending samples and try again.\n";
            return;
        }

        save_surface_model(s.model_path, model);
        s.model = model;
        std::cout << "saved             : " << s.model_path.string() << "\n"
                  << "SURFACE FRAME RESULT: PASS / SAVED\n"
                  << "H=0 is the fitted working plane; H>0 points toward the camera.\n"
                  << "Press H on textured table / hand / object points to inspect surface-relative height.\n\n";
    } catch (const std::exception& error) {
        std::cout << "[SURFACE] FIT FAILED: " << error.what() << "\n";
    }
}

inline void print_height(
    const State& s,
    const touchplus::depth::Calibration& c,
    const touchplus::depth::PointDepth& result,
    int x,
    int y) {

    if (!s.model.valid) {
        std::cout << "[SURFACE] No valid surface frame. Capture points with C, then press F.\n";
        return;
    }
    if (!result.valid) {
        std::cout << "[SURFACE] H measurement invalid at x=" << x << " y=" << y
                  << ". Move onto texture.\n";
        return;
    }
    const Vec3 camera = camera_point_from_q(c, static_cast<double>(x), static_cast<double>(y), result.disparity_px);
    const SurfacePoint p = to_surface(s.model, camera);
    std::cout << "[SURFACE H] pixel=" << x << "," << y
              << std::fixed << std::setprecision(1)
              << " | camera XYZ=(" << camera.x << ", " << camera.y << ", " << camera.z << ") mm"
              << " | surface XYZ=(" << p.x_mm << ", " << p.y_mm << ", H=" << p.h_mm << ") mm\n";
}

} // namespace live_detail

inline touchplus::depth::PointDepth point_depth_surface_wrapper(
    const touchplus::depth::Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    using namespace live_detail;
    State& s = state();
    ensure_initialized(s, c);

    const bool c_pressed = rising('C', s.previous_c_down);
    const bool f_pressed = rising('F', s.previous_f_down);
    const bool r_pressed = rising('R', s.previous_r_down);
    const bool h_pressed = rising('H', s.previous_h_down);

    if (r_pressed) {
        s.capturing = false;
        s.capture_points.clear();
        s.pending_points.clear();
        std::cout << "[SURFACE] Pending calibration points RESET. Saved surface file is unchanged.\n";
    }
    if (c_pressed) {
        begin_capture(s, cursor_x, cursor_y);
    }
    if (f_pressed) {
        fit_and_save(s, c);
    }

    int sample_x = cursor_x;
    int sample_y = cursor_y;
    touchplus::depth::PointDepth result;

    if (s.capturing) {
        sample_x = s.capture_x;
        sample_y = s.capture_y;
        // Bypass only the P-key diagnostic wrapper while C is gathering a
        // fixed surface point. The hardened NCC/LR/consensus/temporal matcher
        // remains exactly the same.
        result = touchplus::depth::point_depth(c, left, right, sample_x, sample_y);
        ++s.capture_frames;
        if (result.valid) {
            ++s.capture_valid;
            const Vec3 p = camera_point_from_q(
                c, static_cast<double>(sample_x), static_cast<double>(sample_y), result.disparity_px);
            if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)) {
                s.capture_points.push_back(p);
            }
        }
        if (s.capture_frames >= kCaptureFrames) {
            finish_capture(s);
        }
    } else {
        result = touchplus::depth::point_depth_probe_wrapper(c, left, right, cursor_x, cursor_y);
        const auto& probe = touchplus::depth::locked_probe_detail::state();
        if (probe.locked) {
            sample_x = probe.x;
            sample_y = probe.y;
        }
    }

    if (h_pressed) {
        print_height(s, c, result, sample_x, sample_y);
    }

    return result;
}

inline const SurfaceModel& live_surface_model() {
    return live_detail::state().model;
}

} // namespace touchplus::surface

// depth_viewer.cpp is parsed after this forced header. Keep the existing P-key
// wrapper behavior and add the Phase 2A C/F/R/H surface-frame layer on top.
#define point_depth touchplus::surface::point_depth_surface_wrapper
