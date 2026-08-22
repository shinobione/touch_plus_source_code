#include "fingertip_refiner_v10.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using touchplus::tracking::DistalRefineStatusV10;
using touchplus::tracking::distal_refine_status_name_v10;
using touchplus::tracking::refine_distal_tip_v10;

namespace {

constexpr int kW = 640;
constexpr int kH = 480;
constexpr int kMW = 320;
constexpr int kMH = 240;
constexpr int kScale = 2;

struct Scene {
    std::vector<std::uint8_t> current = std::vector<std::uint8_t>(static_cast<std::size_t>(kW) * kH, 100);
    std::vector<std::uint8_t> background = std::vector<std::uint8_t>(static_cast<std::size_t>(kW) * kH, 100);
    std::vector<std::uint8_t> mask = std::vector<std::uint8_t>(static_cast<std::size_t>(kMW) * kMH, 0);
};

void paint_rect(Scene& s, int x0, int y0, int x1, int y1, std::uint8_t value = 155, bool mask = true) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (x < 0 || x >= kW || y < 0 || y >= kH) continue;
            s.current[static_cast<std::size_t>(y) * kW + x] = value;
            if (mask) {
                const int gx = x / kScale;
                const int gy = y / kScale;
                if (gx >= 0 && gx < kMW && gy >= 0 && gy < kMH) {
                    s.mask[static_cast<std::size_t>(gy) * kMW + gx] = 1;
                }
            }
        }
    }
}

bool expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "[FAIL] " << message << "\n";
    return false;
}

bool test_clean_distal_extension() {
    Scene s;
    paint_rect(s, 312, 190, 328, 270);

    const auto r = refine_distal_tip_v10(
        s.current, s.background, kW, kH,
        s.mask, kMW, kMH, kScale,
        320, 246, 320.0, 180.0, 0.0, 1.0);

    bool ok = true;
    ok &= expect(r.accepted, "clean finger must refine");
    ok &= expect(r.status == DistalRefineStatusV10::Accepted, "clean finger status must be ACCEPT");
    ok &= expect(r.refined_y >= 266 && r.refined_y <= 270, "refined point must reach distal cap");
    ok &= expect(r.refined_x >= 316 && r.refined_x <= 324, "refined point must stay near finger centerline");
    ok &= expect(r.forward_px >= 18.0, "clean refinement must move materially outward");
    ok &= expect(r.lateral_px <= 5.0, "clean refinement must remain laterally bounded");
    return ok;
}

bool test_disconnected_neighbor_not_reidentified() {
    Scene s;
    paint_rect(s, 312, 190, 326, 266);
    // Larger neighboring digit-like blob inside the local search box, but not
    // connected to the coarse/inward anchor. The refiner must not jump to it.
    paint_rect(s, 333, 224, 343, 276);

    const auto r = refine_distal_tip_v10(
        s.current, s.background, kW, kH,
        s.mask, kMW, kMH, kScale,
        319, 244, 319.0, 180.0, 0.0, 1.0);

    bool ok = true;
    ok &= expect(r.accepted, "anchored finger should still refine with neighbor present");
    ok &= expect(r.refined_x < 330, "refiner must not re-identify disconnected neighboring digit");
    ok &= expect(r.refined_y >= 261, "anchored component distal end should be selected");
    return ok;
}

bool test_noise_without_anchor_rejects() {
    Scene s;
    paint_rect(s, 330, 258, 340, 272);

    const auto r = refine_distal_tip_v10(
        s.current, s.background, kW, kH,
        s.mask, kMW, kMH, kScale,
        310, 230, 310.0, 170.0, 0.0, 1.0);

    return expect(!r.accepted, "disconnected local foreground must not be accepted");
}

bool test_axis_fallback_to_palm_ray() {
    Scene s;
    paint_rect(s, 392, 212, 408, 292);

    const auto r = refine_distal_tip_v10(
        s.current, s.background, kW, kH,
        s.mask, kMW, kMH, kScale,
        400, 268, 400.0, 200.0, 0.0, 0.0);

    bool ok = true;
    ok &= expect(r.accepted, "palm->coarse fallback axis should refine");
    ok &= expect(r.refined_y >= 288, "fallback axis should still reach distal cap");
    return ok;
}

bool test_missing_distal_mask_support_rejects() {
    Scene s;
    paint_rect(s, 252, 180, 268, 260, 155, false);
    // Only mark the proximal section in the modern silhouette mask.
    for (int y = 180; y <= 232; ++y) {
        for (int x = 252; x <= 268; ++x) {
            s.mask[static_cast<std::size_t>(y / kScale) * kMW + (x / kScale)] = 1;
        }
    }

    const auto r = refine_distal_tip_v10(
        s.current, s.background, kW, kH,
        s.mask, kMW, kMH, kScale,
        260, 232, 260.0, 170.0, 0.0, 1.0);

    bool ok = true;
    ok &= expect(!r.accepted, "candidate without current distal silhouette support must fail closed");
    ok &= expect(r.status == DistalRefineStatusV10::DistalSupportWeak ||
                 r.status == DistalRefineStatusV10::NoForeground,
                 std::string("unexpected rejection status: ") + distal_refine_status_name_v10(r.status));
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= test_clean_distal_extension();
    ok &= test_disconnected_neighbor_not_reidentified();
    ok &= test_noise_without_anchor_rejects();
    ok &= test_axis_fallback_to_palm_ray();
    ok &= test_missing_distal_mask_support_rejects();

    if (!ok) return 1;
    std::cout << "[PASS] Revival hybrid Ractiv-style distal refiner synthetic regression\n";
    return 0;
}
