#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace touchplus::depth {

constexpr int kEyeWidth = 640;
constexpr int kEyeHeight = 480;
constexpr int kStereoWidth = 1280;
constexpr int kStereoHeight = 480;
constexpr int kDepthScale = 2;
constexpr int kDepthWidth = kEyeWidth / kDepthScale;
constexpr int kDepthHeight = kEyeHeight / kDepthScale;

struct Calibration {
    std::string serial;
    std::string promotion_state;
    std::array<double, 9> K1{};
    std::array<double, 5> D1{};
    std::array<double, 9> K2{};
    std::array<double, 5> D2{};
    std::array<double, 9> R1{};
    std::array<double, 9> R2{};
    std::array<double, 12> P1{};
    std::array<double, 12> P2{};
    std::array<double, 16> Q{};
};

inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open calibration file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

inline size_t find_key(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        throw std::runtime_error("Calibration JSON missing key: " + key);
    }
    return pos + needle.size();
}

inline std::string json_string(const std::string& json, const std::string& key) {
    size_t pos = find_key(json, key);
    pos = json.find(':', pos);
    if (pos == std::string::npos) {
        throw std::runtime_error("Malformed calibration JSON near key: " + key);
    }
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) {
        throw std::runtime_error("Malformed string value for calibration key: " + key);
    }
    const size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) {
        throw std::runtime_error("Unterminated string value for calibration key: " + key);
    }
    return json.substr(pos + 1, end - pos - 1);
}

inline std::vector<double> json_number_array(const std::string& json, const std::string& key) {
    size_t pos = find_key(json, key);
    pos = json.find(':', pos);
    if (pos == std::string::npos) {
        throw std::runtime_error("Malformed calibration JSON near key: " + key);
    }
    pos = json.find('[', pos + 1);
    if (pos == std::string::npos) {
        throw std::runtime_error("Expected array for calibration key: " + key);
    }

    std::vector<double> values;
    int depth = 0;
    const char* base = json.c_str();
    for (size_t i = pos; i < json.size();) {
        const char c = json[i];
        if (c == '[') {
            ++depth;
            ++i;
            continue;
        }
        if (c == ']') {
            --depth;
            ++i;
            if (depth == 0) {
                break;
            }
            continue;
        }

        const bool number_start = (c == '-') || (c == '+') || (c == '.') ||
            (c >= '0' && c <= '9');
        if (!number_start) {
            ++i;
            continue;
        }

        char* end = nullptr;
        const double value = std::strtod(base + i, &end);
        if (end == base + i) {
            ++i;
            continue;
        }
        values.push_back(value);
        i = static_cast<size_t>(end - base);
    }
    return values;
}

template <size_t N>
inline std::array<double, N> json_fixed_array(const std::string& json, const std::string& key) {
    const auto values = json_number_array(json, key);
    if (values.size() != N) {
        std::ostringstream oss;
        oss << "Calibration key " << key << " expected " << N
            << " numeric values but found " << values.size();
        throw std::runtime_error(oss.str());
    }
    std::array<double, N> result{};
    std::copy(values.begin(), values.end(), result.begin());
    return result;
}

inline Calibration load_calibration(const std::filesystem::path& path) {
    const std::string json = read_text_file(path);
    Calibration c;
    c.serial = json_string(json, "device_serial");
    c.promotion_state = json_string(json, "promotion_state");
    c.K1 = json_fixed_array<9>(json, "K1");
    c.D1 = json_fixed_array<5>(json, "D1");
    c.K2 = json_fixed_array<9>(json, "K2");
    c.D2 = json_fixed_array<5>(json, "D2");
    c.R1 = json_fixed_array<9>(json, "R1");
    c.R2 = json_fixed_array<9>(json, "R2");
    c.P1 = json_fixed_array<12>(json, "P1");
    c.P2 = json_fixed_array<12>(json, "P2");
    c.Q = json_fixed_array<16>(json, "Q");

    if (c.promotion_state != "candidate_physical_depth_validated") {
        throw std::runtime_error(
            "Calibration is not physically depth-validated: state=" + c.promotion_state);
    }
    return c;
}

struct MapPoint {
    float x = -1.0f;
    float y = -1.0f;
};

