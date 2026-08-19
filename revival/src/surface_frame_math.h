#pragma once

#include "depth_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace touchplus::surface {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(const Vec3& a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline Vec3 operator/(const Vec3& a, double s) {
    return {a.x / s, a.y / s, a.z / s};
}
inline double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
inline double norm(const Vec3& v) {
    return std::sqrt(dot(v, v));
}
inline Vec3 normalized(const Vec3& v) {
    const double n = norm(v);
    if (n < 1e-12) {
        throw std::runtime_error("Cannot normalize near-zero vector");
    }
    return v / n;
}

inline double median(std::vector<double> values) {
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    double result = values[mid];
    if ((values.size() & 1U) == 0U) {
        const auto lower = std::max_element(values.begin(), values.begin() + mid);
        result = (*lower + result) * 0.5;
    }
    return result;
}

inline Vec3 camera_point_from_q(
    const touchplus::depth::Calibration& c,
    double u,
    double v,
    double disparity) {

    const auto& q = c.Q;
    const double xh = q[0] * u + q[1] * v + q[2] * disparity + q[3];
    const double yh = q[4] * u + q[5] * v + q[6] * disparity + q[7];
    const double zh = q[8] * u + q[9] * v + q[10] * disparity + q[11];
    const double wh = q[12] * u + q[13] * v + q[14] * disparity + q[15];
    if (std::abs(wh) < 1e-12) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan, nan};
    }
    return {xh / wh, yh / wh, zh / wh};
}

struct SurfaceModel {
    bool valid = false;
    std::string serial;
    Vec3 origin_camera_mm{};
    Vec3 normal_camera{};      // points toward the camera; H>0 means above the surface
    Vec3 axis_x_camera{};
    Vec3 axis_y_camera{};
    double plane_d_mm = 0.0;   // n dot p + d = 0
    double fit_rms_mm = 0.0;
    double fit_max_mm = 0.0;
    double spread_x_mm = 0.0;
    double spread_y_mm = 0.0;
    size_t sample_count = 0;
    size_t inlier_count = 0;
};

struct SurfacePoint {
    double x_mm = 0.0;
    double y_mm = 0.0;
    double h_mm = 0.0;
};

inline SurfacePoint to_surface(const SurfaceModel& model, const Vec3& camera_point) {
    if (!model.valid) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan, nan};
    }
    const Vec3 delta = camera_point - model.origin_camera_mm;
    return {
        dot(delta, model.axis_x_camera),
        dot(delta, model.axis_y_camera),
        dot(delta, model.normal_camera)
    };
}

inline bool solve_3x3(double a[3][4], std::array<double, 3>& x) {
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 3; ++row) {
            if (std::abs(a[row][col]) > std::abs(a[pivot][col])) {
                pivot = row;
            }
        }
        if (std::abs(a[pivot][col]) < 1e-10) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k < 4; ++k) {
                std::swap(a[pivot][k], a[col][k]);
            }
        }
        const double div = a[col][col];
        for (int k = col; k < 4; ++k) {
            a[col][k] /= div;
        }
        for (int row = 0; row < 3; ++row) {
            if (row == col) continue;
            const double factor = a[row][col];
            for (int k = col; k < 4; ++k) {
                a[row][k] -= factor * a[col][k];
            }
        }
    }
    x = {a[0][3], a[1][3], a[2][3]};
    return true;
}

inline bool fit_z_plane_coefficients(
    const std::vector<Vec3>& points,
    const std::vector<size_t>& indices,
    std::array<double, 3>& coeff) {

    if (indices.size() < 3) {
        return false;
    }

    double sx = 0.0, sy = 0.0, sz = 0.0;
    double sxx = 0.0, syy = 0.0, sxy = 0.0;
    double sxz = 0.0, syz = 0.0;
    for (const size_t index : indices) {
        const Vec3& p = points[index];
        sx += p.x; sy += p.y; sz += p.z;
        sxx += p.x * p.x;
        syy += p.y * p.y;
        sxy += p.x * p.y;
        sxz += p.x * p.z;
        syz += p.y * p.z;
    }
    const double n = static_cast<double>(indices.size());
    double aug[3][4] = {
        {sxx, sxy, sx,  sxz},
        {sxy, syy, sy,  syz},
        {sx,  sy,  n,   sz }
    };
    return solve_3x3(aug, coeff);
}

