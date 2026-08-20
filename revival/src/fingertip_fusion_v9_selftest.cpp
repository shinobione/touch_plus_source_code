#include "fingertip_tracker_v9.h"

#include <iostream>
#include <vector>

int main() {
    using namespace touchplus::tracking;

    const int width = touchplus::depth::kDepthWidth;
    const int height = touchplus::depth::kDepthHeight;
    const int scale = touchplus::depth::kDepthScale;
    std::vector<uint8_t> mask(static_cast<size_t>(width) * height, 0);

    auto mark_full = [&](int px, int py, int radius = 3) {
        const int gx = px / scale;
        const int gy = py / scale;
        for (int y = gy - radius; y <= gy + radius; ++y) {
            for (int x = gx - radius; x <= gx + radius; ++x) {
                if (x < 0 || x >= width || y < 0 || y >= height) continue;
                mask[static_cast<size_t>(y) * width + x] = 1;
            }
        }
    };
    mark_full(152, 182, 6);
    mark_full(310, 211, 6);

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
    a1.frame_id = 10;
    a1.tip_x = 150;
    a1.tip_y = 180;
    a1.hand_confidence = 0.99;
    a1.axis_quality = 0.94;
    a1.continuity = 1.0;

    const auto d1 = anatomy_gate.update(a1, 60.0);
    AnatomyObservationV9 a2 = a1;
    a2.frame_id = 11;
    a2.tip_x = 152;
    a2.tip_y = 182;
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
    pair007.anatomy_id = 9;
    pair007.state = AnatomyTrackStateV9::Locked;
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
    const auto stale_fusion = fuse_identity_v9(geometry_obs, geometry_locked, stale, mask, width, height, scale);
    const bool stale_blocks = !stale_fusion.publish;

    std::vector<uint8_t> empty_mask(mask.size(), 0);
    const auto outside = fuse_identity_v9(geometry_obs, geometry_locked, d2, empty_mask, width, height, scale);
    const bool silhouette_gate_blocks = !outside.publish && outside.reason == "anatomy-tip-outside-current-silhouette";

    const bool stereo_cannot_override_unknown = !final_identity_stereo_gate_v9("LOW", "HIGH") && final_identity_stereo_gate_v9("MEDIUM", "HIGH");

    std::cout
        << "TouchPlus Phase 2B.9C.1 fusion self-test\n"
        << "anatomy reaches LOCKED         : " << anatomy_locks << "\n"
        << "geometry + anatomy agree       : " << agree_pass << "\n"
        << "disagreement -> UNKNOWN        : " << disagreement_fails_closed << "\n"
        << "explicit anatomy reject blocks : " << explicit_reject_blocks << "\n"
        << "pair-007 anatomy-only rescue    : " << anatomy_only_rescue << "\n"
        << "pair-011 reject remains blocked : " << pair011_stays_rejected << "\n"
        << "stale sidecar blocks            : " << stale_blocks << "\n"
        << "current silhouette gate         : " << silhouette_gate_blocks << "\n"
        << "HIGH stereo cannot override ID  : " << stereo_cannot_override_unknown << "\n";

    const bool pass = anatomy_locks && agree_pass && disagreement_fails_closed && explicit_reject_blocks && anatomy_only_rescue && pair011_stays_rejected && stale_blocks && silhouette_gate_blocks && stereo_cannot_override_unknown;
    if (!pass) {
        std::cerr << "PHASE 2B.9C.1 LIVE ANATOMY FUSION SELF-TEST: FAIL\n";
        return 1;
    }
    std::cout << "PHASE 2B.9C.1 LIVE ANATOMY FUSION SELF-TEST: PASS (SYNTHETIC ONLY)\n";
    return 0;
}
