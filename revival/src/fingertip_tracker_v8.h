#pragma once

#include "fingertip_identity_v8.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.8 runtime integration.
//
// Accepted lower layers are intentionally reused unchanged:
// - V5 learned-background appearance silhouette;
// - V6 physical support bounding;
// - hardened Phase 1C full-resolution stereo matcher;
// - accepted calibration/Q and Phase 2A surface transform.
//
// V8 changes only the 2D identity boundary. Stereo refinement is not attempted
// until TemporalIdentityGateV8 reports a LOCKED branch identity.

class FingertipTrackerV8 {
public:
    void request_background_capture() {
        base_.request_background_capture();
        clear_tracking_only();
    }

    bool background_ready() const { return base_.background_ready(); }
    bool background_learning() const { return base_.background_learning(); }
    int background_frames() const { return base_.background_frames(); }

    TrackingResult update(
        const touchplus::depth::Calibration& calibration,
        const touchplus::surface::SurfaceModel& surface,
        const std::vector<uint8_t>& left_gray,
        const std::vector<uint8_t>& right_gray,
        const touchplus::depth::DepthWorkspace& workspace) {

        // V5 owns the accepted learned-background appearance silhouette.
        // Its endpoint/metric result is ignored here; V8 only consumes mask
        // and hand-presence plumbing, exactly as V7 did.
        const TrackingResult base_result =
            base_.update(calibration, surface, left_gray, right_gray, workspace);

        TrackingResult out;
        selected_mask_ = base_.selected_mask();
        out.foreground_samples = base_result.foreground_samples;
        last_identity_ = {};
        last_decision_ = {};
        identity_confidence_ = "LOW";
        stereo_confidence_ = "NOT_RUN";

        if (!base_.background_ready() ||
            selected_mask_.empty() ||
            !base_result.hand_valid) {

            if (!base_result.hand_valid) selected_mask_.clear();
            temporal_identity_.update({});
            reset_metric_if_identity_lost();
            last_result_ = out;
            return out;
        }

        const size_t depth_cells =
            static_cast<size_t>(touchplus::depth::kDepthWidth) *
            touchplus::depth::kDepthHeight;
        std::vector<uint8_t> support(depth_cells, 0);
        std::vector<int> support_disp_small(depth_cells, 0);

        const double roi_half_x = surface.spread_x_mm >= 80.0
            ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
        const double roi_half_y = surface.spread_y_mm >= 80.0
            ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

        constexpr int radius = 2;
        constexpr int area = (radius * 2 + 1) * (radius * 2 + 1);
        constexpr int max_average_cost = 44;
        constexpr double uniqueness = 1.08;
        constexpr int inf = std::numeric_limits<int>::max() / 4;

        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx =
                    static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                if (!selected_mask_[idx]) continue;

                const int d_small = workspace.best_disp[idx];
                const bool dense_valid = d_small > 0 &&
                    workspace.best_cost[idx] <= max_average_cost * area &&
                    (workspace.second_cost[idx] == inf ||
                     static_cast<double>(workspace.second_cost[idx]) >=
                        static_cast<double>(workspace.best_cost[idx]) * uniqueness);
                if (!dense_valid) continue;

                const double disparity =
                    static_cast<double>(d_small * touchplus::depth::kDepthScale);
                const double u =
                    gx * touchplus::depth::kDepthScale + 0.5;
                const double v =
                    gy * touchplus::depth::kDepthScale + 0.5;
                const auto camera =
                    touchplus::surface::camera_point_from_q(
                        calibration, u, v, disparity);
                if (!std::isfinite(camera.x) ||
                    !std::isfinite(camera.y) ||
                    !std::isfinite(camera.z)) {
                    continue;
                }

                const auto sp =
                    touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) ||
                    sp.h_mm < kV6MinSupportHmm ||
                    sp.h_mm > kV6MaxSupportHmm) {
                    continue;
                }
                if (std::abs(sp.x_mm) > roi_half_x ||
                    std::abs(sp.y_mm) > roi_half_y) {
                    continue;
                }

                support[idx] = 1;
                support_disp_small[idx] = d_small;
            }
        }

        const auto bounded = constrain_to_physical_support_v6(
            selected_mask_,
            support,
            touchplus::depth::kDepthWidth,
            touchplus::depth::kDepthHeight);

        if (!bounded.valid) {
            selected_mask_.clear();
            temporal_identity_.update({});
            reset_metric_if_identity_lost();
            last_result_ = out;
            return out;
        }

        selected_mask_ = bounded.mask;
        out.hand_samples = bounded.cells;
        out.hand_valid = true;

        last_identity_ = analyze_finger_identity_v8(
            selected_mask_,
            touchplus::depth::kDepthWidth,
            touchplus::depth::kDepthHeight);
        last_decision_ = temporal_identity_.update(last_identity_);
        identity_confidence_ = last_decision_.confidence;

        // Keep the 2D candidate visible diagnostically while acquiring or
        // rejecting. It is not allowed to become finite XYZ until locked.
        if (last_decision_.has_candidate) {
            out.pixel_x =
                last_decision_.tip_gx * touchplus::depth::kDepthScale + 1;
            out.pixel_y =
                last_decision_.tip_gy * touchplus::depth::kDepthScale + 1;
        }

        if (!last_decision_.publish) {
            out.confidence = "LOW";
            reset_metric_if_identity_lost();
            last_result_ = out;
            return out;
        }

        const std::uint64_t active_branch_id = last_decision_.branch_id;
        if (metric_branch_id_ != active_branch_id) {
            have_smoothed_ = false;
            missing_metric_frames_ = 0;
            smoothed_ = {};
            metric_branch_id_ = active_branch_id;
        }

        const int px = out.pixel_x;
        const int py = out.pixel_y;
        if (px < 0 || py < 0) {
            out.confidence = "LOW";
            last_result_ = out;
            return out;
        }

        int nearest_d_small = 0;
        int nearest_dist2 = std::numeric_limits<int>::max();
        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx =
                    static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                if (!support[idx] || !selected_mask_[idx]) continue;

                const int sx = gx - last_decision_.tip_gx;
                const int sy = gy - last_decision_.tip_gy;
                const int d2 = sx * sx + sy * sy;
                if (d2 < nearest_dist2) {
                    nearest_dist2 = d2;
                    nearest_d_small = support_disp_small[idx];
                }
            }
        }

        if (nearest_d_small <= 0 || nearest_dist2 > 44 * 44) {
            stereo_confidence_ = "LOW";
            out.confidence = "LOW";
            ++missing_metric_frames_;
            last_result_ = out;
            return out;
        }

        const double coarse_disp =
            static_cast<double>(
                nearest_d_small * touchplus::depth::kDepthScale);
        const int min_d = std::max(
            touchplus::depth::robust_point_detail::kMinDisparity,
            static_cast<int>(std::floor(coarse_disp - 18.0)));
        const int max_d = std::min(
            touchplus::depth::robust_point_detail::kMaxDisparity,
            static_cast<int>(std::ceil(coarse_disp + 18.0)));

        std::vector<touchplus::surface::SurfacePoint> refined;
        constexpr std::array<int, 7> offsets{{-12, -8, -4, 0, 4, 8, 12}};

        for (const int oy : offsets) {
            for (const int ox : offsets) {
                const int sx = px + ox;
                const int sy = py + oy;
                if (sx < 12 ||
                    sx >= touchplus::depth::kEyeWidth - 5 ||
                    sy < 5 ||
                    sy >= touchplus::depth::kEyeHeight - 5) {
                    continue;
                }

                const int sgx = sx / touchplus::depth::kDepthScale;
                const int sgy = sy / touchplus::depth::kDepthScale;
                if (!mask_near_v5(
                        selected_mask_,
                        touchplus::depth::kDepthWidth,
                        touchplus::depth::kDepthHeight,
                        sgx, sgy, 1)) {
                    continue;
                }

                const auto match =
                    touchplus::depth::robust_point_detail::mutually_consistent_match(
                        left_gray, right_gray, sx, sy, min_d, max_d);
                if (!match.valid) continue;

                const auto camera =
                    touchplus::surface::camera_point_from_q(
                        calibration,
                        static_cast<double>(sx),
                        static_cast<double>(sy),
                        match.disparity);
                if (!std::isfinite(camera.x) ||
                    !std::isfinite(camera.y) ||
                    !std::isfinite(camera.z)) {
                    continue;
                }

                const auto sp =
                    touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) ||
                    sp.h_mm < 2.0 ||
                    sp.h_mm > kV6MaxSupportHmm + 20.0 ||
                    std::abs(sp.x_mm) > roi_half_x ||
                    std::abs(sp.y_mm) > roi_half_y) {
                    continue;
                }
                refined.push_back(sp);
            }
        }

        if (!refined.empty()) {
            std::vector<double> hs;
            hs.reserve(refined.size());
            for (const auto& p : refined) hs.push_back(p.h_mm);
            const double median_h =
                touchplus::surface::median(std::move(hs));

            std::vector<touchplus::surface::SurfacePoint> consistent;
            consistent.reserve(refined.size());
            for (const auto& p : refined) {
                if (std::abs(p.h_mm - median_h) <= 22.0) {
                    consistent.push_back(p);
                }
            }
            refined = std::move(consistent);
        }

        out.refinement_support = static_cast<int>(refined.size());
        stereo_confidence_ =
            refined.size() >= 6 ? "HIGH" :
            refined.size() >= 3 ? "MEDIUM" : "LOW";

        if (!final_identity_stereo_gate_v8(
                identity_confidence_, stereo_confidence_)) {
            out.confidence = "LOW";
            ++missing_metric_frames_;
            last_result_ = out;
            return out;
        }

        out.raw_tip = median_surface_point(refined);

        if (have_smoothed_) {
            const double jump = std::sqrt(
                sqr(out.raw_tip.x_mm - smoothed_.x_mm) +
                sqr(out.raw_tip.y_mm - smoothed_.y_mm) +
                sqr(out.raw_tip.h_mm - smoothed_.h_mm));

            if (jump > 85.0 && missing_metric_frames_ < 3) {
                stereo_confidence_ = "LOW";
                out.confidence = "LOW";
                ++missing_metric_frames_;
                last_result_ = out;
                return out;
            }

            constexpr double alpha = 0.32;
            smoothed_.x_mm =
                smoothed_.x_mm * (1.0 - alpha) +
                out.raw_tip.x_mm * alpha;
            smoothed_.y_mm =
                smoothed_.y_mm * (1.0 - alpha) +
                out.raw_tip.y_mm * alpha;
            smoothed_.h_mm =
                smoothed_.h_mm * (1.0 - alpha) +
                out.raw_tip.h_mm * alpha;
        } else {
            smoothed_ = out.raw_tip;
            have_smoothed_ = true;
        }

        missing_metric_frames_ = 0;
        out.smoothed_tip = smoothed_;
        out.confidence =
            identity_confidence_ == "HIGH" &&
            stereo_confidence_ == "HIGH"
            ? "HIGH" : "MEDIUM";
        out.fingertip_valid = true;
        last_result_ = out;
        return out;
    }

    void clear() {
        base_.clear();
        clear_tracking_only();
    }

    const TrackingResult& last_result() const { return last_result_; }
    const std::vector<uint8_t>& selected_mask() const { return selected_mask_; }
    const IdentityObservationV8& last_identity() const { return last_identity_; }
    const IdentityDecisionV8& last_decision() const { return last_decision_; }
    const std::string& identity_confidence() const {
        return identity_confidence_;
    }
    const std::string& stereo_confidence() const {
        return stereo_confidence_;
    }

