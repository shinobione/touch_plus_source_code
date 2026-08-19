#pragma once

#include "depth_point_robust.h"
#include "surface_frame_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace touchplus::tracking {

constexpr double kMinForegroundHmm = 6.0;
constexpr double kMaxForegroundHmm = 260.0;
constexpr size_t kMinHandSamples = 24;
constexpr double kMaxTemporalJumpMm = 140.0;

struct GridSample {
    int gx = 0;
    int gy = 0;
    touchplus::surface::SurfacePoint surface{};
    double disparity_px = 0.0;
};

struct ComponentAnalysis {
    bool valid = false;
    size_t foreground_samples = 0;
    size_t component_samples = 0;
    int tip_gx = -1;
    int tip_gy = -1;
    touchplus::surface::SurfacePoint coarse_tip{};
    std::vector<uint8_t> selected_mask;
};

struct TrackingResult {
    bool hand_valid = false;
    bool fingertip_valid = false;
    size_t foreground_samples = 0;
    size_t hand_samples = 0;
    int pixel_x = -1;
    int pixel_y = -1;
    touchplus::surface::SurfacePoint raw_tip{};
    touchplus::surface::SurfacePoint smoothed_tip{};
    int refinement_support = 0;
    std::string confidence = "UNKNOWN";
};

inline double sqr(double value) { return value * value; }

inline ComponentAnalysis analyze_surface_samples(
    const std::vector<GridSample>& samples,
    int width,
    int height) {

    ComponentAnalysis out;
    if (width <= 0 || height <= 0 || samples.empty()) {
        return out;
    }

    const size_t cell_count = static_cast<size_t>(width) * height;
    std::vector<int> sample_index(cell_count, -1);
    std::vector<uint8_t> raw(cell_count, 0);
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        if (s.gx < 0 || s.gx >= width || s.gy < 0 || s.gy >= height) continue;
        const size_t idx = static_cast<size_t>(s.gy) * width + s.gx;
        raw[idx] = 1;
        sample_index[idx] = static_cast<int>(i);
    }
    out.foreground_samples = samples.size();

    std::vector<uint8_t> grown(cell_count, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!raw[idx]) continue;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        grown[static_cast<size_t>(ny) * width + nx] = 1;
                    }
                }
            }
        }
    }

    std::vector<int> labels(cell_count, -1);
    std::vector<int> queue;
    queue.reserve(cell_count);
    int next_label = 0;
    int best_label = -1;
    size_t best_raw_count = 0;

    constexpr std::array<std::array<int, 2>, 8> neighbors{{
        {{-1,-1}}, {{0,-1}}, {{1,-1}}, {{-1,0}},
        {{1,0}}, {{-1,1}}, {{0,1}}, {{1,1}}
    }};

    for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            const size_t seed = static_cast<size_t>(sy) * width + sx;
            if (!grown[seed] || labels[seed] >= 0) continue;

            queue.clear();
            queue.push_back(static_cast<int>(seed));
            labels[seed] = next_label;
            size_t head = 0;
            size_t raw_count = 0;

            while (head < queue.size()) {
                const int flat = queue[head++];
                const int y = flat / width;
                const int x = flat - y * width;
                if (raw[static_cast<size_t>(flat)]) ++raw_count;

                for (const auto& n : neighbors) {
                    const int nx = x + n[0];
                    const int ny = y + n[1];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    const size_t ni = static_cast<size_t>(ny) * width + nx;
                    if (grown[ni] && labels[ni] < 0) {
                        labels[ni] = next_label;
                        queue.push_back(static_cast<int>(ni));
                    }
                }
            }

            if (raw_count > best_raw_count) {
                best_raw_count = raw_count;
                best_label = next_label;
            }
            ++next_label;
        }
    }

    if (best_label < 0 || best_raw_count < kMinHandSamples) {
        return out;
    }

    out.selected_mask.assign(cell_count, 0);
    std::vector<size_t> selected_samples;
    selected_samples.reserve(best_raw_count);
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (!raw[idx] || labels[idx] != best_label) continue;
        out.selected_mask[idx] = 1;
        const int si = sample_index[idx];
        if (si >= 0) selected_samples.push_back(static_cast<size_t>(si));
    }
    if (selected_samples.size() < kMinHandSamples) return out;

    double cx = 0.0;
    double cy = 0.0;
    double min_h = std::numeric_limits<double>::infinity();
    double max_h = -std::numeric_limits<double>::infinity();
    for (const size_t si : selected_samples) {
        const auto& p = samples[si].surface;
        cx += p.x_mm;
        cy += p.y_mm;
        min_h = std::min(min_h, p.h_mm);
        max_h = std::max(max_h, p.h_mm);
    }
    cx /= static_cast<double>(selected_samples.size());
    cy /= static_cast<double>(selected_samples.size());

    double max_radius = 1e-6;
    for (const size_t si : selected_samples) {
        const auto& p = samples[si].surface;
        max_radius = std::max(max_radius, std::sqrt(sqr(p.x_mm - cx) + sqr(p.y_mm - cy)));
    }

    double best_score = -1.0;
    size_t best_si = selected_samples.front();
    const double h_span = std::max(5.0, max_h - min_h);

    for (const size_t si : selected_samples) {
        const auto& s = samples[si];
        const auto& p = s.surface;
        const double radius = std::sqrt(sqr(p.x_mm - cx) + sqr(p.y_mm - cy));
        if (radius < max_radius * 0.45) continue;

        int neighbor_count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = s.gx + dx;
                const int ny = s.gy + dy;
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                if (out.selected_mask[static_cast<size_t>(ny) * width + nx]) ++neighbor_count;
            }
        }
        const double boundary_bonus = neighbor_count <= 5 ? 0.08 : 0.0;
        const double radial_score = std::clamp(radius / max_radius, 0.0, 1.0);
        const double low_height_score = std::clamp(1.0 - (p.h_mm - min_h) / h_span, 0.0, 1.0);
        const double score = 0.72 * radial_score + 0.28 * low_height_score + boundary_bonus;
        if (score > best_score) {
            best_score = score;
            best_si = si;
        }
    }

    const auto& tip = samples[best_si];
    out.valid = true;
    out.component_samples = selected_samples.size();
    out.tip_gx = tip.gx;
    out.tip_gy = tip.gy;
    out.coarse_tip = tip.surface;
    return out;
}

