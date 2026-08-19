#include "fingertip_tracker_v5.h"

#include <iostream>
#include <vector>

int main() {
    const int width = touchplus::depth::kDepthWidth;
    const int height = touchplus::depth::kDepthHeight;
    const size_t cells = static_cast<size_t>(width) * height;

    std::vector<uint8_t> appearance(cells, 0);
    std::vector<uint8_t> core(cells, 0);

    auto mark = [&](std::vector<uint8_t>& mask, int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        mask[static_cast<size_t>(y) * width + x] = 1;
    };

    // Canonical top-entry forearm.
    for (int y = 20; y <= 78; ++y) {
        for (int x = 151; x <= 169; ++x) {
            mark(appearance, x, y);
            if ((x + y) % 3 != 0) mark(core, x, y);
        }
    }

    // Broad palm.
    for (int y = 70; y <= 132; ++y) {
        for (int x = 132; x <= 188; ++x) {
            const double nx = (x - 160) / 28.0;
            const double ny = (y - 102) / 31.0;
            if (nx * nx + ny * ny > 1.0) continue;
            mark(appearance, x, y);
            if ((x + 2 * y) % 4 != 0) mark(core, x, y);
        }
    }

    // Long index. IMPORTANT regression: the distal 34 rows deliberately have
    // NO dense-depth core, reproducing the real video where the visible finger
    // continued below the last reliable disparity cell.
    for (int y = 124; y <= 194; ++y) {
        const int half = y > 178 ? 3 : 5;
        for (int x = 160 - half; x <= 160 + half; ++x) {
            mark(appearance, x, y);
            if (y <= 160 && (x + y) % 3 != 0) {
                mark(core, x, y);
            }
        }
    }

    // Two shorter folded-finger branches.
    for (int y = 120; y <= 148; ++y) {
        for (int x = 139; x <= 147; ++x) {
            mark(appearance, x, y);
            if ((x + y) % 4 != 0) mark(core, x, y);
        }
        for (int x = 175; x <= 183; ++x) {
            mark(appearance, x, y);
            if ((x + y) % 4 != 0) mark(core, x, y);
        }
    }

    // Large appearance-only distractor: no depth core, therefore it must lose
    // even though it is bigger than the real hand silhouette.
    for (int y = 30; y <= 150; ++y) {
        for (int x = 8; x <= 120; ++x) {
            mark(appearance, x, y);
        }
    }

    const auto tip = touchplus::tracking::analyze_appearance_silhouette_v5(
        appearance, core, width, height);

    // No-hand regression: an 11x9 changed patch is below the physical hand
    // minimum and must not become a valid component.
    std::vector<uint8_t> noise(cells, 0);
    std::vector<uint8_t> noise_core(cells, 0);
    for (int y = 20; y < 29; ++y) {
        for (int x = 150; x < 161; ++x) {
            mark(noise, x, y);
            if ((x + y) % 2 == 0) mark(noise_core, x, y);
        }
    }
    const auto noise_tip =
        touchplus::tracking::analyze_appearance_silhouette_v5(
            noise, noise_core, width, height);

    const bool static_background_rejected =
        !touchplus::tracking::foreground_against_background_v4(
            40, 40.2, 20, 55.0, 40.0);
    const bool closer_foreground_accepted =
        touchplus::tracking::foreground_against_background_v4(
            44, 40.0, 20, 55.0, 4.0);

    std::cout
        << "TouchPlus Phase 2B.5 appearance-silhouette fingertip self-test\n"
        << "silhouette cells       : " << tip.silhouette_cells << "\n"
        << "depth core cells       : " << tip.depth_core_cells << "\n"
        << "silhouette tip grid    : " << tip.gx << "," << tip.gy << "\n"
        << "geodesic steps         : " << tip.geodesic_steps << "\n"
        << "small noise rejected   : " << (!noise_tip.valid) << "\n"
        << "static bg rejected     : " << static_background_rejected << "\n"
        << "closer fg accepted     : " << closer_foreground_accepted << "\n";

    const bool pass =
        tip.valid &&
        tip.silhouette_cells > 1000 &&
        tip.depth_core_cells > 200 &&
        tip.gx >= 154 && tip.gx <= 166 &&
        tip.gy >= 184 &&
        tip.geodesic_steps >= 90 &&
        !noise_tip.valid &&
        static_background_rejected &&
        closer_foreground_accepted;

    if (!pass) {
        std::cerr
            << "PHASE 2B.5 APPEARANCE-SILHOUETTE FINGERTIP SELF-TEST: FAIL\n";
        return 1;
    }

    std::cout
        << "PHASE 2B.5 APPEARANCE-SILHOUETTE FINGERTIP SELF-TEST: PASS\n";
    return 0;
}
