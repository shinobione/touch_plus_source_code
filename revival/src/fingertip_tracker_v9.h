#pragma once

#include "fingertip_anatomy_ipc_v9.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.9C.2 runtime integration.
//
// Geometry V8 remains the temporal palm/branch safety layer. A separate Python
// sidecar receives only LEFT grayscale + the current Touch+ silhouette and
// returns a 2D landmark-guided distal candidate. Fusion happens BEFORE stereo.
// 2B.9C.2 additionally aligns asynchronous anatomy results back to the current
// frame using a short palm/silhouette history and requires the compensated point
// to remain a real current distal boundary. No sidecar/model Z enters this class;
// accepted Touch+ stereo/Q is still the only metric XYZ source.

class FingertipTrackerV9 {
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

        ++frame_id_;
        if (frame_id_ == 0) ++frame_id_;

        const TrackingResult base_result =
            base_.update(calibration, surface, left_gray, right_gray, workspace);

        TrackingResult out;
        selected_mask_ = base_.selected_mask();
        out.foreground_samples = base_result.foreground_samples;
        last_identity_ = {};
        last_decision_ = {};
        last_anatomy_observation_ = {};
        last_anatomy_decision_ = {};
        last_fusion_ = {};
        identity_confidence_ = "LOW";
        stereo_confidence_ = "NOT_RUN";

        if (!base_.background_ready() || selected_mask_.empty() || !base_result.hand_valid) {
            if (!base_result.hand_valid) selected_mask_.clear();
            temporal_identity_.update({});
            exchange_anatomy(left_gray, selected_mask_, false, 30.0);
            reset_metric_if_identity_lost(false, 0);
            last_result_ = out;
            return out;
        }

        const size_t depth_cells = static_cast<size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight;
        std::vector<uint8_t> support(depth_cells, 0);
        std::vector<int> support_disp_small(depth_cells, 0);

        const double roi_half_x = surface.spread_x_mm >= 80.0 ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
        const double roi_half_y = surface.spread_y_mm >= 80.0 ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