inline touchplus::surface::SurfacePoint median_surface_point(
    const std::vector<touchplus::surface::SurfacePoint>& points) {
    std::vector<double> xs, ys, hs;
    xs.reserve(points.size()); ys.reserve(points.size()); hs.reserve(points.size());
    for (const auto& p : points) {
        xs.push_back(p.x_mm); ys.push_back(p.y_mm); hs.push_back(p.h_mm);
    }
    return {
        touchplus::surface::median(std::move(xs)),
        touchplus::surface::median(std::move(ys)),
        touchplus::surface::median(std::move(hs))
    };
}

class FingertipTracker {
public:
    TrackingResult update(
        const touchplus::depth::Calibration& calibration,
        const touchplus::surface::SurfaceModel& surface,
        const std::vector<uint8_t>& left_gray,
        const std::vector<uint8_t>& right_gray,
        const touchplus::depth::DepthWorkspace& workspace) {

        TrackingResult out;
        if (!surface.valid || left_gray.size() < static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight ||
            right_gray.size() < static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight) {
            last_result_ = out;
            return out;
        }

        std::vector<GridSample> samples;
        samples.reserve(static_cast<size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight / 6);
        constexpr int radius = 2;
        constexpr int area = (radius * 2 + 1) * (radius * 2 + 1);
        constexpr int max_average_cost = 55;
        constexpr double uniqueness = 1.04;
        constexpr int inf = std::numeric_limits<int>::max() / 4;

        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx = static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                const int d_small = workspace.best_disp[idx];
                const bool dense_valid = d_small > 0 &&
                    workspace.best_cost[idx] <= max_average_cost * area &&
                    (workspace.second_cost[idx] == inf ||
                     static_cast<double>(workspace.second_cost[idx]) >=
                        static_cast<double>(workspace.best_cost[idx]) * uniqueness);
                if (!dense_valid) continue;

                const double disparity = static_cast<double>(d_small * touchplus::depth::kDepthScale);
                const double u = gx * touchplus::depth::kDepthScale + 0.5;
                const double v = gy * touchplus::depth::kDepthScale + 0.5;
                const auto camera = touchplus::surface::camera_point_from_q(calibration, u, v, disparity);
                if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
                const auto sp = touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) || sp.h_mm < kMinForegroundHmm || sp.h_mm > kMaxForegroundHmm) continue;
                samples.push_back({gx, gy, sp, disparity});
            }
        }

        ComponentAnalysis analysis = analyze_surface_samples(
            samples, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);
        selected_mask_ = std::move(analysis.selected_mask);
        out.foreground_samples = analysis.foreground_samples;
        out.hand_samples = analysis.component_samples;
        if (!analysis.valid) {
            ++missing_frames_;
            last_result_ = out;
            return out;
        }
        out.hand_valid = true;

        const int px = analysis.tip_gx * touchplus::depth::kDepthScale + 1;
        const int py = analysis.tip_gy * touchplus::depth::kDepthScale + 1;
        out.pixel_x = px;
        out.pixel_y = py;
        out.raw_tip = analysis.coarse_tip;

        double coarse_disp = 0.0;
        for (const auto& s : samples) {
            if (s.gx == analysis.tip_gx && s.gy == analysis.tip_gy) {
                coarse_disp = s.disparity_px;
                break;
            }
        }

        std::vector<touchplus::surface::SurfacePoint> refined;
        constexpr std::array<std::array<int, 2>, 9> offsets{{
            {{0,0}}, {{-2,0}}, {{2,0}}, {{0,-2}}, {{0,2}},
            {{-2,-2}}, {{2,-2}}, {{-2,2}}, {{2,2}}
        }};
        const int min_d = std::max(
            touchplus::depth::robust_point_detail::kMinDisparity,
            static_cast<int>(std::floor(coarse_disp - 8.0)));
        const int max_d = std::min(
            touchplus::depth::robust_point_detail::kMaxDisparity,
            static_cast<int>(std::ceil(coarse_disp + 8.0)));

        for (const auto& off : offsets) {
            const int sx = px + off[0];
            const int sy = py + off[1];
            if (sx < 12 || sx >= touchplus::depth::kEyeWidth - 5 || sy < 5 || sy >= touchplus::depth::kEyeHeight - 5) continue;
            const auto match = touchplus::depth::robust_point_detail::mutually_consistent_match(
                left_gray, right_gray, sx, sy, min_d, max_d);
            if (!match.valid) continue;
            const auto camera = touchplus::surface::camera_point_from_q(
                calibration, static_cast<double>(sx), static_cast<double>(sy), match.disparity);
            if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
            const auto sp = touchplus::surface::to_surface(surface, camera);
            if (!std::isfinite(sp.h_mm) || sp.h_mm < 2.0 || sp.h_mm > kMaxForegroundHmm + 40.0) continue;
            const double planar_delta = std::sqrt(
                sqr(sp.x_mm - analysis.coarse_tip.x_mm) + sqr(sp.y_mm - analysis.coarse_tip.y_mm));
            if (planar_delta <= 30.0) refined.push_back(sp);
        }

        out.refinement_support = static_cast<int>(refined.size());
        if (refined.size() < 3) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        out.raw_tip = median_surface_point(refined);
        const bool strong_component = out.hand_samples >= 60;
        out.confidence = (strong_component && refined.size() >= 5) ? "HIGH" : "MEDIUM";

        if (have_smoothed_) {
            const double jump = std::sqrt(
                sqr(out.raw_tip.x_mm - smoothed_.x_mm) +
                sqr(out.raw_tip.y_mm - smoothed_.y_mm) +
                sqr(out.raw_tip.h_mm - smoothed_.h_mm));
            if (jump > kMaxTemporalJumpMm && missing_frames_ < 3) {
                out.confidence = "LOW";
                ++missing_frames_;
                last_result_ = out;
                return out;
            }
            constexpr double alpha = 0.35;
            smoothed_.x_mm = smoothed_.x_mm * (1.0 - alpha) + out.raw_tip.x_mm * alpha;
            smoothed_.y_mm = smoothed_.y_mm * (1.0 - alpha) + out.raw_tip.y_mm * alpha;
            smoothed_.h_mm = smoothed_.h_mm * (1.0 - alpha) + out.raw_tip.h_mm * alpha;
        } else {
            smoothed_ = out.raw_tip;
            have_smoothed_ = true;
        }

        missing_frames_ = 0;
        out.smoothed_tip = smoothed_;
        out.fingertip_valid = out.confidence == "HIGH" || out.confidence == "MEDIUM";
        last_result_ = out;
        return out;
    }

    void clear() {
        selected_mask_.clear();
        last_result_ = {};
        have_smoothed_ = false;
        missing_frames_ = 0;
    }

    const TrackingResult& last_result() const { return last_result_; }
    const std::vector<uint8_t>& selected_mask() const { return selected_mask_; }