inline SurfaceModel model_from_coefficients(
    const std::string& serial,
    const std::vector<Vec3>& points,
    const std::vector<size_t>& indices,
    const std::array<double, 3>& coeff) {

    // z = a*x + b*y + c -> a*x + b*y - z + c = 0.
    Vec3 normal{coeff[0], coeff[1], -1.0};
    double d = coeff[2];
    const double scale = norm(normal);
    normal = normal / scale;
    d /= scale;

    // The physical working surface is in front of the camera. Orient the
    // normal toward the camera origin so hand/hover height is positive.
    if (d < 0.0) {
        normal = normal * -1.0;
        d = -d;
    }

    Vec3 centroid{};
    for (const size_t index : indices) {
        centroid = centroid + points[index];
    }
    centroid = centroid / static_cast<double>(indices.size());
    const double centroid_distance = dot(normal, centroid) + d;
    const Vec3 origin = centroid - normal * centroid_distance;

    Vec3 axis_x{1.0, 0.0, 0.0};
    axis_x = axis_x - normal * dot(axis_x, normal);
    if (norm(axis_x) < 1e-6) {
        axis_x = Vec3{0.0, 1.0, 0.0} - normal * normal.y;
    }
    axis_x = normalized(axis_x);
    const Vec3 axis_y = normalized(cross(normal, axis_x));

    SurfaceModel model;
    model.valid = true;
    model.serial = serial;
    model.origin_camera_mm = origin;
    model.normal_camera = normal;
    model.axis_x_camera = axis_x;
    model.axis_y_camera = axis_y;
    model.plane_d_mm = d;
    model.sample_count = points.size();
    model.inlier_count = indices.size();

    double sum_sq = 0.0;
    double max_abs = 0.0;
    double min_x = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    for (const size_t index : indices) {
        const Vec3 delta = points[index] - origin;
        const double residual = dot(delta, normal);
        sum_sq += residual * residual;
        max_abs = std::max(max_abs, std::abs(residual));
        const double px = dot(delta, axis_x);
        const double py = dot(delta, axis_y);
        min_x = std::min(min_x, px); max_x = std::max(max_x, px);
        min_y = std::min(min_y, py); max_y = std::max(max_y, py);
    }
    model.fit_rms_mm = std::sqrt(sum_sq / static_cast<double>(indices.size()));
    model.fit_max_mm = max_abs;
    model.spread_x_mm = max_x - min_x;
    model.spread_y_mm = max_y - min_y;
    return model;
}

inline SurfaceModel fit_surface_robust(
    const std::string& serial,
    const std::vector<Vec3>& points) {

    if (points.size() < 6) {
        throw std::runtime_error("Surface fit requires at least 6 sampled points");
    }

    std::vector<size_t> all(points.size());
    std::iota(all.begin(), all.end(), 0);
    std::array<double, 3> coeff{};
    if (!fit_z_plane_coefficients(points, all, coeff)) {
        throw std::runtime_error("Surface samples are degenerate; spread points across the working area");
    }

    SurfaceModel initial = model_from_coefficients(serial, points, all, coeff);
    std::vector<double> abs_residuals;
    abs_residuals.reserve(points.size());
    for (const Vec3& point : points) {
        abs_residuals.push_back(std::abs(dot(initial.normal_camera, point) + initial.plane_d_mm));
    }
    const double med = median(abs_residuals);
    std::vector<double> deviations;
    deviations.reserve(abs_residuals.size());
    for (const double value : abs_residuals) {
        deviations.push_back(std::abs(value - med));
    }
    const double mad = median(deviations);
    const double robust_sigma = 1.4826 * (std::isfinite(mad) ? mad : 0.0);
    const double threshold = std::max(2.0, med + 3.5 * robust_sigma);

    std::vector<size_t> inliers;
    for (size_t i = 0; i < abs_residuals.size(); ++i) {
        if (abs_residuals[i] <= threshold) {
            inliers.push_back(i);
        }
    }
    if (inliers.size() < 6) {
        throw std::runtime_error("Surface fit rejected too many samples; capture more textured points");
    }
    if (!fit_z_plane_coefficients(points, inliers, coeff)) {
        throw std::runtime_error("Surface inliers are degenerate; spread points across the working area");
    }
    return model_from_coefficients(serial, points, inliers, coeff);
}