        constexpr int radius = 2;
        constexpr int area = (radius * 2 + 1) * (radius * 2 + 1);
        constexpr int max_average_cost = 44;
        constexpr double uniqueness = 1.08;
        constexpr int inf = std::numeric_limits<int>::max() / 4;

        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx = static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                if (!selected_mask_[idx]) continue;
                const int d_small = workspace.best_disp[idx];
                const bool dense_valid = d_small > 0 && workspace.best_cost[idx] <= max_average_cost * area &&
                    (workspace.second_cost[idx] == inf || static_cast<double>(workspace.second_cost[idx]) >= static_cast<double>(workspace.best_cost[idx]) * uniqueness);
                if (!dense_valid) continue;
                const double disparity = static_cast<double>(d_small * touchplus::depth::kDepthScale);
                const double u = gx * touchplus::depth::kDepthScale + 0.5;
                const double v = gy * touchplus::depth::kDepthScale + 0.5;
                const auto camera = touchplus::surface::camera_point_from_q(calibration, u, v, disparity);
                if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
                const auto sp = touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) || sp.h_mm < kV6MinSupportHmm || sp.h_mm > kV6MaxSupportHmm) continue;
                if (std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
                support[idx] = 1;
                support_disp_small[idx] = d_small;
            }
        }

        const auto bounded = constrain_to_physical_support_v6(selected_mask_, support, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);
        if (!bounded.valid) {
            selected_mask_.clear();
            temporal_identity_.update({});
            exchange_anatomy(left_gray, selected_mask_, false, 30.0);
            reset_metric_if_identity_lost(false, 0);
            last_result_ = out;
            return out;
        }

        selected_mask_ = bounded.mask;
        out.hand_samples = bounded.cells;
        out.hand_valid = true;

        last_identity_ = analyze_finger_identity_v8(selected_mask_, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);
        last_decision_ = temporal_identity_.update(last_identity_);

        exchange_anatomy(left_gray, selected_mask_, true, std::max(8.0, last_identity_.palm_radius * touchplus::depth::kDepthScale));
        last_fusion_ = fuse_identity_v9(last_identity_, last_decision_, last_anatomy_decision_, selected_mask_, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight, touchplus::depth::kDepthScale);
        identity_confidence_ = last_fusion_.confidence;

        if (last_anatomy_decision_.has_candidate) {
            out.pixel_x = last_anatomy_decision_.tip_x;
            out.pixel_y = last_anatomy_decision_.tip_y;
        } else if (last_decision_.has_candidate) {
            out.pixel_x = last_decision_.tip_gx * touchplus::depth::kDepthScale + 1;
            out.pixel_y = last_decision_.tip_gy * touchplus::depth::kDepthScale + 1;
        }

        if (!last_fusion_.publish) {
            out.confidence = "LOW";
            reset_metric_if_identity_lost(false, 0);
            last_result_ = out;
            return out;
        }

        out.pixel_x = last_fusion_.pixel_x;
        out.pixel_y = last_fusion_.pixel_y;
        const std::uint64_t active_identity_id = last_fusion_.identity_id;
        reset_metric_if_identity_lost(true, active_identity_id);

        const int px = out.pixel_x;
        const int py = out.pixel_y;
        if (px < 0 || py < 0) { out.confidence = "LOW"; last_result_ = out; return out; }

        int nearest_d_small = 0;
        int nearest_dist2 = std::numeric_limits<int>::max();
        const int target_gx = px / touchplus::depth::kDepthScale;
        const int target_gy = py / touchplus::depth::kDepthScale;
        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx = static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                if (!support[idx] || !selected_mask_[idx]) continue;
                const int sx = gx - target_gx;
                const int sy = gy - target_gy;
                const int d2 = sx * sx + sy * sy;
                if (d2 < nearest_dist2) { nearest_dist2 = d2; nearest_d_small = support_disp_small[idx]; }
            }
        }

        if (nearest_d_small <= 0 || nearest_dist2 > 44 * 44) {
            stereo_confidence_ = "LOW";
            out.confidence = "LOW";
            ++missing_metric_frames_;
            last_result_ = out;
            return out;
        }

        const double coarse_disp = static_cast<double>(nearest_d_small * touchplus::depth::kDepthScale);
        const int min_d = std::max(touchplus::depth::robust_point_detail::kMinDisparity, static_cast<int>(std::floor(coarse_disp - 18.0)));
        const int max_d = std::min(touchplus::depth::robust_point_detail::kMaxDisparity, static_cast<int>(std::ceil(coarse_disp + 18.0)));

        std::vector<touchplus::surface::SurfacePoint> refined;
        constexpr std::array<int, 7> offsets{{-12, -8, -4, 0, 4, 8, 12}};
        for (const int oy : offsets) {
            for (const int ox : offsets) {
                const int sx = px + ox, sy = py + oy;
                if (sx < 12 || sx >= touchplus::depth::kEyeWidth - 5 || sy < 5 || sy >= touchplus::depth::kEyeHeight - 5) continue;
                const int sgx = sx / touchplus::depth::kDepthScale;
                const int sgy = sy / touchplus::depth::kDepthScale;
                if (!mask_near_v5(selected_mask_, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight, sgx, sgy, 1)) continue;
                const auto match = touchplus::depth::robust_point_detail::mutually_consistent_match(left_gray, right_gray, sx, sy, min_d, max_d);
                if (!match.valid) continue;
                const auto camera = touchplus::surface::camera_point_from_q(calibration, static_cast<double>(sx), static_cast<double>(sy), match.disparity);
                if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
                const auto sp = touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) || sp.h_mm < 2.0 || sp.h_mm > kV6MaxSupportHmm + 20.0 || std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
                refined.push_back(sp);
            }
        }

        if (!refined.empty()) {
            std::vector<double> hs; hs.reserve(refined.size()); for (const auto& p : refined) hs.push_back(p.h_mm);
            const double median_h = touchplus::surface::median(std::move(hs));
            std::vector<touchplus::surface::SurfacePoint> consistent; consistent.reserve(refined.size());
            for (const auto& p : refined) if (std::abs(p.h_mm - median_h) <= 22.0) consistent.push_back(p);
            refined = std::move(consistent);
        }

        out.refinement_support = static_cast<int>(refined.size());
        stereo_confidence_ = refined.size() >= 6 ? "HIGH" : refined.size() >= 3 ? "MEDIUM" : "LOW";
        if (!final_identity_stereo_gate_v9(identity_confidence_, stereo_confidence_)) {
            out.confidence = "LOW";
            ++missing_metric_frames_;
            last_result_ = out;
            return out;
        }

        out.raw_tip = median_surface_point(refined);
        if (have_smoothed_) {
            const double jump = std::sqrt(sqr(out.raw_tip.x_mm - smoothed_.x_mm) + sqr(out.raw_tip.y_mm - smoothed_.y_mm) + sqr(out.raw_tip.h_mm - smoothed_.h_mm));
            if (jump > 85.0 && missing_metric_frames_ < 3) {
                stereo_confidence_ = "LOW"; out.confidence = "LOW"; ++missing_metric_frames_; last_result_ = out; return out;
            }
            constexpr double alpha = 0.32;
            smoothed_.x_mm = smoothed_.x_mm * (1.0 - alpha) + out.raw_tip.x_mm * alpha;
            smoothed_.y_mm = smoothed_.y_mm * (1.0 - alpha) + out.raw_tip.y_mm * alpha;
            smoothed_.h_mm = smoothed_.h_mm * (1.0 - alpha) + out.raw_tip.h_mm * alpha;
        } else { smoothed_ = out.raw_tip; have_smoothed_ = true; }

        missing_metric_frames_ = 0;
        out.smoothed_tip = smoothed_;
        out.confidence = identity_confidence_ == "HIGH" && stereo_confidence_ == "HIGH" ? "HIGH" : "MEDIUM";
        out.fingertip_valid = true;
        last_result_ = out;
        return out;
    }

    void clear() { base_.clear(); clear_tracking_only(); }
    const TrackingResult& last_result() const { return last_result_; }
    const std::vector<uint8_t>& selected_mask() const { return selected_mask_; }
    const IdentityObservationV8& last_identity() const { return last_identity_; }
    const IdentityDecisionV8& last_decision() const { return last_decision_; }
    const AnatomyObservationV9& last_anatomy_observation() const { return last_anatomy_observation_; }
    const AnatomyDecisionV9& last_anatomy_decision() const { return last_anatomy_decision_; }
    const FusedIdentityV9& last_fusion() const { return last_fusion_; }
    const std::string& identity_confidence() const { return identity_confidence_; }
    const std::string& stereo_confidence() const { return stereo_confidence_; }
    std::uint32_t frame_id() const { return frame_id_; }
    std::uint32_t sidecar_last_error() const { return static_cast<std::uint32_t>(anatomy_bridge_.last_error()); }

