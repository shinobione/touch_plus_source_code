#include "fingertip_tracker_v8.h"

#include <cmath>
#include <iostream>
#include <vector>

int main() {
    const int width = touchplus::depth::kDepthWidth;
    const int height = touchplus::depth::kDepthHeight;
    const size_t cells = static_cast<size_t>(width) * height;

    auto mark = [&](std::vector<uint8_t>& mask, int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        mask[static_cast<size_t>(y) * width + x] = 1;
    };

    auto disk = [&](std::vector<uint8_t>& mask, int cx, int cy, int r) {
        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                const int dx = x - cx;
                const int dy = y - cy;
                if (dx * dx + dy * dy <= r * r) mark(mask, x, y);
            }
        }
    };

    auto thick_line = [&](std::vector<uint8_t>& mask,
                          int x0, int y0, int x1, int y1,
                          int radius, int steps) {
        for (int i = 0; i <= steps; ++i) {
            const double t = static_cast<double>(i) / std::max(1, steps);
            const int x = static_cast<int>(std::lround(x0 + (x1 - x0) * t));
            const int y = static_cast<int>(std::lround(y0 + (y1 - y0) * t));
            disk(mask, x, y, radius);
        }
    };

    auto fill_supported_forearm_and_palm = [&](std::vector<uint8_t>& appearance,
                                                std::vector<uint8_t>& support,
                                                int palm_x,
                                                int palm_y) {
        for (int y = 4; y <= palm_y - 8; ++y) {
            for (int x = palm_x - 10; x <= palm_x + 10; ++x) {
                mark(appearance, x, y);
                if ((x + y) % 5 != 0) mark(support, x, y);
            }
        }
        disk(appearance, palm_x, palm_y, 31);
        for (int y = palm_y - 29; y <= palm_y + 29; ++y) {
            for (int x = palm_x - 29; x <= palm_x + 29; ++x) {
                const int dx = x - palm_x;
                const int dy = y - palm_y;
                if (dx * dx + dy * dy <= 29 * 29 && (x + 2 * y) % 5 != 0) {
                    mark(support, x, y);
                }
            }
        }
    };

    // Geometry regression A: dominant diagonal index, distal skin remains
    // appearance-only, and a long unsupported shadow tail must stay excluded.
    std::vector<uint8_t> appearance_diag(cells, 0);
    std::vector<uint8_t> support_diag(cells, 0);
    fill_supported_forearm_and_palm(appearance_diag, support_diag, 160, 108);
    thick_line(appearance_diag, 176, 122, 235, 190, 5, 80);
    thick_line(support_diag,    176, 122, 205, 155, 4, 42);
    thick_line(appearance_diag, 145, 123, 125, 153, 5, 36);
    thick_line(support_diag,    145, 123, 130, 146, 4, 28);
    thick_line(appearance_diag, 146, 122, 55, 220, 5, 105);

    const auto bounded_diag = touchplus::tracking::constrain_to_physical_support_v6(
        appearance_diag, support_diag, width, height);
    const auto diag = bounded_diag.valid
        ? touchplus::tracking::analyze_finger_identity_v8(
            bounded_diag.mask, width, height)
        : touchplus::tracking::IdentityObservationV8{};
    const int diag_best =
        touchplus::tracking::best_static_candidate_index_v8(diag);

    const bool diagonal_palm_ok =
        diag.palm_valid &&
        diag.palm_radius >= 18.0 &&
        std::abs(diag.palm_gx - 160) <= 16 &&
        std::abs(diag.palm_gy - 108) <= 18 &&
        diag.palm_core_fill >= touchplus::tracking::kV8MinPalmCoreFill;

    const bool diagonal_tip_ok =
        diag_best >= 0 &&
        std::abs(diag.candidates[diag_best].tip_gx - 235) <= 12 &&
        std::abs(diag.candidates[diag_best].tip_gy - 190) <= 12;

    const bool finger_geometry_ok =
        diag_best >= 0 &&
        diag.candidates[diag_best].linearity >=
            touchplus::tracking::kV8MinBranchLinearity &&
        std::max(
            diag.candidates[diag_best].mid_width,
            diag.candidates[diag_best].distal_width) <=
            diag.palm_radius *
            touchplus::tracking::kV8MaxFingerWidthPalmRatio;

    const bool far_shadow_removed =
        bounded_diag.valid &&
        !bounded_diag.mask[static_cast<size_t>(220) * width + 55];

    // Geometry regression B: horizontal index. This guards the physical class
    // where a proximal palm point used to win against the visible fingertip.
    std::vector<uint8_t> appearance_horizontal(cells, 0);
    std::vector<uint8_t> support_horizontal(cells, 0);
    fill_supported_forearm_and_palm(
        appearance_horizontal, support_horizontal, 160, 108);
    thick_line(appearance_horizontal, 180, 108, 250, 108, 5, 76);
    thick_line(support_horizontal,    180, 108, 216, 108, 4, 40);
    thick_line(appearance_horizontal, 144, 122, 130, 151, 5, 32);
    thick_line(support_horizontal,    144, 122, 134, 143, 4, 24);

    const auto bounded_horizontal =
        touchplus::tracking::constrain_to_physical_support_v6(
            appearance_horizontal, support_horizontal, width, height);
    const auto horizontal = bounded_horizontal.valid
        ? touchplus::tracking::analyze_finger_identity_v8(
            bounded_horizontal.mask, width, height)
        : touchplus::tracking::IdentityObservationV8{};
    const int horizontal_best =
        touchplus::tracking::best_static_candidate_index_v8(horizontal);
    const bool horizontal_tip_ok =
        horizontal_best >= 0 &&
        std::abs(horizontal.candidates[horizontal_best].tip_gx - 250) <= 12 &&
        std::abs(horizontal.candidates[horizontal_best].tip_gy - 108) <= 12;

    // Safety regression: two similarly strong distal branches stay ambiguous
    // when there is no previous branch identity to disambiguate them.
    std::vector<uint8_t> ambiguous_mask(cells, 0);
    std::vector<uint8_t> ambiguous_support(cells, 0);
    fill_supported_forearm_and_palm(
        ambiguous_mask, ambiguous_support, 160, 108);
    thick_line(ambiguous_mask, 146, 122, 103, 190, 5, 78);
    thick_line(ambiguous_mask, 174, 122, 217, 190, 5, 78);
    thick_line(ambiguous_support, 146, 122, 124, 158, 4, 42);
    thick_line(ambiguous_support, 174, 122, 196, 158, 4, 42);

    const auto bounded_ambiguous =
        touchplus::tracking::constrain_to_physical_support_v6(
            ambiguous_mask, ambiguous_support, width, height);
    const auto ambiguous = bounded_ambiguous.valid
        ? touchplus::tracking::analyze_finger_identity_v8(
            bounded_ambiguous.mask, width, height)
        : touchplus::tracking::IdentityObservationV8{};

    touchplus::tracking::TemporalIdentityGateV8 ambiguity_gate;
    const auto amb1 = ambiguity_gate.update(ambiguous);
    const auto amb2 = ambiguity_gate.update(ambiguous);
    const auto amb3 = ambiguity_gate.update(ambiguous);
    const bool ambiguity_rejected =
        ambiguous.candidates.size() >= 2 &&
        ambiguous.static_ambiguous &&
        !amb1.publish && !amb2.publish && !amb3.publish;

    // No-hand regression from earlier physical failures.
    std::vector<uint8_t> noise(cells, 0);
    std::vector<uint8_t> noise_support(cells, 0);
    for (int y = 20; y < 28; ++y) {
        for (int x = 150; x < 160; ++x) {
            mark(noise, x, y);
            if ((x + y) % 3 == 0) mark(noise_support, x, y);
        }
    }
    const auto noise_bounded =
        touchplus::tracking::constrain_to_physical_support_v6(
            noise, noise_support, width, height);

    auto make_observation = [](
        int palm_x, int palm_y, double palm_r,
        int tip_x, int tip_y,
        double root_angle = 2.35,
        double direction_angle = 2.15) {

        touchplus::tracking::IdentityObservationV8 obs;
        obs.hand_valid = true;
        obs.palm_valid = true;
        obs.palm_gx = palm_x;
        obs.palm_gy = palm_y;
        obs.palm_radius = palm_r;
        obs.palm_core_fill = 0.94;
        obs.palm_score = 0.90;

        touchplus::tracking::FingerBranchV8 b;
        b.valid = true;
        b.root_gx = palm_x - 10;
        b.root_gy = palm_y + 9;
        b.skeleton_tip_gx = tip_x;
        b.skeleton_tip_gy = tip_y;
        b.tip_gx = tip_x;
        b.tip_gy = tip_y;
        b.cells = 70;
        b.extension = palm_r * 1.55;
        b.extension_ratio = 1.55;
        b.root_angle = root_angle;
        b.direction_angle = direction_angle;
        b.proximal_width = 10.0;
        b.mid_width = 8.5;
        b.distal_width = 7.0;
        b.linearity = 0.94;
        b.geometry_score = 0.88;
        obs.candidates.push_back(b);
        return obs;
    };

    // Physical 2B.7 regression: a stable single-index sequence around
    // tip_pixel 263,173 -> 265,177 must lock, but the later 189,87 re-election
    // must become UNKNOWN before stereo is even consulted. Runtime tip_pixel is
    // full resolution; identity operates on the half-resolution grid.
    touchplus::tracking::TemporalIdentityGateV8 physical_gate;
    const auto p1 = physical_gate.update(
        make_observation(190, 33, 26.3, 131, 86));
    const auto p2 = physical_gate.update(
        make_observation(190, 34, 26.7, 132, 88));
    const auto p3 = physical_gate.update(
        make_observation(189, 35, 27.0, 133, 89));
    const auto bad_jump = physical_gate.update(
        make_observation(187, 36, 27.7, 94, 43));

    const bool stable_identity_locks =
        p1.state == touchplus::tracking::IdentityStateV8::Acquiring &&
        p2.state == touchplus::tracking::IdentityStateV8::Acquiring &&
        p3.state == touchplus::tracking::IdentityStateV8::Locked &&
        p3.publish &&
        (p3.confidence == "MEDIUM" || p3.confidence == "HIGH");

    const bool physical_jump_rejected =
        !bad_jump.publish &&
        bad_jump.jump_rejected &&
        bad_jump.state == touchplus::tracking::IdentityStateV8::Locked;

    // A strong stereo score can never override a rejected identity.
    const bool high_stereo_cannot_override_bad_identity =
        !touchplus::tracking::final_identity_stereo_gate_v8("LOW", "HIGH") &&
         touchplus::tracking::final_identity_stereo_gate_v8("MEDIUM", "HIGH");

    // Palm persistence regression: after lock, a sudden palm relocation is
    // rejected instead of silently re-rooting the same branch elsewhere.
    touchplus::tracking::TemporalIdentityGateV8 palm_gate;
    palm_gate.update(make_observation(160, 108, 30.0, 220, 180));
    palm_gate.update(make_observation(161, 108, 30.4, 221, 180));
    const auto palm_locked =
        palm_gate.update(make_observation(162, 109, 30.2, 222, 181));
    const auto palm_jump =
        palm_gate.update(make_observation(125, 155, 29.8, 185, 227));
    const bool palm_jump_rejected =
        palm_locked.publish &&
        !palm_jump.publish &&
        palm_jump.palm_rejected;

    std::cout
        << "TouchPlus Phase 2B.8 temporal palm/branch identity self-test\n"
        << "diag bounded cells             : " << bounded_diag.cells << "\n"
        << "diag palm                      : " << diag.palm_gx << "," << diag.palm_gy
        << " r=" << diag.palm_radius
        << " fill=" << diag.palm_core_fill << "\n"
        << "diag candidates                : " << diag.candidates.size() << "\n"
        << "diag tip recovered             : " << diagonal_tip_ok << "\n"
        << "palm validation                : " << diagonal_palm_ok << "\n"
        << "finger width/linearity gate    : " << finger_geometry_ok << "\n"
        << "far shadow removed             : " << far_shadow_removed << "\n"
        << "horizontal tip recovered       : " << horizontal_tip_ok << "\n"
        << "equal fingers stay ambiguous   : " << ambiguity_rejected << "\n"
        << "small noise rejected           : " << (!noise_bounded.valid) << "\n"
        << "stable branch reaches LOCKED   : " << stable_identity_locks << "\n"
        << "2B.7 bad jump -> UNKNOWN       : " << physical_jump_rejected << "\n"
        << "HIGH stereo cannot override ID : "
        << high_stereo_cannot_override_bad_identity << "\n"
        << "palm teleport rejected         : " << palm_jump_rejected << "\n";

    const bool pass =
        bounded_diag.valid &&
        diagonal_palm_ok &&
        diagonal_tip_ok &&
        finger_geometry_ok &&
        far_shadow_removed &&
        bounded_horizontal.valid &&
        horizontal_tip_ok &&
        ambiguity_rejected &&
        !noise_bounded.valid &&
        stable_identity_locks &&
        physical_jump_rejected &&
        high_stereo_cannot_override_bad_identity &&
        palm_jump_rejected;

    if (!pass) {
        std::cerr
            << "PHASE 2B.8 TEMPORAL PALM/BRANCH IDENTITY SELF-TEST: FAIL\n";
        return 1;
    }

    std::cout
        << "PHASE 2B.8 TEMPORAL PALM/BRANCH IDENTITY SELF-TEST: "
        << "PASS (SYNTHETIC ONLY)\n";
    return 0;
}
