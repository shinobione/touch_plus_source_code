#pragma once

// Phase 2C.1I diagnostic-only dataset writer.
//
// Operator labels are ground truth for offline analysis only. They are never
// consumed by the 2C.1H shadow proxy or the authoritative Phase 2C contact
// state machine. Numeric keys deliberately avoid the existing Phase 2A
// H/C surface shortcuts:
//   1 = HIGH, 2 = NEAR, 3 = CONTACT, 0 = NONE.

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace touchplus::diagnostic {

enum class ShadowDatasetLabelV2C1I { None, High, Near, Contact };

inline const char* shadow_dataset_label_name_v2c1i(ShadowDatasetLabelV2C1I label) {
    switch (label) {
        case ShadowDatasetLabelV2C1I::High: return "HIGH";
        case ShadowDatasetLabelV2C1I::Near: return "NEAR";
        case ShadowDatasetLabelV2C1I::Contact: return "CONTACT";
        case ShadowDatasetLabelV2C1I::None: return "NONE";
    }
    return "NONE";
}

constexpr std::array<int, 4> kShadowDatasetKeysV2C1I{'1', '2', '3', '0'};
constexpr std::array<ShadowDatasetLabelV2C1I, 4> kShadowDatasetLabelsV2C1I{
    ShadowDatasetLabelV2C1I::High,
    ShadowDatasetLabelV2C1I::Near,
    ShadowDatasetLabelV2C1I::Contact,
    ShadowDatasetLabelV2C1I::None};

inline std::string timestamp_utc_v2c1i(
    std::chrono::system_clock::time_point timestamp) {

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch());
    const std::time_t seconds = std::chrono::system_clock::to_time_t(timestamp);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream value;
    value << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
          << '.' << std::setfill('0') << std::setw(3)
          << (milliseconds.count() % 1000) << 'Z';
    return value.str();
}

struct ShadowDatasetRowV2C1I {
    std::chrono::system_clock::time_point timestamp{};
    std::uint64_t frame = 0;
    ShadowDatasetLabelV2C1I physical_label = ShadowDatasetLabelV2C1I::None;
    bool sample_valid = false;
    const char* target_source = "NONE";
    int target_x = -1;
    int target_y = -1;
    int raw_dense_count = 0;
    double nearest_px = std::numeric_limits<double>::quiet_NaN();
    double nearest_h_mm = std::numeric_limits<double>::quiet_NaN();
    double nearest_disparity_px = std::numeric_limits<double>::quiet_NaN();
    double h_min_mm = std::numeric_limits<double>::quiet_NaN();
    double h_p25_mm = std::numeric_limits<double>::quiet_NaN();
    double h_median_mm = std::numeric_limits<double>::quiet_NaN();
    double spread_mm = std::numeric_limits<double>::quiet_NaN();
    double approach_drop_mm = std::numeric_limits<double>::quiet_NaN();
    double plateau_h_span_mm = std::numeric_limits<double>::quiet_NaN();
    double plateau_motion_px = std::numeric_limits<double>::quiet_NaN();
    bool trusted_target = false;
    bool dense_enough = false;
    bool low_band = false;
    bool approach_seen = false;
    bool plateau = false;
    int candidate_count = 0;
    int release_count = 0;
    bool would_contact = false;
    const char* event = "NONE";
    const char* reason = "NO_SAMPLE";
};

class ShadowDatasetCsvV2C1I {
public:
    static constexpr std::uint64_t kFlushFrames = 30;

    ShadowDatasetCsvV2C1I() = default;
    ShadowDatasetCsvV2C1I(const ShadowDatasetCsvV2C1I&) = delete;
    ShadowDatasetCsvV2C1I& operator=(const ShadowDatasetCsvV2C1I&) = delete;
    ~ShadowDatasetCsvV2C1I() { close(); }

    bool open(const std::filesystem::path& path) {
        close();
        stream_.open(path, std::ios::out | std::ios::trunc);
        if (!stream_) return false;
        rows_since_flush_ = 0;
        stream_
            << "timestamp_utc,frame,physical_label,sample_valid,target_source,"
               "target_x,target_y,raw_dense_count,nearest_px,nearest_H_mm,"
               "nearest_disparity_px,H_min_mm,H_p25_mm,H_median_mm,spread_mm,"
               "approach_drop_mm,plateau_H_span_mm,plateau_motion_px,"
               "trusted_target,dense_enough,low_band,approach_seen,plateau,"
               "candidate_count,release_count,would_contact,event,reason,"
               "label_used_for_decision,shadow_only,authoritative,OS_INJECTION\n";
        stream_.flush();
        return static_cast<bool>(stream_);
    }

    bool write(const ShadowDatasetRowV2C1I& row) {
        if (!stream_) return false;
        stream_ << timestamp_utc_v2c1i(row.timestamp) << ','
                << row.frame << ','
                << shadow_dataset_label_name_v2c1i(row.physical_label) << ','
                << (row.sample_valid ? 1 : 0) << ','
                << safe_text(row.target_source) << ','
                << row.target_x << ',' << row.target_y << ','
                << row.raw_dense_count << ',';
        write_number(row.nearest_px); stream_ << ',';
        write_number(row.nearest_h_mm); stream_ << ',';
        write_number(row.nearest_disparity_px); stream_ << ',';
        write_number(row.h_min_mm); stream_ << ',';
        write_number(row.h_p25_mm); stream_ << ',';
        write_number(row.h_median_mm); stream_ << ',';
        write_number(row.spread_mm); stream_ << ',';
        write_number(row.approach_drop_mm); stream_ << ',';
        write_number(row.plateau_h_span_mm); stream_ << ',';
        write_number(row.plateau_motion_px); stream_ << ','
                << (row.trusted_target ? 1 : 0) << ','
                << (row.dense_enough ? 1 : 0) << ','
                << (row.low_band ? 1 : 0) << ','
                << (row.approach_seen ? 1 : 0) << ','
                << (row.plateau ? 1 : 0) << ','
                << row.candidate_count << ','
                << row.release_count << ','
                << (row.would_contact ? 1 : 0) << ','
                << safe_text(row.event) << ','
                << safe_text(row.reason) << ','
                << "0,1,UNCHANGED,DISABLED\n";
        ++rows_since_flush_;
        if (rows_since_flush_ >= kFlushFrames) flush();
        return static_cast<bool>(stream_);
    }

    void flush() {
        if (stream_) stream_.flush();
        rows_since_flush_ = 0;
    }

    void close() {
        if (!stream_.is_open()) return;
        stream_.flush();
        stream_.close();
        rows_since_flush_ = 0;
    }

    bool is_open() const { return stream_.is_open(); }

private:
    static const char* safe_text(const char* value) {
        return value != nullptr ? value : "";
    }

    void write_number(double value) {
        if (std::isfinite(value)) {
            stream_ << std::fixed << std::setprecision(3) << value;
        } else {
            stream_ << "nan";
        }
    }

    std::ofstream stream_;
    std::uint64_t rows_since_flush_ = 0;
};

} // namespace touchplus::diagnostic