struct RectifyMap {
    std::vector<MapPoint> points;
    size_t valid_points = 0;
};

inline RectifyMap build_rectify_map(
    const std::array<double, 9>& K,
    const std::array<double, 5>& D,
    const std::array<double, 9>& R,
    const std::array<double, 12>& P) {

    RectifyMap map;
    map.points.resize(static_cast<size_t>(kEyeWidth) * kEyeHeight);

    const double fx_new = P[0];
    const double fy_new = P[5];
    const double cx_new = P[2];
    const double cy_new = P[6];

    if (fx_new == 0.0 || fy_new == 0.0) {
        throw std::runtime_error("Invalid rectified projection matrix focal length");
    }

    const double k1 = D[0];
    const double k2 = D[1];
    const double p1 = D[2];
    const double p2 = D[3];
    const double k3 = D[4];

    for (int v = 0; v < kEyeHeight; ++v) {
        for (int u = 0; u < kEyeWidth; ++u) {
            const double xr = (static_cast<double>(u) - cx_new) / fx_new;
            const double yr = (static_cast<double>(v) - cy_new) / fy_new;

            // OpenCV stereoRectify R1/R2 rotate original camera rays into the
            // common rectified frame. initUndistortRectifyMap therefore maps
            // a rectified output ray back through R^T before re-distortion.
            const double X = R[0] * xr + R[3] * yr + R[6];
            const double Y = R[1] * xr + R[4] * yr + R[7];
            const double Z = R[2] * xr + R[5] * yr + R[8];
            if (std::abs(Z) < 1e-12) {
                continue;
            }

            const double x = X / Z;
            const double y = Y / Z;
            const double r2 = x * x + y * y;
            const double r4 = r2 * r2;
            const double r6 = r4 * r2;
            const double radial = 1.0 + k1 * r2 + k2 * r4 + k3 * r6;
            const double xd = x * radial + 2.0 * p1 * x * y + p2 * (r2 + 2.0 * x * x);
            const double yd = y * radial + p1 * (r2 + 2.0 * y * y) + 2.0 * p2 * x * y;

            const double src_x = K[0] * xd + K[1] * yd + K[2];
            const double src_y = K[3] * xd + K[4] * yd + K[5];

            auto& point = map.points[static_cast<size_t>(v) * kEyeWidth + u];
            point.x = static_cast<float>(src_x);
            point.y = static_cast<float>(src_y);
            if (src_x >= 0.0 && src_x < static_cast<double>(kEyeWidth - 1) &&
                src_y >= 0.0 && src_y < static_cast<double>(kEyeHeight - 1)) {
                ++map.valid_points;
            }
        }
    }
    return map;
}

inline uint8_t clamp_u8(double value) {
    if (value <= 0.0) return 0;
    if (value >= 255.0) return 255;
    return static_cast<uint8_t>(value + 0.5);
}

inline void rectify_eye_bgra(
    const std::vector<uint8_t>& stereo_bgra,
    int source_eye_offset_x,
    const RectifyMap& map,
    std::vector<uint8_t>& rectified_bgra,
    std::vector<uint8_t>& rectified_gray) {

    const size_t pixel_count = static_cast<size_t>(kEyeWidth) * kEyeHeight;
    rectified_bgra.assign(pixel_count * 4, 0);
    rectified_gray.assign(pixel_count, 0);

    for (int y = 0; y < kEyeHeight; ++y) {
        for (int x = 0; x < kEyeWidth; ++x) {
            const size_t dst_pixel = static_cast<size_t>(y) * kEyeWidth + x;
            const MapPoint p = map.points[dst_pixel];
            const int x0 = static_cast<int>(std::floor(p.x));
            const int y0 = static_cast<int>(std::floor(p.y));
            if (x0 < 0 || y0 < 0 || x0 + 1 >= kEyeWidth || y0 + 1 >= kEyeHeight) {
                rectified_bgra[dst_pixel * 4 + 3] = 255;
                continue;
            }

            const double ax = static_cast<double>(p.x) - x0;
            const double ay = static_cast<double>(p.y) - y0;
            const double w00 = (1.0 - ax) * (1.0 - ay);
            const double w10 = ax * (1.0 - ay);
            const double w01 = (1.0 - ax) * ay;
            const double w11 = ax * ay;

            const auto sample_channel = [&](int sx, int sy, int channel) -> double {
                const size_t index =
                    (static_cast<size_t>(sy) * kStereoWidth + source_eye_offset_x + sx) * 4 + channel;
                return stereo_bgra[index];
            };

            double b = 0.0;
            double g = 0.0;
            double r = 0.0;
            for (int channel = 0; channel < 3; ++channel) {
                const double value =
                    w00 * sample_channel(x0, y0, channel) +
                    w10 * sample_channel(x0 + 1, y0, channel) +
                    w01 * sample_channel(x0, y0 + 1, channel) +
                    w11 * sample_channel(x0 + 1, y0 + 1, channel);
                if (channel == 0) b = value;
                if (channel == 1) g = value;
                if (channel == 2) r = value;
            }

            rectified_bgra[dst_pixel * 4 + 0] = clamp_u8(b);
            rectified_bgra[dst_pixel * 4 + 1] = clamp_u8(g);
            rectified_bgra[dst_pixel * 4 + 2] = clamp_u8(r);
            rectified_bgra[dst_pixel * 4 + 3] = 255;
            rectified_gray[dst_pixel] = clamp_u8(0.114 * b + 0.587 * g + 0.299 * r);
        }
    }
}

