#include "fingertip_tracker_v6.h"

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

    std::vector<uint8_t> appearance(cells, 0);
    std::vector<uint8_t> support(cells, 0);

    // Canonical top-entry forearm with strong physical depth support.
    for (int y = 18; y <= 82; ++y) {
        for (int x = 151; x <= 169; ++x) {
            mark(appearance, x, y);
            if ((x + y) % 4 != 0) mark(support, x, y);
        }
    }

    // Palm.
    for (int y = 70; y <= 136; ++y) {
        for (int x = 130; x <= 190; ++x) {
            const double nx = (x - 160) / 30.0;
            const double ny = (y - 103) / 33.0;
            if (nx * nx + ny * ny > 1.0) continue;
            mark(appearance, x, y);
            if ((x + 2 * y) % 4 != 0) mark(support, x, y);
        }
    }

    // REAL-VIDEO REGRESSION: the one dominant index points diagonally down-right.
    // The distal ~35 half-res cells have appearance only (low-texture skin), so
    // a depth-only tracker cannot see the visible fingertip.
    thick_line(appearance, 165, 124, 222, 198, 5, 76);
    thick_line(support,    165, 124, 191, 158, 4, 36);

    // Short folded-finger branch: must lose to the dominant index.
    thick_line(appearance, 146, 122, 132, 154, 5, 34);
    thick_line(support,    146, 122, 136, 145, 4, 24);

    // V5 physical failure: a connected appearance-only shadow/tail runs far
    // down-left from the palm. It must be truncated by physical-support distance
    // and must never become the fingertip just because it reaches the frame low.
    thick_line(appearance, 150, 124, 58, 218, 5, 96);

    const auto bounded = touchplus::tracking::constrain_to_physical_support_v6(
        appearance, support, width, height);
    const auto tip = bounded.valid
        ? touchplus::tracking::skeleton_distal_tip_v6(bounded.mask, width, height)
        : touchplus::tracking::SkeletonTipV6{};

    const bool far_shadow_removed = bounded.valid &&
        !bounded.mask[static_cast<size_t>(218) * width + 58];
    const bool diagonal_tip_recovered = tip.valid && !tip.ambiguous &&
        std::abs(tip.gx - 222) <= 9 &&
        std::abs(tip.gy - 198) <= 9;

    // Multi-finger safety regression: two similarly long distal branches are
    // deliberately ambiguous. V6 must return unknown instead of arbitrarily
    // choosing one of them.
    std::vector<uint8_t> ambiguous_mask(cells, 0);
    std::vector<uint8_t> ambiguous_support(cells, 0);
    for (int y = 18; y <= 82; ++y) {
        for (int x = 151; x <= 169; ++x) {
            mark(ambiguous_mask, x, y);
            mark(ambiguous_support, x, y);
        }
    }
    for (int y = 70; y <= 136; ++y) {
        for (int x = 130; x <= 190; ++x) {
            const double nx = (x - 160) / 30.0;
            const double ny = (y - 103) / 33.0;
            if (nx * nx + ny * ny > 1.0) continue;
            mark(ambiguous_mask, x, y);
            mark(ambiguous_support, x, y);
        }
    }
    thick_line(ambiguous_mask, 150, 124, 105, 198, 5, 78);
    thick_line(ambiguous_mask, 170, 124, 215, 198, 5, 78);
    thick_line(ambiguous_support, 150, 124, 126, 162, 4, 42);
    thick_line(ambiguous_support, 170, 124, 194, 162, 4, 42);

    const auto ambiguous_bounded = touchplus::tracking::constrain_to_physical_support_v6(
        ambiguous_mask, ambiguous_support, width, height);
    const auto ambiguous_tip = ambiguous_bounded.valid
        ? touchplus::tracking::skeleton_distal_tip_v6(
            ambiguous_bounded.mask, width, height)
        : touchplus::tracking::SkeletonTipV6{};
    const bool ambiguity_rejected = ambiguous_bounded.valid &&
        !ambiguous_tip.valid && ambiguous_tip.ambiguous;

    // No-hand regression: tiny photometric patch with a few support cells must
    // stay below the accepted physical hand boundary.
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
        << "TouchPlus Phase 2B.6 support-bounded skeleton fingertip self-test\n"
        << "bounded hand cells       : " << bounded.cells << "\n"
        << "support cells            : " << bounded.support_cells << "\n"
        << "skeleton cells           : " << tip.skeleton_cells << "\n"
        << "endpoint candidates      : " << tip.endpoint_count << "\n"
        << "skeleton tip grid        : " << tip.gx << "," << tip.gy << "\n"
        << "geodesic steps           : " << tip.geodesic_steps << "\n"
        << "far shadow removed       : " << far_shadow_removed << "\n"
        << "diagonal tip recovered   : " << diagonal_tip_recovered << "\n"
        << "equal branches ambiguous : " << ambiguity_rejected << "\n"
        << "small noise rejected     : " << (!noise_bounded.valid) << "\n";

    const bool pass = bounded.valid &&
        bounded.support_cells > 200 &&
        far_shadow_removed &&
        diagonal_tip_recovered &&
        tip.geodesic_steps >= 80 &&
        ambiguity_rejected &&
        !noise_bounded.valid;

    if (!pass) {
        std::cerr << "PHASE 2B.6 SUPPORT-SKELETON FINGERTIP SELF-TEST: FAIL\n";
        return 1;
    }

    std::cout << "PHASE 2B.6 SUPPORT-SKELETON FINGERTIP SELF-TEST: PASS\n";
    return 0;
}
