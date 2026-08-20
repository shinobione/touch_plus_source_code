#pragma once

#include "fingertip_tracker.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.2 hardening for real scenes.
// The half-resolution dense disparity is integer-quantized and is deliberately
// diagnostic. Around a 350-600 mm working distance a one-cell disparity error
// can move the reconstructed point by several millimetres. A 6 mm foreground
// threshold therefore turns table quantization into a giant false component.
constexpr double kV2MinForegroundHmm = 18.0;
constexpr double kV2MaxForegroundHmm = 240.0;
constexpr size_t kV2MinHandSamples = 40;
constexpr size_t kV2MaxHandSamples = 12000;
constexpr double kV2MaxComponentSpanXmm = 340.0;
constexpr double kV2MaxComponentSpanYmm = 420.0;
constexpr double kV2LocalHeightToleranceMm = 24.0;
constexpr double kV2LocalDisparityTolerancePx = 6.0;

struct ComponentStatsV2 {
    int label = -1;
    size_t raw_count = 0;
    double min_x = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    double min_h = std::numeric_limits<double>::infinity();
    double max_h = -std::numeric_limits<double>::infinity();
};

inline bool plausible_component_v2(const ComponentStatsV2& c) {
    if (c.raw_count < kV2MinHandSamples || c.raw_count > kV2MaxHandSamples) return false;
    if (!std::isfinite(c.min_x) || !std::isfinite(c.max_x) ||
        !std::isfinite(c.min_y) || !std::isfinite(c.max_y)) return false;
    if ((c.max_x - c.min_x) > kV2MaxComponentSpanXmm) return false;
    if ((c.max_y - c.min_y) > kV2MaxComponentSpanYmm) return false;
    return true;
}

inline ComponentAnalysis analyze_surface_samples_v2(
    const std::vector<GridSample>& samples,
    int width,
    int height) {

    ComponentAnalysis out;
    if (width <= 0 || height <= 0 || samples.empty()) return out;

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

    // Fill genuine one-cell holes, but do NOT dilate every foreground pixel.
    // The old 3x3 dilation could bridge unrelated depth speckle into one huge
    // component spanning most of the work area.
    std::vector<uint8_t> connected = raw;
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (raw[idx]) continue;
            int neighbors = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (raw[static_cast<size_t>(y + dy) * width + (x + dx)]) ++neighbors;
                }
            }
            if (neighbors >= 5) connected[idx] = 1;
        }
    }

    constexpr std::array<std::array<int, 2>, 8> neighbors{{
        {{-1,-1}}, {{0,-1}}, {{1,-1}}, {{-1,0}},
        {{1,0}}, {{-1,1}}, {{0,1}}, {{1,1}}
    }};

    std::vector<int> labels(cell_count, -1);
    std::vector<int> queue;
    queue.reserve(cell_count);
    std::vector<ComponentStatsV2> components;

    int next_label = 0;
    for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            const size_t seed = static_cast<size_t>(sy) * width + sx;
            if (!connected[seed] || labels[seed] >= 0) continue;

            ComponentStatsV2 stats;
            stats.label = next_label;
            queue.clear();
            queue.push_back(static_cast<int>(seed));
            labels[seed] = next_label;
            size_t head = 0;

            while (head < queue.size()) {
                const int flat = queue[head++];
                const int y = flat / width;
                const int x = flat - y * width;
                if (raw[static_cast<size_t>(flat)]) {
                    ++stats.raw_count;
                    const int si = sample_index[static_cast<size_t>(flat)];
                    if (si >= 0) {
                        const auto& p = samples[static_cast<size_t>(si)].surface;
                        stats.min_x = std::min(stats.min_x, p.x_mm);
                        stats.max_x = std::max(stats.max_x, p.x_mm);
                        stats.min_y = std::min(stats.min_y, p.y_mm);
                        stats.max_y = std::max(stats.max_y, p.y_mm);
                        stats.min_h = std::min(stats.min_h, p.h_mm);
                        stats.max_h = std::max(stats.max_h, p.h_mm);
                    }
                }

                for (const auto& n : neighbors) {
                    const int nx = x + n[0];
                    const int ny = y + n[1];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    const size_t ni = static_cast<size_t>(ny) * width + nx;
                    if (connected[ni] && labels[ni] < 0) {
                        labels[ni] = next_label;
                        queue.push_back(static_cast<int>(ni));
                    }
                }
            }

            components.push_back(stats);
            ++next_label;
        }
    }

    int best_label = -1;
    size_t best_count = 0;
    for (const auto& c : components) {
        if (!plausible_component_v2(c)) continue;
        if (c.raw_count > best_count) {
            best_count = c.raw_count;
            best_label = c.label;
        }
    }
    if (best_label < 0) return out;

    out.selected_mask.assign(cell_count, 0);
    std::vector<size_t> selected_samples;
    selected_samples.reserve(best_count);
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (!raw[idx] || labels[idx] != best_label) continue;
        out.selected_mask[idx] = 1;
        const int si = sample_index[idx];
        if (si >= 0) selected_samples.push_back(static_cast<size_t>(si));
    }
    if (selected_samples.size() < kV2MinHandSamples) return out;

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
        if (radius < max_radius * 0.50) continue;

        int neighbor_count = 0;
        for (const auto& n : neighbors) {
            const int nx = s.gx + n[0];
            const int ny = s.gy + n[1];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (out.selected_mask[static_cast<size_t>(ny) * width + nx]) ++neighbor_count;
        }

        const double radial_score = std::clamp(radius / max_radius, 0.0, 1.0);
        const double low_height_score = std::clamp(1.0 - (p.h_mm - min_h) / h_span, 0.0, 1.0);
        const double boundary_score = 1.0 - static_cast<double>(neighbor_count) / 8.0;
        const double score = 0.60 * radial_score + 0.22 * low_height_score + 0.18 * boundary_score;
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