inline const char* confidence(const SurfaceModel& model) {
    if (!model.valid) return "NONE";
    if (model.inlier_count >= 8 && model.fit_rms_mm <= 2.0 && model.fit_max_mm <= 5.0 &&
        model.spread_x_mm >= 80.0 && model.spread_y_mm >= 80.0) {
        return "HIGH";
    }
    if (model.inlier_count >= 6 && model.fit_rms_mm <= 5.0 && model.fit_max_mm <= 12.0 &&
        model.spread_x_mm >= 40.0 && model.spread_y_mm >= 40.0) {
        return "MEDIUM";
    }
    return "LOW";
}

inline void save_surface_model(const std::filesystem::path& path, const SurfaceModel& model) {
    if (!model.valid) {
        throw std::runtime_error("Refusing to save invalid surface model");
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Unable to save surface model: " + path.string());
    }
    out << std::fixed << std::setprecision(9);
    out << "{\n";
    out << "  \"schema\": \"touchplus-revival-surface-frame-v1\",\n";
    out << "  \"device_serial\": \"" << model.serial << "\",\n";
    out << "  \"sample_count\": " << model.sample_count << ",\n";
    out << "  \"inlier_count\": " << model.inlier_count << ",\n";
    out << "  \"fit_rms_mm\": " << model.fit_rms_mm << ",\n";
    out << "  \"fit_max_mm\": " << model.fit_max_mm << ",\n";
    out << "  \"spread_x_mm\": " << model.spread_x_mm << ",\n";
    out << "  \"spread_y_mm\": " << model.spread_y_mm << ",\n";
    out << "  \"origin_camera_mm\": [" << model.origin_camera_mm.x << ", " << model.origin_camera_mm.y << ", " << model.origin_camera_mm.z << "],\n";
    out << "  \"normal_camera\": [" << model.normal_camera.x << ", " << model.normal_camera.y << ", " << model.normal_camera.z << "],\n";
    out << "  \"axis_x_camera\": [" << model.axis_x_camera.x << ", " << model.axis_x_camera.y << ", " << model.axis_x_camera.z << "],\n";
    out << "  \"axis_y_camera\": [" << model.axis_y_camera.x << ", " << model.axis_y_camera.y << ", " << model.axis_y_camera.z << "],\n";
    out << "  \"plane_d_mm\": " << model.plane_d_mm << "\n";
    out << "}\n";
}

inline SurfaceModel load_surface_model(const std::filesystem::path& path, const std::string& expected_serial) {
    const std::string json = touchplus::depth::read_text_file(path);
    const std::string serial = touchplus::depth::json_string(json, "device_serial");
    if (serial != expected_serial) {
        throw std::runtime_error("Surface model serial mismatch: expected=" + expected_serial + " file=" + serial);
    }
    const auto origin = touchplus::depth::json_fixed_array<3>(json, "origin_camera_mm");
    const auto normal = touchplus::depth::json_fixed_array<3>(json, "normal_camera");
    const auto axis_x = touchplus::depth::json_fixed_array<3>(json, "axis_x_camera");
    const auto axis_y = touchplus::depth::json_fixed_array<3>(json, "axis_y_camera");

    SurfaceModel model;
    model.valid = true;
    model.serial = serial;
    model.origin_camera_mm = {origin[0], origin[1], origin[2]};
    model.normal_camera = normalized({normal[0], normal[1], normal[2]});
    model.axis_x_camera = normalized({axis_x[0], axis_x[1], axis_x[2]});
    model.axis_y_camera = normalized({axis_y[0], axis_y[1], axis_y[2]});
    model.plane_d_mm = -dot(model.normal_camera, model.origin_camera_mm);
    return model;
}

} // namespace touchplus::surface