inline double camera_z_from_q(const Calibration& c, double u, double v, double disparity) {
    const auto& q = c.Q;
    const double z_h = q[8] * u + q[9] * v + q[10] * disparity + q[11];
    const double w_h = q[12] * u + q[13] * v + q[14] * disparity + q[15];
    if (std::abs(w_h) < 1e-12) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return z_h / w_h;
}

struct PointDepth {
    bool valid = false;
    double disparity_px = 0.0;
    double z_mm = 0.0;
    double average_cost = 0.0;
};

inline PointDepth point_depth(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int x,
    int y) {

    constexpr int radius = 3;
    constexpr int min_disp = 8;
    constexpr int max_disp_limit = 192;
    PointDepth result;

    if (x < radius + min_disp || x >= kEyeWidth - radius ||
        y < radius || y >= kEyeHeight - radius) {
        return result;
    }

    const int max_disp = std::min(max_disp_limit, x - radius - 1);
    if (max_disp <= min_disp + 2) {
        return result;
    }

    const int area = (radius * 2 + 1) * (radius * 2 + 1);
    std::vector<int> costs(static_cast<size_t>(max_disp + 1), std::numeric_limits<int>::max());
    int best_d = -1;
    int best_cost = std::numeric_limits<int>::max();

    for (int d = min_disp; d <= max_disp; ++d) {
        int cost = 0;
        for (int yy = -radius; yy <= radius; ++yy) {
            const size_t left_row = static_cast<size_t>(y + yy) * kEyeWidth;
            const size_t right_row = left_row;
            for (int xx = -radius; xx <= radius; ++xx) {
                const int lv = left[left_row + x + xx];
                const int rv = right[right_row + x + xx - d];
                cost += std::abs(lv - rv);
            }
        }
        costs[static_cast<size_t>(d)] = cost;
        if (cost < best_cost) {
            best_cost = cost;
            best_d = d;
        }
    }

    if (best_d < 0 || best_cost / static_cast<double>(area) > 55.0) {
        return result;
    }

    int second_cost = std::numeric_limits<int>::max();
    for (int d = min_disp; d <= max_disp; ++d) {
        if (std::abs(d - best_d) <= 1) continue;
        second_cost = std::min(second_cost, costs[static_cast<size_t>(d)]);
    }
    if (second_cost != std::numeric_limits<int>::max() &&
        static_cast<double>(second_cost) < static_cast<double>(best_cost) * 1.035) {
        return result;
    }

    double disparity = static_cast<double>(best_d);
    if (best_d > min_disp && best_d < max_disp) {
        const double c0 = static_cast<double>(costs[static_cast<size_t>(best_d - 1)]);
        const double c1 = static_cast<double>(costs[static_cast<size_t>(best_d)]);
        const double c2 = static_cast<double>(costs[static_cast<size_t>(best_d + 1)]);
        const double denom = c0 - 2.0 * c1 + c2;
        if (std::abs(denom) > 1e-9) {
            const double offset = std::clamp(0.5 * (c0 - c2) / denom, -1.0, 1.0);
            disparity += offset;
        }
    }

    const double z = camera_z_from_q(c, static_cast<double>(x), static_cast<double>(y), disparity);
    if (!std::isfinite(z) || z <= 0.0 || z > 5000.0) {
        return result;
    }

    result.valid = true;
    result.disparity_px = disparity;
    result.z_mm = z;
    result.average_cost = best_cost / static_cast<double>(area);
    return result;
}

