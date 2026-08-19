#include "surface_frame_robust.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

int main() {
    using namespace touchplus::surface;

    // Deterministic tilted working plane. Keep the noise sub-millimetric, then
    // contaminate it with a second structured surface plus gross depth mistakes.
    // The old all-points LS+MAD path could widen its threshold until almost
    // everything became an inlier; the Phase 2A fitter must recover the dominant
    // physical plane instead.
    std::vector<Vec3> points;
    for (int iy = -2; iy <= 2; ++iy) {
        for (int ix = -2; ix <= 2; ++ix) {
            const double x = static_cast<double>(ix) * 55.0;
            const double y = static_cast<double>(iy) * 45.0;
            const int noise_code = (ix * 7 + iy * 11 + 25) % 5;
            const double noise = static_cast<double>(noise_code) * 0.08 - 0.16;
            const double z = 420.0 + 0.08 * x + 0.24 * y + noise;
            points.push_back({x, y, z});
        }
    }

    // Seven coherent but WRONG samples from a different plane.
    for (int i = 0; i < 7; ++i) {
        const double x = -150.0 + static_cast<double>(i) * 45.0;
        const double y = -95.0 + static_cast<double>((i * 3) % 5) * 48.0;
        const double z = 495.0 - 0.18 * x + 0.05 * y;
        points.push_back({x, y, z});
    }

    // And two one-off catastrophic stereo mistakes.
    points.push_back({20.0, -15.0, 560.0});
    points.push_back({-75.0, 35.0, 305.0});

    const SurfaceModel model = fit_surface_robust("SELFTEST", points);
    std::cout << "TouchPlus Phase 2A dominant-surface self-test\n"
              << "samples/inliers: " << model.sample_count << "/" << model.inlier_count << "\n"
              << "rejected: " << (model.sample_count - model.inlier_count) << "\n"
              << "RMS: " << model.fit_rms_mm << " mm\n"
              << "max: " << model.fit_max_mm << " mm\n"
              << "spread: " << model.spread_x_mm << " x " << model.spread_y_mm << " mm\n"
              << "confidence: " << confidence(model) << "\n";

    const Vec3 above = model.origin_camera_mm + model.normal_camera * 50.0;
    const SurfacePoint surface = to_surface(model, above);
    std::cout << "synthetic height: " << surface.h_mm << " mm\n";

    const bool pass = model.valid &&
        model.sample_count == 34 &&
        model.inlier_count >= 24 && model.inlier_count <= 25 &&
        model.fit_rms_mm < 0.5 && model.fit_max_mm < 1.0 &&
        model.spread_x_mm > 180.0 && model.spread_y_mm > 150.0 &&
        std::abs(surface.h_mm - 50.0) < 1e-6 &&
        std::string(confidence(model)) == "HIGH";

    if (!pass) {
        std::cerr << "PHASE 2A SURFACE FRAME SELF-TEST: FAIL\n";
        return 1;
    }
    std::cout << "PHASE 2A SURFACE FRAME SELF-TEST: PASS\n";
    return 0;
}
