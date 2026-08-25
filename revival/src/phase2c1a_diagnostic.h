#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace touchplus::diagnostic {

enum class PhysicalLabelV2C1A { None, High, Near, Contact };

inline const char* physical_label_name_v2c1a(PhysicalLabelV2C1A label) {
    switch (label) {
        case PhysicalLabelV2C1A::High: return "HIGH";
        case PhysicalLabelV2C1A::Near: return "NEAR";
        case PhysicalLabelV2C1A::Contact: return "CONTACT";
        case PhysicalLabelV2C1A::None: return "NONE";
    }
    return "NONE";
}

struct DiagnosticRowV2C1A {
    std::chrono::system_clock::time_point timestamp{};
    std::uint64_t frame = 0;
    PhysicalLabelV2C1A physical_label = PhysicalLabelV2C1A::None;
    std::uint64_t identity_id = 0;
    bool identity_accepted = false;
    bool identity_current = false;
    bool identity_stale = false;
    bool fingertip_valid = false;
    const char* fingertip_source = "A";
    double raw_h_mm = 0.0;
    double smoothed_h_mm = 0.0;
    const char* contact_state = "NO_FINGER";
    const char* contact_event = "HOVER";
    const char* rejection_reason = "NO_CURRENT_FINGER";
};

inline std::string timestamp_utc_v2c1a(
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

class DiagnosticCsvV2C1A {
public:
    static constexpr std::uint64_t kFlushFrames = 30;

    DiagnosticCsvV2C1A() = default;
    DiagnosticCsvV2C1A(const DiagnosticCsvV2C1A&) = delete;
    DiagnosticCsvV2C1A& operator=(const DiagnosticCsvV2C1A&) = delete;
    ~DiagnosticCsvV2C1A() { close(); }

    bool open(const std::filesystem::path& path) {
        close();
        stream_.open(path, std::ios::out | std::ios::trunc);
        if (!stream_) return false;
        rows_since_flush_ = 0;
        stream_ << "timestamp_utc,frame,physical_label,identity_id,"
                   "identity_accepted,identity_current,identity_stale,"
                   "fingertip_valid,fingertip_source,raw_h_mm,smoothed_h_mm,"
                   "contact_state,contact_event,rejection_reason\n";
        stream_.flush();
        return static_cast<bool>(stream_);
    }

    bool write(const DiagnosticRowV2C1A& row) {
        if (!stream_) return false;
        stream_ << timestamp_utc_v2c1a(row.timestamp) << ','
                << row.frame << ','
                << physical_label_name_v2c1a(row.physical_label) << ','
                << row.identity_id << ','
                << (row.identity_accepted ? 1 : 0) << ','
                << (row.identity_current ? 1 : 0) << ','
                << (row.identity_stale ? 1 : 0) << ','
                << (row.fingertip_valid ? 1 : 0) << ','
                << row.fingertip_source << ',';
        write_number(row.raw_h_mm);
        stream_ << ',';
        write_number(row.smoothed_h_mm);
        stream_ << ',' << row.contact_state
                << ',' << row.contact_event
                << ',' << row.rejection_reason << '\n';
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