struct DepthWorkspace {
    std::vector<uint8_t> left_small;
    std::vector<uint8_t> right_small;
    std::vector<int> best_cost;
    std::vector<int> second_cost;
    std::vector<uint8_t> best_disp;
    std::vector<int> integral;
    std::vector<uint8_t> heatmap_bgra;

    DepthWorkspace()
        : left_small(static_cast<size_t>(kDepthWidth) * kDepthHeight),
          right_small(static_cast<size_t>(kDepthWidth) * kDepthHeight),
          best_cost(static_cast<size_t>(kDepthWidth) * kDepthHeight),
          second_cost(static_cast<size_t>(kDepthWidth) * kDepthHeight),
          best_disp(static_cast<size_t>(kDepthWidth) * kDepthHeight),
          integral(static_cast<size_t>(kDepthWidth + 1) * (kDepthHeight + 1)),
          heatmap_bgra(static_cast<size_t>(kEyeWidth) * kEyeHeight * 4, 0) {}
};

inline void downsample_2x(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
    for (int y = 0; y < kDepthHeight; ++y) {
        for (int x = 0; x < kDepthWidth; ++x) {
            const int sx = x * 2;
            const int sy = y * 2;
            const size_t row0 = static_cast<size_t>(sy) * kEyeWidth;
            const size_t row1 = static_cast<size_t>(sy + 1) * kEyeWidth;
            const int sum = src[row0 + sx] + src[row0 + sx + 1] +
                            src[row1 + sx] + src[row1 + sx + 1];
            dst[static_cast<size_t>(y) * kDepthWidth + x] = static_cast<uint8_t>((sum + 2) / 4);
        }
    }
}

inline void depth_color(double z_mm, uint8_t& b, uint8_t& g, uint8_t& r) {
    const double t = std::clamp((z_mm - 250.0) / 1000.0, 0.0, 1.0);
    if (t < 0.25) {
        const double a = t / 0.25;
        r = 255;
        g = clamp_u8(255.0 * a);
        b = 0;
    } else if (t < 0.5) {
        const double a = (t - 0.25) / 0.25;
        r = clamp_u8(255.0 * (1.0 - a));
        g = 255;
        b = 0;
    } else if (t < 0.75) {
        const double a = (t - 0.5) / 0.25;
        r = 0;
        g = 255;
        b = clamp_u8(255.0 * a);
    } else {
        const double a = (t - 0.75) / 0.25;
        r = 0;
        g = clamp_u8(255.0 * (1.0 - a));
        b = 255;
    }
}