inline std::vector<GridSample> locally_consistent_samples_v2(
    const std::vector<GridSample>& candidates,
    int width,
    int height) {

    const size_t cell_count = static_cast<size_t>(width) * height;
    std::vector<int> lookup(cell_count, -1);
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& s = candidates[i];
        if (s.gx < 0 || s.gx >= width || s.gy < 0 || s.gy >= height) continue;
        lookup[static_cast<size_t>(s.gy) * width + s.gx] = static_cast<int>(i);
    }

    std::vector<GridSample> stable;
    stable.reserve(candidates.size());
    for (const auto& s : candidates) {
        int agreeing = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = s.gx + dx;
                const int ny = s.gy + dy;
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                const int ni = lookup[static_cast<size_t>(ny) * width + nx];
                if (ni < 0) continue;
                const auto& other = candidates[static_cast<size_t>(ni)];
                if (std::abs(other.surface.h_mm - s.surface.h_mm) <= kV2LocalHeightToleranceMm &&
                    std::abs(other.disparity_px - s.disparity_px) <= kV2LocalDisparityTolerancePx) {
                    ++agreeing;
                }
            }
        }
        if (agreeing >= 2) stable.push_back(s);
    }
    return stable;
}

class FingertipTrackerV2 {
public:
    TrackingResult update(
        const touchplus::depth::Calibration& calibration,
        const touchplus::surface::SurfaceModel& surface,
        const std::vector<uint8_t>& left_gray,
        const std::vector<uint8_t>& right_gray,
        const touchplus::depth::DepthWorkspace& workspace) {

        TrackingResult out;
        if (!surface.valid ||
            left_gray.size() < static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight ||
            right_gray.size() < static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight) {
            last_result_ = out;
            return out;
        }

        // The saved Phase 2A model currently reloads only the transform axes.
        // Use the accepted physical coverage as a finite ROI fallback instead
        // of treating the fitted plane as mathematically infinite.
        const double roi_half_x = surface.spread_x_mm >= 80.0
            ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
        const double roi_half_y = surface.spread_y_mm >= 80.0
            ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

        std::vector<GridSample> candidates;
        candidates.reserve(static_cast<size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight / 8);
        constexpr int radius = 2;
        constexpr int area = (radius * 2 + 1) * (radius * 2 + 1);
        constexpr int max_average_cost = 42;
        constexpr double uniqueness = 1.10;
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
                if (!std::isfinite(sp.h_mm) || sp.h_mm < kV2MinForegroundHmm || sp.h_mm > kV2MaxForegroundHmm) continue;
                if (std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
                candidates.push_back({gx, gy, sp, disparity});
            }
        }

        std::vector<GridSample> samples = locally_consistent_samples_v2(
            candidates, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);

        ComponentAnalysis analysis = analyze_surface_samples_v2(
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
        constexpr std::array<int, 5> offsets{{-6, -3, 0, 3, 6}};
        const int min_d = std::max(
            touchplus::depth::robust_point_detail::kMinDisparity,
            static_cast<int>(std::floor(coarse_disp - 12.0)));
        const int max_d = std::min(
            touchplus::depth::robust_point_detail::kMaxDisparity,
            static_cast<int>(std::ceil(coarse_disp + 12.0)));

        for (const int oy : offsets) {
            for (const int ox : offsets) {
                const int sx = px + ox;
                const int sy = py + oy;
                if (sx < 12 || sx >= touchplus::depth::kEyeWidth - 5 ||
                    sy < 5 || sy >= touchplus::depth::kEyeHeight - 5) continue;
                const auto match = touchplus::depth::robust_point_detail::mutually_consistent_match(
                    left_gray, right_gray, sx, sy, min_d, max_d);
                if (!match.valid) continue;
                const auto camera = touchplus::surface::camera_point_from_q(
                    calibration, static_cast<double>(sx), static_cast<double>(sy), match.disparity);
                if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
                const auto sp = touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) || sp.h_mm < 2.0 || sp.h_mm > kV2MaxForegroundHmm + 40.0) continue;
                if (std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
                const double planar_delta = std::sqrt(
                    sqr(sp.x_mm - analysis.coarse_tip.x_mm) + sqr(sp.y_mm - analysis.coarse_tip.y_mm));
                if (planar_delta <= 35.0) refined.push_back(sp);
            }
        }

        out.refinement_support = static_cast<int>(refined.size());
        if (refined.size() < 3) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        out.raw_tip = median_surface_point(refined);
        const bool strong_component = out.hand_samples >= 120 && out.hand_samples <= kV2MaxHandSamples;
        out.confidence = (strong_component && refined.size() >= 6) ? "HIGH" : "MEDIUM";

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

} // namespace touchplus::tracking
