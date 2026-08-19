#pragma once

// Replace the first-generation all-points least-squares + MAD surface fit with
// a deterministic dominant-plane consensus pass. A handful of stable wrong
// stereo matches or accidentally sampled non-table surfaces must not be able to
// inflate the robust threshold until every point becomes an "inlier".
#define fit_surface_robust fit_surface_robust_legacy
#include "surface_frame_math.h"
#undef fit_surface_robust

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace touchplus::surface {
namespace robust_surface_detail {

constexpr double kSeedThresholdMm = 6.0;
constexpr double kRefineMinThresholdMm = 2.0;
constexpr double kRefineMaxThresholdMm = 6.0;

inline double coefficient_residual_mm(
    const std::array<double, 3>& coeff,
    const Vec3& point) {

    // z = a*x + b*y + c -> a*x + b*y - z + c = 0.
    const double scale = std::sqrt(coeff[0] * coeff[0] + coeff[1] * coeff[1] + 1.0);
    if (scale < 1e-12) {
        return std::numeric_limits<double>::infinity();
    }
    return std::abs(coeff[0] * point.x + coeff[1] * point.y - point.z + coeff[2]) / scale;
}

inline std::vector<size_t> inliers_for(
    const std::vector<Vec3>& points,
    const std::array<double, 3>& coeff,
    double threshold_mm) {

    std::vector<size_t> inliers;
    inliers.reserve(points.size());
    for (size_t index = 0; index < points.size(); ++index) {
        if (coefficient_residual_mm(coeff, points[index]) <= threshold_mm) {
            inliers.push_back(index);
        }
    }
    return inliers;
}

inline double median_residual(
    const std::vector<Vec3>& points,
    const std::vector<size_t>& indices,
    const std::array<double, 3>& coeff) {

    std::vector<double> residuals;
    residuals.reserve(indices.size());
    for (const size_t index : indices) {
        residuals.push_back(coefficient_residual_mm(coeff, points[index]));
    }
    return median(std::move(residuals));
}

inline double coverage_score(
    const std::string& serial,
    const std::vector<Vec3>& points,
    const std::vector<size_t>& indices,
    const std::array<double, 3>& coeff) {

    const SurfaceModel candidate = model_from_coefficients(serial, points, indices, coeff);
    return std::min(candidate.spread_x_mm, candidate.spread_y_mm);
}

} // namespace robust_surface_detail

inline SurfaceModel fit_surface_robust(
    const std::string& serial,
    const std::vector<Vec3>& points) {

    using namespace robust_surface_detail;

    if (points.size() < 6) {
        throw std::runtime_error("Surface fit requires at least 6 sampled points");
    }

    // N is tiny in the interactive calibrator (normally 8-25). Enumerating all
    // triplets is therefore cheap, deterministic, and much easier to audit than
    // a random RANSAC seed. Score first by consensus count, then by useful plane
    // coverage, then by residual quality.
    std::vector<size_t> best_inliers;
    std::array<double, 3> best_coeff{};
    double best_coverage = -1.0;
    double best_median = std::numeric_limits<double>::infinity();

    for (size_t i = 0; i + 2 < points.size(); ++i) {
        for (size_t j = i + 1; j + 1 < points.size(); ++j) {
            for (size_t k = j + 1; k < points.size(); ++k) {
                std::vector<size_t> seed{i, j, k};
                std::array<double, 3> coeff{};
                if (!fit_z_plane_coefficients(points, seed, coeff)) {
                    continue;
                }

                std::vector<size_t> inliers = inliers_for(points, coeff, kSeedThresholdMm);
                if (inliers.size() < 6) {
                    continue;
                }

                // Refitting on the full candidate consensus prevents a lucky
                // three-point seed from winning on its own interpolation.
                if (!fit_z_plane_coefficients(points, inliers, coeff)) {
                    continue;
                }
                inliers = inliers_for(points, coeff, kSeedThresholdMm);
                if (inliers.size() < 6 || !fit_z_plane_coefficients(points, inliers, coeff)) {
                    continue;
                }

                const double coverage = coverage_score(serial, points, inliers, coeff);
                const double med = median_residual(points, inliers, coeff);
                const bool better =
                    inliers.size() > best_inliers.size() ||
                    (inliers.size() == best_inliers.size() && coverage > best_coverage + 1e-9) ||
                    (inliers.size() == best_inliers.size() && std::abs(coverage - best_coverage) <= 1e-9 && med < best_median);
                if (better) {
                    best_inliers = std::move(inliers);
                    best_coeff = coeff;
                    best_coverage = coverage;
                    best_median = med;
                }
            }
        }
    }

    if (best_inliers.size() < 6) {
        throw std::runtime_error(
            "No dominant working plane found within 6 mm. Keep only one physical surface and add more textured samples.");
    }

    // Tight robust refinement around the dominant plane. Unlike the old path,
    // the threshold is physically capped at 6 mm, so 50-100 mm mistakes can
    // never self-justify as inliers simply by widening MAD.
    std::array<double, 3> coeff = best_coeff;
    std::vector<size_t> inliers = best_inliers;
    for (int iteration = 0; iteration < 3; ++iteration) {
        if (!fit_z_plane_coefficients(points, inliers, coeff)) {
            throw std::runtime_error("Dominant surface consensus became degenerate");
        }

        std::vector<double> residuals;
        residuals.reserve(inliers.size());
        for (const size_t index : inliers) {
            residuals.push_back(coefficient_residual_mm(coeff, points[index]));
        }
        const double med = median(residuals);
        std::vector<double> deviations;
        deviations.reserve(residuals.size());
        for (const double value : residuals) {
            deviations.push_back(std::abs(value - med));
        }
        const double mad = median(std::move(deviations));
        const double sigma = 1.4826 * (std::isfinite(mad) ? mad : 0.0);
        const double threshold = std::clamp(
            med + 3.5 * sigma,
            kRefineMinThresholdMm,
            kRefineMaxThresholdMm);

        std::vector<size_t> refined = inliers_for(points, coeff, threshold);
        if (refined.size() < 6) {
            break;
        }
        if (refined == inliers) {
            break;
        }
        inliers = std::move(refined);
    }

    if (!fit_z_plane_coefficients(points, inliers, coeff)) {
        throw std::runtime_error("Unable to refit final dominant surface plane");
    }
    return model_from_coefficients(serial, points, inliers, coeff);
}

} // namespace touchplus::surface
