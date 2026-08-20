#include "fingertip_tracker_v9.h"

#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace touchplus::tracking;

    const int width = touchplus::depth::kDepthWidth;
    const int height = touchplus::depth::kDepthHeight;
    const int scale = touchplus::depth::kDepthScale;

    auto blank_mask = [&]() { return std::vector<uint8_t>(static_cast<size_t>(width) * height, 0); };
    auto mark_circle_full = [&](std::vector<uint8_t>& mask, int cx, int cy, int radius_px) {
        const int gx0 = cx / scale, gy0 = cy / scale, gr = std::max(1, radius_px / scale);
        for (int y = gy0 - gr; y <= gy0 + gr; ++y) {
            for (int x = gx0 - gr; x <= gx0 + gr; ++x) {
                if (x < 0 || x >= width || y < 0 || y >= height) continue;
                const int dx = x - gx0, dy = y - gy0;
                if (dx * dx + dy * dy <= gr * gr) mask[static_cast<size_t>(y) * width + x] = 1;
            }
        }
    };
    auto mark_segment_full = [&](std::vector<uint8_t>& mask, int x0, int y0, int x1, int y1, int half_width_px) {
        const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        for (int i = 0; i <= steps; ++i) {
            const double t = steps ? static_cast<double>(i) / steps : 0.0;
            const int x = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
            const int y = static_cast<int>(std::lround(y0 + (y1 - y0) * t));
            mark_circle_full(mask, x, y, half_width_px);
        }
    };

    std::vector<uint8_t> mask = blank_mask();
    mark_circle_full(mask, 190, 110, 58);
    mark_segment_full(mask, 190, 110, 152, 182, 12);
    mark_segment_full(mask, 190, 110, 310, 211, 12);

    IdentityObservationV8 geometry_obs;
    geometry_obs.hand_valid = true;
    geometry_obs.palm_valid = true;
    geometry_obs.palm_gx = 95;
    geometry_obs.palm_gy = 55;
    geometry_obs.palm_radius = 30.0;
    geometry_obs.palm_score = 0.9;

    IdentityDecisionV8 geometry_locked;
    geometry_locked.has_candidate = true;
    geometry_locked.publish = true;
    geometry_locked.tip_gx = 75;
    geometry_locked.tip_gy = 90;
    geometry_locked.branch_id = 47;
    geometry_locked.state = IdentityStateV8::Locked;
    geometry_locked.confidence = "HIGH";

    TemporalAnatomyGateV9 anatomy_gate;
    AnatomyObservationV9 a1;
    a1.status = AnatomyStatusV9::GuidedDistal;
    a1.source = AnatomySourceV9::Roi2;
    a1.pose_mode = AnatomyPoseModeV9::Strict2D;
    a1.sync_status = AnatomySyncStatusV9::CurrentFrame;
    a1.frame_id = 10;
    a1.tip_x = 150;
    a1.tip_y = 180;
    a1.source_tip_x = 150;
    a1.source_tip_y = 180;
    a1.hand_confidence = 0.99;
    a1.axis_quality = 0.94;
    a1.continuity = 1.0;
    a1.sync_shape_overlap = 1.0;

    const auto d1 = anatomy_gate.update(a1, 60.0);
    AnatomyObservationV9 a2 = a1;
    a2.frame_id = 11;
    a2.tip_x = 152;
    a2.tip_y = 182;
    a2.source_tip_x = 152;
    a2.source_tip_y = 182;
    const auto d2 = anatomy_gate.update(a2, 60.0);

    const bool anatomy_locks = !d1.publish && d1.state == AnatomyTrackStateV9::Acquiring && d2.publish && d2.state == AnatomyTrackStateV9::Locked && d2.confidence == "HIGH";

    const auto agree = fuse_identity_v9(geometry_obs, geometry_locked, d2, mask, width, height, scale);
    const bool agree_pass = agree.publish && agree.mode == FusionModeV9::GeometryAnatomyAgree && agree.pixel_x == 152 && agree.pixel_y == 182 && agree.identity_id == 47;

    IdentityDecisionV8 geometry_far = geometry_locked;
    geometry_far.tip_gx = 125;
    geometry_far.tip_gy = 125;
    const auto disagree = fuse_identity_v9(geometry_obs, geometry_far, d2, mask, width, height, scale);
    const bool disagreement_fails_closed = !disagree.publish && disagree.reason == "geometry-anatomy-disagree";

    AnatomyDecisionV9 explicit_reject;
    explicit_reject.explicit_reject = true;
    const auto rejected = fuse_identity_v9(geometry_obs, geometry_locked, explicit_reject, mask, width, height, scale);
    const bool explicit_reject_blocks = !rejected.publish && rejected.reason == "anatomy-reject";

    AnatomyDecisionV9 pair007;
    pair007.has_candidate = true;
    pair007.publish = true;
    pair007.tip_x = 310;
    pair007.tip_y = 211;
    pair007.source_tip_x = 310;
    pair007.source_tip_y = 211;
    pair007.anatomy_id = 9;
    pair007.state = AnatomyTrackStateV9::Locked;
    pair007.sync_status = AnatomySyncStatusV9::CurrentFrame;
    pair007.sync_shape_overlap = 1.0;
    pair007.confidence = "MEDIUM";
    IdentityDecisionV8 geometry_unknown;
    geometry_unknown.state = IdentityStateV8::Acquiring;
    geometry_unknown.confidence = "LOW";
    const auto rescue = fuse_identity_v9(geometry_obs, geometry_unknown, pair007, mask, width, height, scale);
    const bool anatomy_only_rescue = rescue.publish && rescue.mode == FusionModeV9::AnatomyOnly && rescue.pixel_x == 310 && rescue.pixel_y == 211 && (rescue.identity_id & 0x8000000000000000ULL) != 0;

    AnatomyDecisionV9 pair011;
    pair011.explicit_reject = true;
    const auto pair011_fusion = fuse_identity_v9(geometry_obs, geometry_unknown, pair011, mask, width, height, scale);
    const bool pair011_stays_rejected = !pair011_fusion.publish;

    AnatomyDecisionV9 stale;
    stale.stale = true;
    stale.sync_status = AnatomySyncStatusV9::TooOld;
    const auto stale_fusion = fuse_identity_v9(geometry_obs, geometry_locked, stale, mask, width, height, scale);
    const bool stale_blocks = !stale_fusion.publish && stale_fusion.reason == "anatomy-stale-motion";

    std::vector<uint8_t> empty_mask(mask.size(), 0);
    const auto outside = fuse_identity_v9(geometry_obs, geometry_locked, d2, empty_mask, width, height, scale);
    const bool silhouette_gate_blocks = !outside.publish && outside.reason == "anatomy-tip-outside-current-silhouette";

    // 2B.9C.2 frame-sync regression: a one-frame-old distal on a stable translated
    // hand is transported by palm motion and remains a real current distal.
    std::vector<uint8_t> source_mask = blank_mask();
    mark_circle_full(source_mask, 300, 210, 44);
    mark_segment_full(source_mask, 300, 210, 390, 210, 10);
    std::vector<uint8_t> translated_mask = blank_mask();
    mark_circle_full(translated_mask, 308, 216, 44);
    mark_segment_full(translated_mask, 308, 216, 398, 216, 10);

    AnatomyFrameSyncSnapshotV9 source_snap;
    source_snap.frame_id = 100;
    source_snap.mask = source_mask;
    source_snap.palm_valid = true;
    source_snap.palm_x_px = 300;
    source_snap.palm_y_px = 210;
    source_snap.palm_radius_px = 44;
    AnatomyFrameSyncSnapshotV9 current_snap = source_snap;
    current_snap.frame_id = 101;
    current_snap.mask = translated_mask;
    current_snap.palm_x_px = 308;
    current_snap.palm_y_px = 216;
    std::vector<AnatomyFrameSyncSnapshotV9> sync_history{source_snap, current_snap};

    AnatomyObservationV9 delayed;
    delayed.status = AnatomyStatusV9::GuidedDistal;
    delayed.frame_id = 100;
    delayed.age_frames = 1;
    delayed.tip_x = 400;
    delayed.tip_y = 210;
    delayed.axis_dx = 1.0;
    delayed.axis_dy = 0.0;
    delayed.axis_quality = 0.95;
    delayed.hand_confidence = 0.99;
    delayed.continuity = 1.0;
    const auto synchronized = synchronize_anatomy_observation_v9(delayed, sync_history, current_snap, width, height, scale);
    const bool delayed_translation_pass = synchronized.status == AnatomyStatusV9::GuidedDistal && synchronized.sync_status == AnatomySyncStatusV9::MotionCompensated && std::abs(synchronized.tip_x - 408) <= 2 && std::abs(synchronized.tip_y - 216) <= 2;

    // Physical 2B.9C.1 failure class: the source fingertip is one frame old and
    // still lands inside the *current* hand silhouette after a fast pose change.
    // The old 2B.9C.1 gate only asked "is it near the hand?" and would accept.
    // 2B.9C.2 must reject because the transported point is no longer a current
    // distal boundary along the source anatomical axis.
    std::vector<uint8_t> rotate_source = blank_mask();
    mark_circle_full(rotate_source, 400, 200, 40);
    mark_segment_full(rotate_source, 400, 200, 470, 200, 10);
    std::vector<uint8_t> rotate_current = blank_mask();
    mark_circle_full(rotate_current, 400, 200, 40);
    mark_segment_full(rotate_current, 400, 200, 400, 285, 10);
    // Broad current knuckle/hand region deliberately keeps the old point inside
    // the silhouette, reproducing why 2B.9C.1's simple silhouette-near test was insufficient.
    mark_segment_full(rotate_current, 430, 200, 505, 200, 22);

    AnatomyFrameSyncSnapshotV9 rotate_source_snap;
    rotate_source_snap.frame_id = 200;
    rotate_source_snap.mask = rotate_source;
    rotate_source_snap.palm_valid = true;
    rotate_source_snap.palm_x_px = 400;
    rotate_source_snap.palm_y_px = 200;
    rotate_source_snap.palm_radius_px = 40;
    AnatomyFrameSyncSnapshotV9 rotate_current_snap = rotate_source_snap;
    rotate_current_snap.frame_id = 201;
    rotate_current_snap.mask = rotate_current;
    std::vector<AnatomyFrameSyncSnapshotV9> rotate_history{rotate_source_snap, rotate_current_snap};

    AnatomyObservationV9 physical_regression;
    physical_regression.status = AnatomyStatusV9::GuidedDistal;
    physical_regression.frame_id = 200;
    physical_regression.age_frames = 1;
    physical_regression.tip_x = 470;
    physical_regression.tip_y = 200;
    physical_regression.axis_dx = 1.0;
    physical_regression.axis_dy = 0.0;
    physical_regression.axis_quality = 0.95;
    physical_regression.hand_confidence = 0.99;
    physical_regression.continuity = 1.0;
    const bool old_silhouette_gate_would_pass = mask_near_fullres_v9(rotate_current, width, height, 470, 200, scale, 2);
    const auto fast_pose = synchronize_anatomy_observation_v9(physical_regression, rotate_history, rotate_current_snap, width, height, scale);
    const bool fast_pose_blocks = old_silhouette_gate_would_pass && fast_pose.status == AnatomyStatusV9::Rejected && (fast_pose.sync_status == AnatomySyncStatusV9::TipNotCurrentDistal || fast_pose.sync_status == AnatomySyncStatusV9::ShapeChanged);

    AnatomyDecisionV9 age2_anatomy_only = pair007;
    age2_anatomy_only.age_frames = 2;
    age2_anatomy_only.sync_status = AnatomySyncStatusV9::MotionCompensated;
    age2_anatomy_only.sync_shape_overlap = 0.95;
    const auto age2_rescue = fuse_identity_v9(geometry_obs, geometry_unknown, age2_anatomy_only, mask, width, height, scale);
    const bool anatomy_only_age2_blocks = !age2_rescue.publish && age2_rescue.reason == "anatomy-only-too-old";

    AnatomyDecisionV9 weak_shape_anatomy_only = pair007;
    weak_shape_anatomy_only.age_frames = 1;
    weak_shape_anatomy_only.sync_status = AnatomySyncStatusV9::MotionCompensated;
    weak_shape_anatomy_only.sync_shape_overlap = 0.65;
    const auto weak_shape_rescue = fuse_identity_v9(geometry_obs, geometry_unknown, weak_shape_anatomy_only, mask, width, height, scale);
    const bool anatomy_only_weak_shape_blocks = !weak_shape_rescue.publish && weak_shape_rescue.reason == "anatomy-only-sync-shape-weak";

    const bool stereo_cannot_override_unknown = !final_identity_stereo_gate_v9("LOW", "HIGH") && final_identity_stereo_gate_v9("MEDIUM", "HIGH");

    std::cout
        << "TouchPlus Phase 2B.9C.2 frame-synchronous fusion self-test\n"
        << "anatomy reaches LOCKED            : " << anatomy_locks << "\n"
        << "geometry + anatomy agree          : " << agree_pass << "\n"
        << "disagreement -> UNKNOWN           : " << disagreement_fails_closed << "\n"
        << "explicit anatomy reject blocks    : " << explicit_reject_blocks << "\n"
        << "pair-007 current rescue            : " << anatomy_only_rescue << "\n"
        << "pair-011 reject remains blocked    : " << pair011_stays_rejected << "\n"
        << "stale sidecar blocks               : " << stale_blocks << "\n"
        << "current silhouette gate            : " << silhouette_gate_blocks << "\n"
        << "age-1 stable translation sync      : " << delayed_translation_pass << " tip=" << synchronized.tip_x << "," << synchronized.tip_y << " overlap=" << synchronized.sync_shape_overlap << "\n"
        << "fast pose old-tip-inside-hand block: " << fast_pose_blocks << " old_gate=" << old_silhouette_gate_would_pass << " sync=" << anatomy_sync_status_name_v9(fast_pose.sync_status) << " overlap=" << fast_pose.sync_shape_overlap << "\n"
        << "anatomy-only age>1 blocks          : " << anatomy_only_age2_blocks << "\n"
        << "anatomy-only weak shape blocks     : " << anatomy_only_weak_shape_blocks << "\n"
        << "HIGH stereo cannot override ID     : " << stereo_cannot_override_unknown << "\n";

    const bool pass = anatomy_locks && agree_pass && disagreement_fails_closed && explicit_reject_blocks && anatomy_only_rescue && pair011_stays_rejected && stale_blocks && silhouette_gate_blocks && delayed_translation_pass && fast_pose_blocks && anatomy_only_age2_blocks && anatomy_only_weak_shape_blocks && stereo_cannot_override_unknown;
    if (!pass) {
        std::cerr << "PHASE 2B.9C.2 FRAME-SYNCHRONOUS ANATOMY FUSION SELF-TEST: FAIL\n";
        return 1;
    }
    std::cout << "PHASE 2B.9C.2 FRAME-SYNCHRONOUS ANATOMY FUSION SELF-TEST: PASS (SYNTHETIC ONLY)\n";
    return 0;
}
