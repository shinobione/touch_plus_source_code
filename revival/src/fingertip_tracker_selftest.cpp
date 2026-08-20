#include "fingertip_tracker_v7.h"

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

    // Regression A: dominant diagonal index, distal skin has appearance only,
    // while a long connected appearance-only tail tries to win identity.
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
    const auto diagonal = bounded_diag.valid
        ? touchplus::tracking::palm_core_fingertip_v7(bounded_diag.mask, width, height)
        : touchplus::tracking::PalmBranchTipV7{};

    const bool diagonal_tip_ok = diagonal.valid && !diagonal.ambiguous &&
        std::abs(diagonal.gx - 235) <= 12 &&
        std::abs(diagonal.gy - 190) <= 12;
    const bool diagonal_palm_ok = diagonal.palm_radius >= 18.0 &&
        std::abs(diagonal.palm_gx - 160) <= 16 &&
        std::abs(diagonal.palm_gy - 108) <= 18;
    const bool diagonal_forearm_rejected = diagonal.rejected_forearm_branches >= 1;
    const bool far_shadow_removed = bounded_diag.valid &&
        !bounded_diag.mask[static_cast<size_t>(220) * width + 55];

    // Regression B: horizontal index. This explicitly guards the real-hardware
    // failure where top-entry/downward heuristics selected a proximal point.
    std::vector<uint8_t> appearance_horizontal(cells, 0);
    std::vector<uint8_t> support_horizontal(cells, 0);
    fill_supported_forearm_and_palm(appearance_horizontal, support_horizontal, 160, 108);
    thick_line(appearance_horizontal, 180, 108, 250, 108, 5, 76);
    thick_line(support_horizontal,    180, 108, 216, 108, 4, 40);
    thick_line(appearance_horizontal, 144, 122, 130, 151, 5, 32);
    thick_line(support_horizontal,    144, 122, 134, 143, 4, 24);

    const auto bounded_horizontal = touchplus::tracking::constrain_to_physical_support_v6(
        appearance_horizontal, support_horizontal, width, height);
    const auto horizontal = bounded_horizontal.valid
        ? touchplus::tracking::palm_core_fingertip_v7(
            bounded_horizontal.mask, width, height)
        : touchplus::tracking::PalmBranchTipV7{};
    const bool horizontal_tip_ok = horizontal.valid && !horizontal.ambiguous &&
        std::abs(horizontal.gx - 250) <= 12 &&
        std::abs(horizontal.gy - 108) <= 12;

    // Safety regression: two similarly long fingers outside the same palm are
    // deliberately ambiguous. V7 must return unknown, not pick by angle/y.
    std::vector<uint8_t> ambiguous_mask(cells, 0);
    std::vector<uint8_t> ambiguous_support(cells, 0);
    fill_supported_forearm_and_palm(ambiguous_mask, ambiguous_support, 160, 108);
    thick_line(ambiguous_mask, 146, 122, 103, 190, 5, 78);
    thick_line(ambiguous_mask, 174, 122, 217, 190, 5, 78);
    thick_line(ambiguous_support, 146, 122, 124, 158, 4, 42);
    thick_line(ambiguous_support, 174, 122, 196, 158, 4, 42);

    const auto bounded_ambiguous = touchplus::tracking::constrain_to_physical_support_v6(
        ambiguous_mask, ambiguous_support, width, height);
    const auto ambiguous = bounded_ambiguous.valid
        ? touchplus::tracking::palm_core_fingertip_v7(
            bounded_ambiguous.mask, width, height)
        : touchplus::tracking::PalmBranchTipV7{};
    const bool ambiguity_rejected = bounded_ambiguous.valid &&
        !ambiguous.valid && ambiguous.ambiguous;

    // No-hand regression.
    std::vector<uint8_t> noise(cells, 0);
    std::vector<uint8_t> noise_support(cells, 0);
    for (int y = 20; y < 28; ++y) {
        for (int x = 150; x < 160; ++x) {
            mark(noise, x, y);
            if ((x + y) % 3 == 0) mark(noise_support, x, y);
        }
    }
    const auto noise_bounded = touchplus::tracking::constrain_to_physical_support_v6(
        noise, noise_support, width, height);

    std::cout
        << "TouchPlus Phase 2B.7 palm-core finger-branch self-test\n"
        << "diag bounded cells       : " << bounded_diag.cells << "\n"
        << "diag palm                : " << diagonal.palm_gx << "," << diagonal.palm_gy
        << " r=" << diagonal.palm_radius << "\n"
        << "diag branches            : " << diagonal.branch_count << "\n"
        << "diag forearm rejected    : " << diagonal.rejected_forearm_branches << "\n"
        << "diag tip                 : " << diagonal.gx << "," << diagonal.gy << "\n"
        << "diagonal tip recovered   : " << diagonal_tip_ok << "\n"
        << "palm core recovered      : " << diagonal_palm_ok << "\n"
        << "far shadow removed       : " << far_shadow_removed << "\n"
        << "horizontal tip           : " << horizontal.gx << "," << horizontal.gy << "\n"
        << "horizontal tip recovered : " << horizontal_tip_ok << "\n"
        << "equal fingers ambiguous  : " << ambiguity_rejected << "\n"
        << "small noise rejected     : " << (!noise_bounded.valid) << "\n";

    const bool pass = bounded_diag.valid &&
        diagonal_tip_ok &&
        diagonal_palm_ok &&
        diagonal_forearm_rejected &&
        far_shadow_removed &&
        bounded_horizontal.valid &&
        horizontal_tip_ok &&
        ambiguity_rejected &&
        !noise_bounded.valid;

    if (!pass) {
        std::cerr << "PHASE 2B.7 PALM-CORE FINGER-BRANCH SELF-TEST: FAIL\n";
        return 1;
    }

    std::cout << "PHASE 2B.7 PALM-CORE FINGER-BRANCH SELF-TEST: PASS (SYNTHETIC ONLY)\n";
    return 0;
}