private:
    void remember_sync_snapshot(const AnatomyFrameSyncSnapshotV9& snapshot) {
        sync_history_.push_back(snapshot);
        while (sync_history_.size() > 6) sync_history_.erase(sync_history_.begin());
    }

    void exchange_anatomy(const std::vector<uint8_t>& left_gray, const std::vector<uint8_t>& mask, bool hand_valid, double palm_radius_full_px) {
        const auto current = make_anatomy_sync_snapshot_v9(frame_id_, mask, last_identity_, touchplus::depth::kDepthScale);
        remember_sync_snapshot(current);
        anatomy_bridge_.publish_frame(frame_id_, left_gray, mask, hand_valid, base_.background_ready());
        const AnatomyObservationV9 raw = anatomy_bridge_.read_result(frame_id_, 2);
        last_anatomy_observation_ = synchronize_anatomy_observation_v9(
            raw,
            sync_history_,
            current,
            touchplus::depth::kDepthWidth,
            touchplus::depth::kDepthHeight,
            touchplus::depth::kDepthScale);
        last_anatomy_decision_ = anatomy_gate_.update(last_anatomy_observation_, palm_radius_full_px);
    }
    void reset_metric_if_identity_lost(bool fused_locked, std::uint64_t identity_id) {
        if (!fused_locked) { have_smoothed_ = false; missing_metric_frames_ = 0; smoothed_ = {}; metric_identity_id_ = 0; return; }
        if (metric_identity_id_ != identity_id) { have_smoothed_ = false; missing_metric_frames_ = 0; smoothed_ = {}; metric_identity_id_ = identity_id; }
    }
    void clear_tracking_only() {
        selected_mask_.clear(); sync_history_.clear(); last_result_ = {}; last_identity_ = {}; last_decision_ = {}; last_anatomy_observation_ = {}; last_anatomy_decision_ = {}; last_fusion_ = {};
        temporal_identity_.clear(); anatomy_gate_.clear(); identity_confidence_ = "LOW"; stereo_confidence_ = "NOT_RUN"; have_smoothed_ = false; missing_metric_frames_ = 0; metric_identity_id_ = 0; smoothed_ = {};
    }

    FingertipTrackerV5 base_;
    TemporalIdentityGateV8 temporal_identity_;
    AnatomySidecarBridgeV9 anatomy_bridge_;
    TemporalAnatomyGateV9 anatomy_gate_;
    TrackingResult last_result_{};
    IdentityObservationV8 last_identity_{};
    IdentityDecisionV8 last_decision_{};
    AnatomyObservationV9 last_anatomy_observation_{};
    AnatomyDecisionV9 last_anatomy_decision_{};
    FusedIdentityV9 last_fusion_{};
    std::vector<uint8_t> selected_mask_;
    std::vector<AnatomyFrameSyncSnapshotV9> sync_history_;
    std::string identity_confidence_ = "LOW", stereo_confidence_ = "NOT_RUN";
    std::uint32_t frame_id_ = 0;
    bool have_smoothed_ = false;
    int missing_metric_frames_ = 0;
    std::uint64_t metric_identity_id_ = 0;
    touchplus::surface::SurfacePoint smoothed_{};
};

} // namespace touchplus::tracking
