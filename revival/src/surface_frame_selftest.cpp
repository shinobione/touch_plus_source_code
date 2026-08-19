#include "surface_frame_math.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

int main() {
    using namespace touchplus::surface;

    // Deterministic tilted working plane with one gross outlier. The robust
    // fitter should recover the plane, reject the outlier, and preserve signed
    // perpendicular height toward the camera.
    std::vector<Vec3> points;
    for (int iy = -2; iy <= 2; ++iy) {
        for (int ix = -2; ix <= 2; ++ix) {
            const double x = static_cast<double>(ix) * 55.0;
            const double y = static_cast<double>(iy) * 45.0;
            const double noise = static_cast<double>((ix * 7 + iy * 11) % 5) * 0.08 - 0.16;
            const double z = 420.0 + 0.08 * x + 0.24 * y + noise;
            points.push_back({x, y, z});
        }
    }
    points.push_back({20.0, -15.0, 470.0}); // deliberate gross outlier

    const SurfaceModel model = fit_surface_robust("SELFTEST", points);
    std::cout << "TouchPlus Phase 2A surface-frame self-test\n"
              << "samples/inliers: " << model.sample_count << "/" << model.inlier_count << "\n"
              << "RMS: " << model.fit_rms_mm << " mm\n"
              << "max: " << model.fit_max_mm << " mm\n"
              << "spread: " << model.spread_x_mm << " x " << model.spread_y_mm << " mm\n"
              << "confidence: " << confidence(model) << "\n";

    const Vec3 above = model.origin_camera_mm + model.normal_camera * 50.0;
    const SurfacePoint surface = to_surface(model, above);
    std::cout << "synthetic height: " << surface.h_mm << " mm\n";

    const bool pass = model.valid &&
        model.inlier_count >= 24 && model.inlier_count < model.sample_count &&
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
