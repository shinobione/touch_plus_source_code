#include "fingertip_tracker_v3.h"

#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using touchplus::tracking::GridSample;
    using touchplus::surface::SurfacePoint;

    std::vector<GridSample> samples;

    // Canonical desk setup: forearm enters from TOP of image, broad palm sits
    // below it, and one index finger extends DOWN toward the work surface.
    // This intentionally mirrors the physical smoke that defeated the old
    // centroid-radius scorer by making the wrist side a strong radial extremity.

    // Forearm / wrist entry band.
    for (int gy = 20; gy <= 78; ++gy) {
        for (int gx = 151; gx <= 169; ++gx) {
            const double h = 82.0 + 3.0 * std::sin(gx * 0.17);
            samples.push_back({gx, gy, SurfacePoint{(gx - 160) * 2.0, (gy - 110) * 2.0, h}, 40.0});
        }
    }

    // Palm.
    for (int gy = 70; gy <= 132; ++gy) {
        for (int gx = 132; gx <= 188; ++gx) {
            const double nx = (gx - 160) / 28.0;
            const double ny = (gy - 102) / 31.0;
            if (nx * nx + ny * ny > 1.0) continue;
            const double h = 58.0 + 5.0 * std::sin(gx * 0.13) * std::cos(gy * 0.11);
            samples.push_back({gx, gy, SurfacePoint{(gx - 160) * 2.0, (gy - 110) * 2.0, h}, 42.0});
        }
    }

    // Extended index finger: distal end is LOWER in image and LOWER in H.
    for (int gy = 124; gy <= 192; ++gy) {
        const int half_width = gy > 176 ? 3 : 5;
        for (int gx = 160 - half_width; gx <= 160 + half_width; ++gx) {
            const double t = static_cast<double>(gy - 124) / 68.0;
            const double h = 46.0 - t * 28.0;
            samples.push_back({gx, gy, SurfacePoint{(gx - 160) * 2.0, (gy - 110) * 2.0, h}, 52.0});
        }
    }

    // Two shorter folded-finger protrusions. They are real distal branches but
    // must lose to the much longer extended index geodesically.
    for (int gy = 122; gy <= 148; ++gy) {
        for (int gx = 140; gx <= 147; ++gx) {
            samples.push_back({gx, gy, SurfacePoint{(gx - 160) * 2.0, (gy - 110) * 2.0, 42.0}, 48.0});
        }
        for (int gx = 175; gx <= 182; ++gx) {
            samples.push_back({gx, gy, SurfacePoint{(gx - 160) * 2.0, (gy - 110) * 2.0, 44.0}, 47.0});
        }
    }

    // Real-scene regression from 2B.2: a giant false component contains far
    // more cells than the hand. Hardened segmentation must still reject it.
    for (int gy = 18; gy <= 138; ++gy) {
        for (int gx = 4; gx <= 114; ++gx) {
            const double h = 28.0 + 2.0 * std::sin(gx * 0.11) * std::cos(gy * 0.07);
            samples.push_back({gx, gy, SurfacePoint{-420.0 + gx * 3.0, -260.0 + gy * 3.0, h}, 34.0});
        }
    }

    const auto segmented = touchplus::tracking::analyze_surface_samples_v2(
        samples, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);

    const auto tip = touchplus::tracking::geodesic_tip_from_top_wrist_v3(
        samples,
        segmented.selected_mask,
        touchplus::depth::kDepthWidth,
        touchplus::depth::kDepthHeight);

    std::cout << "TouchPlus Phase 2B.3 geodesic fingertip self-test\n"
              << "foreground samples : " << segmented.foreground_samples << "\n"
              << "selected hand      : " << segmented.component_samples << "\n"
              << "geodesic tip grid  : " << tip.gx << "," << tip.gy << "\n"
              << "geodesic steps     : " << tip.geodesic_steps << "\n"
              << "candidate H        : " << tip.sample.surface.h_mm << " mm\n";

    const bool pass = segmented.valid &&
        segmented.component_samples > 1000 && segmented.component_samples < 8000 &&
        tip.valid &&
        tip.gx >= 154 && tip.gx <= 166 &&
        tip.gy >= 180 &&
        tip.sample.surface.h_mm <= 28.0 &&
        tip.geodesic_steps >= 80;

    if (!pass) {
        std::cerr << "PHASE 2B.3 GEODESIC FINGERTIP SELF-TEST: FAIL\n";
        return 1;
    }

    std::cout << "PHASE 2B.3 GEODESIC FINGERTIP SELF-TEST: PASS\n";
    return 0;
}