private:
    void reset_metric_if_identity_lost() {
        if (last_decision_.state != IdentityStateV8::Locked) {
            have_smoothed_ = false;
            missing_metric_frames_ = 0;
            smoothed_ = {};
            metric_branch_id_ = 0;
        }
    }

    void clear_tracking_only() {
        selected_mask_.clear();
        last_result_ = {};
        last_identity_ = {};
        last_decision_ = {};
        temporal_identity_.clear();
        identity_confidence_ = "LOW";
        stereo_confidence_ = "NOT_RUN";
        have_smoothed_ = false;
        missing_metric_frames_ = 0;
        metric_branch_id_ = 0;
        smoothed_ = {};
    }

    FingertipTrackerV5 base_;
    TemporalIdentityGateV8 temporal_identity_;
    TrackingResult last_result_{};
    IdentityObservationV8 last_identity_{};
    IdentityDecisionV8 last_decision_{};
    std::vector<uint8_t> selected_mask_;
    std::string identity_confidence_ = "LOW";
    std::string stereo_confidence_ = "NOT_RUN";

    bool have_smoothed_ = false;
    int missing_metric_frames_ = 0;
    std::uint64_t metric_branch_id_ = 0;
    touchplus::surface::SurfacePoint smoothed_{};
};

} // namespace touchplus::tracking
