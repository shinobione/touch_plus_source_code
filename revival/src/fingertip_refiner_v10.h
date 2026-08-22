#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace touchplus::tracking {

// Hybrid Ractiv -> Revival experiment.
//
// Ractiv physical recovery showed one genuinely useful idea: once fingertip
// identity is already correct, a small full-resolution foreground window can
// move a coarse/proximal index candidate toward the real distal skin boundary.
//
// This helper ports only that idea. It deliberately does NOT import Ractiv's
// OpenCV 2.4 code, Reprojector, calibration CDN, PointerMapper or contact logic.
// Modern Revival remains the sole owner of anatomy identity, stereo/Q, surface
// coordinates and all fail-closed decisions.

enum class DistalRefineStatusV10 {
    NotRun,
    Accepted,
    InvalidInput,
    NoBackground,
    AxisInvalid,
    NoForeground,
    NoAnchoredComponent,
    DistalSupportWeak,
    ShiftTooLarge,
    LateralDrift,
    MovedTowardPalm
};

inline const char* distal_refine_status_name_v10(DistalRefineStatusV10 status) {
    switch (status) {
        case DistalRefineStatusV10::Accepted: return "ACCEPT";
        case DistalRefineStatusV10::InvalidInput: return "INVALID_INPUT";
        case DistalRefineStatusV10::NoBackground: return "NO_BACKGROUND";
        case DistalRefineStatusV10::AxisInvalid: return "AXIS_INVALID";
        case DistalRefineStatusV10::NoForeground: return "NO_FOREGROUND";
        case DistalRefineStatusV10::NoAnchoredComponent: return "NO_ANCHORED_COMPONENT";
        case DistalRefineStatusV10::DistalSupportWeak: return "DISTAL_SUPPORT_WEAK";
        case DistalRefineStatusV10::ShiftTooLarge: return "SHIFT_TOO_LARGE";
        case DistalRefineStatusV10::LateralDrift: return "LATERAL_DRIFT";
        case DistalRefineStatusV10::MovedTowardPalm: return "MOVED_TOWARD_PALM";
        default: return "NOT_RUN";
    }
}

struct DistalRefineResultV10 {
    bool accepted = false;
    DistalRefineStatusV10 status = DistalRefineStatusV10::NotRun;
    int coarse_x = -1;
    int coarse_y = -1;
    int refined_x = -1;
    int refined_y = -1;
    int component_pixels = 0;
    double shift_px = 0.0;
    double forward_px = 0.0;
    double lateral_px = 0.0;
    double axis_dx = 0.0;
    double axis_dy = 0.0;
};

namespace distal_refiner_detail_v10 {

constexpr int kBackwardPx = 18;
constexpr int kForwardPx = 32;
constexpr int kLateralPx = 15;
constexpr int kAppearanceDelta = 18;
constexpr int kMinComponentPixels = 10;
constexpr double kMaxShiftPx = 31.0;
constexpr double kMaxLateralResultPx = 13.0;
constexpr double kMinForwardPx = -1.5;

inline bool mask_near(
    const std::vector<std::uint8_t>& mask,
    int width,
    int height,
    int full_x,
    int full_y,
    int scale,
    int radius) {

    if (mask.empty() || width <= 0 || height <= 0 || scale <= 0) return false;
    const int gx = full_x / scale;
    const int gy = full_y / scale;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = gx + dx;
            const int y = gy + dy;
            if (x < 0 || x >= width || y < 0 || y >= height) continue;
            if (mask[static_cast<std::size_t>(y) * width + x]) return true;
        }
    }
    return false;
}

inline double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
    double out = values[mid];
    if ((values.size() & 1U) == 0U) {
        const auto lo = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid));
        out = (*lo + out) * 0.5;
    }
    return out;
}

struct Pixel {
    int x = 0;
    int y = 0;
    double forward = 0.0;
    double lateral = 0.0;
};

} // namespace distal_refiner_detail_v10