inline void compute_depth_heatmap(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    DepthWorkspace& w) {

    downsample_2x(left, w.left_small);
    downsample_2x(right, w.right_small);

    constexpr int radius = 2;
    constexpr int min_disp = 4;
    constexpr int max_disp = 96;
    constexpr int max_average_cost = 55;
    constexpr double uniqueness = 1.04;
    constexpr int inf = std::numeric_limits<int>::max() / 4;

    std::fill(w.best_cost.begin(), w.best_cost.end(), inf);
    std::fill(w.second_cost.begin(), w.second_cost.end(), inf);
    std::fill(w.best_disp.begin(), w.best_disp.end(), 0);

    const int stride = kDepthWidth + 1;
    for (int d = min_disp; d <= max_disp; ++d) {
        std::fill(w.integral.begin(), w.integral.begin() + stride, 0);
        for (int y = 0; y < kDepthHeight; ++y) {
            int row_sum = 0;
            w.integral[static_cast<size_t>(y + 1) * stride] = 0;
            for (int x = 0; x < kDepthWidth; ++x) {
                const int diff = x >= d
                    ? std::abs(static_cast<int>(w.left_small[static_cast<size_t>(y) * kDepthWidth + x]) -
                               static_cast<int>(w.right_small[static_cast<size_t>(y) * kDepthWidth + x - d]))
                    : 255;
                row_sum += diff;
                w.integral[static_cast<size_t>(y + 1) * stride + x + 1] =
                    w.integral[static_cast<size_t>(y) * stride + x + 1] + row_sum;
            }
        }

        for (int y = radius; y < kDepthHeight - radius; ++y) {
            const int y0 = y - radius;
            const int y1 = y + radius;
            for (int x = d + radius; x < kDepthWidth - radius; ++x) {
                const int x0 = x - radius;
                const int x1 = x + radius;
                const int sum =
                    w.integral[static_cast<size_t>(y1 + 1) * stride + x1 + 1] -
                    w.integral[static_cast<size_t>(y0) * stride + x1 + 1] -
                    w.integral[static_cast<size_t>(y1 + 1) * stride + x0] +
                    w.integral[static_cast<size_t>(y0) * stride + x0];
                const size_t index = static_cast<size_t>(y) * kDepthWidth + x;
                if (sum < w.best_cost[index]) {
                    w.second_cost[index] = w.best_cost[index];
                    w.best_cost[index] = sum;
                    w.best_disp[index] = static_cast<uint8_t>(d);
                } else if (sum < w.second_cost[index]) {
                    w.second_cost[index] = sum;
                }
            }
        }
    }

    std::fill(w.heatmap_bgra.begin(), w.heatmap_bgra.end(), 0);
    const int area = (radius * 2 + 1) * (radius * 2 + 1);
    for (int y = 0; y < kDepthHeight; ++y) {
        for (int x = 0; x < kDepthWidth; ++x) {
            const size_t index = static_cast<size_t>(y) * kDepthWidth + x;
            const int d_small = w.best_disp[index];
            bool valid = d_small > 0 &&
                w.best_cost[index] <= max_average_cost * area &&
                (w.second_cost[index] == inf ||
                 static_cast<double>(w.second_cost[index]) >=
                    static_cast<double>(w.best_cost[index]) * uniqueness);

            double z = std::numeric_limits<double>::quiet_NaN();
            if (valid) {
                const double full_disp = static_cast<double>(d_small * kDepthScale);
                z = camera_z_from_q(c, x * kDepthScale + 0.5, y * kDepthScale + 0.5, full_disp);
                valid = std::isfinite(z) && z >= 150.0 && z <= 2500.0;
            }

            uint8_t b = 18;
            uint8_t g = 18;
            uint8_t r = 18;
            if (valid) {
                depth_color(z, b, g, r);
            }

            for (int yy = 0; yy < kDepthScale; ++yy) {
                for (int xx = 0; xx < kDepthScale; ++xx) {
                    const int fx = x * kDepthScale + xx;
                    const int fy = y * kDepthScale + yy;
                    const size_t dst = (static_cast<size_t>(fy) * kEyeWidth + fx) * 4;
                    w.heatmap_bgra[dst + 0] = b;
                    w.heatmap_bgra[dst + 1] = g;
                    w.heatmap_bgra[dst + 2] = r;
                    w.heatmap_bgra[dst + 3] = 255;
                }
            }
        }
    }
}

inline void compose_stereo_or_depth(
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right_or_depth,
    std::vector<uint8_t>& stereo_output) {

    stereo_output.resize(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
    for (int y = 0; y < kEyeHeight; ++y) {
        const size_t eye_row = static_cast<size_t>(y) * kEyeWidth * 4;
        const size_t stereo_row = static_cast<size_t>(y) * kStereoWidth * 4;
        std::copy_n(left.data() + eye_row, static_cast<size_t>(kEyeWidth) * 4,
                    stereo_output.data() + stereo_row);
        std::copy_n(right_or_depth.data() + eye_row, static_cast<size_t>(kEyeWidth) * 4,
                    stereo_output.data() + stereo_row + static_cast<size_t>(kEyeWidth) * 4);
    }
}

inline double inferred_baseline_mm(const Calibration& c) {
    const double q32 = c.Q[14];
    return std::abs(q32) > 1e-12 ? std::abs(1.0 / q32) : 0.0;
}

} // namespace touchplus::depth
