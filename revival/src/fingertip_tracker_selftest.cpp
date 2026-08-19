#include "fingertip_tracker.h"

#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using touchplus::tracking::GridSample;
    using touchplus::surface::SurfacePoint;

    std::vector<GridSample> samples;

    // Synthetic hand: broad palm + one extended, lower fingertip.
    for (int gy = 88; gy <= 132; ++gy) {
        for (int gx = 135; gx <= 185; ++gx) {
            const double nx = (gx - 160) / 25.0;
            const double ny = (gy - 110) / 22.0;
            if (nx * nx + ny * ny > 1.0) continue;
            const double h = 55.0 + 6.0 * std::sin(gx * 0.17) * std::cos(gy * 0.13);
            samples.push_back({gx, gy, SurfacePoint{(gx - 160) * 2.0, (gy - 120) * 2.0, h}, 40.0});
        }
    }

    // Extended index finger aimed upward in image/surface Y. Height decreases
    // toward the distal tip so the geometry-only extremity score has a clear
    // physically meaningful winner.
    for (int gy = 48; gy < 94; ++gy) {
        const int half_width = gy < 60 ? 3 : 5;
        for (int gx = 160 - half_width; gx <= 160 + half_width; ++gx) {
            const double t = static_cast<double>(gy - 48) / 46.0;
            const double h = 18.0 + t * 30.0;
            samples.push_back({gx, gy, SurfacePoint{(gx - 160) * 2.0, (gy - 120) * 2.0, h}, 52.0});
        }
    }

    // Distractor above the plane that must lose to the larger hand component.
    for (int gy = 160; gy < 166; ++gy) {
        for (int gx = 255; gx < 262; ++gx) {
            samples.push_back({gx, gy, SurfacePoint{180.0 + gx, 100.0 + gy, 80.0}, 30.0});
        }
    }

    const auto result = touchplus::tracking::analyze_surface_samples(
        samples, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);

    std::cout << "TouchPlus Phase 2B geometry tracker self-test\n"
              << "foreground samples : " << result.foreground_samples << "\n"
              << "hand samples       : " << result.component_samples << "\n"
              << "candidate grid     : " << result.tip_gx << "," << result.tip_gy << "\n"
              << "candidate H        : " << result.coarse_tip.h_mm << " mm\n";

    const bool pass = result.valid &&
        result.component_samples > 500 &&
        result.tip_gx >= 150 && result.tip_gx <= 170 &&
        result.tip_gy <= 62 &&
        result.coarse_tip.h_mm < 30.0;

    if (!pass) {
        std::cerr << "PHASE 2B FINGERTIP TRACKER SELF-TEST: FAIL\n";
        return 1;
    }

    std::cout << "PHASE 2B FINGERTIP TRACKER SELF-TEST: PASS\n";
    return 0;
}