private:
    TrackingResult last_result_{};
    std::vector<uint8_t> selected_mask_;
    bool have_smoothed_ = false;
    int missing_frames_ = 0;
    touchplus::surface::SurfacePoint smoothed_{};
};

inline void overlay_tracking(
    std::vector<uint8_t>& heatmap_bgra,
    const std::vector<uint8_t>& selected_mask,
    const TrackingResult& result) {

    if (heatmap_bgra.size() < static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight * 4) return;
    if (selected_mask.size() == static_cast<size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight) {
        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                if (!selected_mask[static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx]) continue;
                for (int dy = 0; dy < touchplus::depth::kDepthScale; ++dy) {
                    for (int dx = 0; dx < touchplus::depth::kDepthScale; ++dx) {
                        const int x = gx * touchplus::depth::kDepthScale + dx;
                        const int y = gy * touchplus::depth::kDepthScale + dy;
                        const size_t dst = (static_cast<size_t>(y) * touchplus::depth::kEyeWidth + x) * 4;
                        heatmap_bgra[dst + 0] = 40;
                        heatmap_bgra[dst + 1] = 210;
                        heatmap_bgra[dst + 2] = 255;
                        heatmap_bgra[dst + 3] = 255;
                    }
                }
            }
        }
    }

    if (!result.fingertip_valid || result.pixel_x < 0 || result.pixel_y < 0) return;
    for (int d = -10; d <= 10; ++d) {
        const std::array<std::pair<int,int>, 2> pixels{{
            {result.pixel_x + d, result.pixel_y},
            {result.pixel_x, result.pixel_y + d}
        }};
        for (const auto& p : pixels) {
            if (p.first < 0 || p.first >= touchplus::depth::kEyeWidth || p.second < 0 || p.second >= touchplus::depth::kEyeHeight) continue;
            const size_t dst = (static_cast<size_t>(p.second) * touchplus::depth::kEyeWidth + p.first) * 4;
            heatmap_bgra[dst + 0] = 255;
            heatmap_bgra[dst + 1] = 70;
            heatmap_bgra[dst + 2] = 255;
            heatmap_bgra[dst + 3] = 255;
        }
    }
}

} // namespace touchplus::tracking