inline DistalRefineResultV10 refine_distal_tip_v10(
    const std::vector<std::uint8_t>& current_gray,
    const std::vector<std::uint8_t>& background_gray,
    int image_width,
    int image_height,
    const std::vector<std::uint8_t>& hand_mask,
    int mask_width,
    int mask_height,
    int depth_scale,
    int coarse_x,
    int coarse_y,
    double palm_x,
    double palm_y,
    double anatomy_axis_dx,
    double anatomy_axis_dy) {

    using namespace distal_refiner_detail_v10;

    DistalRefineResultV10 out;
    out.coarse_x = coarse_x;
    out.coarse_y = coarse_y;

    const std::size_t pixels = static_cast<std::size_t>(image_width) * image_height;
    const std::size_t mask_cells = static_cast<std::size_t>(mask_width) * mask_height;
    if (image_width <= 0 || image_height <= 0 || mask_width <= 0 || mask_height <= 0 ||
        depth_scale <= 0 || current_gray.size() < pixels || hand_mask.size() != mask_cells ||
        coarse_x < 0 || coarse_x >= image_width || coarse_y < 0 || coarse_y >= image_height) {
        out.status = DistalRefineStatusV10::InvalidInput;
        return out;
    }
    if (background_gray.size() < pixels) {
        out.status = DistalRefineStatusV10::NoBackground;
        return out;
    }

    double axis_x = anatomy_axis_dx;
    double axis_y = anatomy_axis_dy;
    double axis_norm = std::hypot(axis_x, axis_y);
    if (!std::isfinite(axis_norm) || axis_norm < 0.50) {
        axis_x = static_cast<double>(coarse_x) - palm_x;
        axis_y = static_cast<double>(coarse_y) - palm_y;
        axis_norm = std::hypot(axis_x, axis_y);
    }
    if (!std::isfinite(axis_norm) || axis_norm < 8.0) {
        out.status = DistalRefineStatusV10::AxisInvalid;
        return out;
    }
    axis_x /= axis_norm;
    axis_y /= axis_norm;
    out.axis_dx = axis_x;
    out.axis_dy = axis_y;

    const int radius = kForwardPx + kLateralPx + 2;
    const int min_x = std::max(0, coarse_x - radius);
    const int max_x = std::min(image_width - 1, coarse_x + radius);
    const int min_y = std::max(0, coarse_y - radius);
    const int max_y = std::min(image_height - 1, coarse_y + radius);
    const int local_w = max_x - min_x + 1;
    const int local_h = max_y - min_y + 1;
    if (local_w <= 0 || local_h <= 0) {
        out.status = DistalRefineStatusV10::InvalidInput;
        return out;
    }

    std::vector<std::uint8_t> binary(static_cast<std::size_t>(local_w) * local_h, 0);
    int changed = 0;
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const double dx = static_cast<double>(x - coarse_x);
            const double dy = static_cast<double>(y - coarse_y);
            const double forward = dx * axis_x + dy * axis_y;
            const double lateral = std::abs(dx * axis_y - dy * axis_x);
            if (forward < -kBackwardPx || forward > kForwardPx || lateral > kLateralPx) continue;
            if (!mask_near(hand_mask, mask_width, mask_height, x, y, depth_scale, 2)) continue;

            const std::size_t idx = static_cast<std::size_t>(y) * image_width + x;
            const int delta = std::abs(static_cast<int>(current_gray[idx]) - static_cast<int>(background_gray[idx]));
            if (delta < kAppearanceDelta) continue;
            binary[static_cast<std::size_t>(y - min_y) * local_w + (x - min_x)] = 1;
            ++changed;
        }
    }
    if (changed < kMinComponentPixels) {
        out.status = DistalRefineStatusV10::NoForeground;
        return out;
    }

    std::vector<int> labels(binary.size(), -1);
    std::vector<int> queue;
    queue.reserve(binary.size());
    int next_label = 0;
    int chosen_label = -1;
    int chosen_size = 0;
    double chosen_anchor_d2 = std::numeric_limits<double>::infinity();

    constexpr int nx[8] = {-1,0,1,-1,1,-1,0,1};
    constexpr int ny[8] = {-1,-1,-1,0,0,1,1,1};

    for (int sy = 0; sy < local_h; ++sy) {
        for (int sx = 0; sx < local_w; ++sx) {
            const std::size_t seed = static_cast<std::size_t>(sy) * local_w + sx;
            if (!binary[seed] || labels[seed] >= 0) continue;

            queue.clear();
            queue.push_back(static_cast<int>(seed));
            labels[seed] = next_label;
            std::size_t head = 0;
            int component_size = 0;
            double anchor_d2 = std::numeric_limits<double>::infinity();

            while (head < queue.size()) {
                const int flat = queue[head++];
                const int ly = flat / local_w;
                const int lx = flat - ly * local_w;
                const int x = min_x + lx;
                const int y = min_y + ly;
                ++component_size;

                // Anchor against the coarse point and a few pixels inward.
                const double inward_x = static_cast<double>(coarse_x) - axis_x * 5.0;
                const double inward_y = static_cast<double>(coarse_y) - axis_y * 5.0;
                const double adx = static_cast<double>(x) - inward_x;
                const double ady = static_cast<double>(y) - inward_y;
                anchor_d2 = std::min(anchor_d2, adx * adx + ady * ady);

                for (int k = 0; k < 8; ++k) {
                    const int xx = lx + nx[k];
                    const int yy = ly + ny[k];
                    if (xx < 0 || xx >= local_w || yy < 0 || yy >= local_h) continue;
                    const std::size_t ni = static_cast<std::size_t>(yy) * local_w + xx;
                    if (!binary[ni] || labels[ni] >= 0) continue;
                    labels[ni] = next_label;
                    queue.push_back(static_cast<int>(ni));
                }
            }

            // The Ractiv lesson is local refinement, not re-identification.
            // Refuse a disconnected neighboring finger/blob even if it is larger.
            if (component_size >= kMinComponentPixels && anchor_d2 <= 10.0 * 10.0) {
                if (anchor_d2 < chosen_anchor_d2 - 1e-6 ||
                    (std::abs(anchor_d2 - chosen_anchor_d2) <= 1e-6 && component_size > chosen_size)) {
                    chosen_label = next_label;
                    chosen_size = component_size;
                    chosen_anchor_d2 = anchor_d2;
                }
            }
            ++next_label;
        }
    }

    if (chosen_label < 0) {
        out.status = DistalRefineStatusV10::NoAnchoredComponent;
        return out;
    }
    out.component_pixels = chosen_size;

    std::vector<Pixel> component;
    component.reserve(static_cast<std::size_t>(chosen_size));
    double max_forward = -std::numeric_limits<double>::infinity();
    for (int ly = 0; ly < local_h; ++ly) {
        for (int lx = 0; lx < local_w; ++lx) {
            const std::size_t idx = static_cast<std::size_t>(ly) * local_w + lx;
            if (labels[idx] != chosen_label) continue;
            const int x = min_x + lx;
            const int y = min_y + ly;
            const double dx = static_cast<double>(x - coarse_x);
            const double dy = static_cast<double>(y - coarse_y);
            const double forward = dx * axis_x + dy * axis_y;
            const double lateral = dx * axis_y - dy * axis_x;
            component.push_back({x, y, forward, lateral});
            max_forward = std::max(max_forward, forward);
        }
    }

    if (component.size() < static_cast<std::size_t>(kMinComponentPixels) || max_forward < -1.0) {
        out.status = DistalRefineStatusV10::DistalSupportWeak;
        return out;
    }

    // Use the median of the distal cap rather than one extreme pixel. This keeps
    // the point on the physical fingertip center instead of chasing single-pixel
    // noise at the silhouette edge.
    std::vector<double> distal_x;
    std::vector<double> distal_y;
    for (const auto& p : component) {
        if (p.forward < max_forward - 3.0) continue;
        if (std::abs(p.lateral) > kMaxLateralResultPx) continue;
        distal_x.push_back(static_cast<double>(p.x));
        distal_y.push_back(static_cast<double>(p.y));
    }
    if (distal_x.size() < 2) {
        out.status = DistalRefineStatusV10::DistalSupportWeak;
        return out;
    }

    out.refined_x = static_cast<int>(std::lround(median(std::move(distal_x))));
    out.refined_y = static_cast<int>(std::lround(median(std::move(distal_y))));

    const double rx = static_cast<double>(out.refined_x - coarse_x);
    const double ry = static_cast<double>(out.refined_y - coarse_y);
    out.shift_px = std::hypot(rx, ry);
    out.forward_px = rx * axis_x + ry * axis_y;
    out.lateral_px = std::abs(rx * axis_y - ry * axis_x);

    if (out.shift_px > kMaxShiftPx) {
        out.status = DistalRefineStatusV10::ShiftTooLarge;
        return out;
    }
    if (out.lateral_px > kMaxLateralResultPx) {
        out.status = DistalRefineStatusV10::LateralDrift;
        return out;
    }
    if (out.forward_px < kMinForwardPx) {
        out.status = DistalRefineStatusV10::MovedTowardPalm;
        return out;
    }

    // Require real current-hand support immediately inward from the proposed
    // full-res point. The outermost pixel itself may sit just beyond the coarse
    // 320x240 silhouette cell, hence the relaxed radius only at the candidate.
    int inward_support = 0;
    for (const int step : {4, 8, 12}) {
        const int ix = static_cast<int>(std::lround(out.refined_x - axis_x * step));
        const int iy = static_cast<int>(std::lround(out.refined_y - axis_y * step));
        if (mask_near(hand_mask, mask_width, mask_height, ix, iy, depth_scale, 1)) ++inward_support;
    }
    if (inward_support < 2 ||
        !mask_near(hand_mask, mask_width, mask_height, out.refined_x, out.refined_y, depth_scale, 2)) {
        out.status = DistalRefineStatusV10::DistalSupportWeak;
        return out;
    }

    out.accepted = true;
    out.status = DistalRefineStatusV10::Accepted;
    return out;
}

} // namespace touchplus::tracking
