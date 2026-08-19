#include "fingertip_tracker_v2.h"

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

    // Real-scene regression: a giant above-plane false component deliberately
    // contains far more cells than the hand. Phase 2B.1 selected the largest
    // component blindly and therefore treated broad depth clutter/table
    // quantization as a 20k-cell "hand". V2 must reject this component.
    for (int gy = 18; gy <= 138; ++gy) {
        for (int gx = 4; gx <= 114; ++gx) {
            const double h = 28.0 + 2.0 * std::sin(gx * 0.11) * std::cos(gy * 0.07);
            samples.push_back({gx, gy, SurfacePoint{-420.0 + gx * 3.0, -260.0 + gy * 3.0, h}, 34.0});
        }
    }

    // Smaller distractor above the plane that must also lose to the plausible
    // hand component.
    for (int gy = 160; gy < 166; ++gy) {
        for (int gx = 255; gx < 262; ++gx) {
            samples.push_back({gx, gy, SurfacePoint{180.0 + gx, 100.0 + gy, 80.0}, 30.0});
        }
    }

    const auto result = touchplus::tracking::analyze_surface_samples_v2(
        samples, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);

    std::cout << "TouchPlus Phase 2B.2 hardened geometry tracker self-test\n"
              << "foreground samples : " << result.foreground_samples << "\n"
              << "selected hand      : " << result.component_samples << "\n"
              << "candidate grid     : " << result.tip_gx << "," << result.tip_gy << "\n"
              << "candidate H        : " << result.coarse_tip.h_mm << " mm\n";

    const bool pass = result.valid &&
        result.component_samples > 500 && result.component_samples < 5000 &&
        result.tip_gx >= 150 && result.tip_gx <= 170 &&
        result.tip_gy <= 62 &&
        result.coarse_tip.h_mm <= 30.0;

    if (!pass) {
        std::cerr << "PHASE 2B.2 HARDENED FINGERTIP TRACKER SELF-TEST: FAIL\n";
        return 1;
    }

    std::cout << "PHASE 2B.2 HARDENED FINGERTIP TRACKER SELF-TEST: PASS\n";
    return 0;
}
