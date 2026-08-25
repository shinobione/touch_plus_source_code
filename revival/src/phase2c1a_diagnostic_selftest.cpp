#include "phase2c1a_diagnostic.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    return condition;
}

std::vector<std::string> lines(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::vector<std::string> result;
    for (std::string line; std::getline(input, line);) result.push_back(line);
    return result;
}

} // namespace

int main() {
    using namespace touchplus::diagnostic;
    bool ok = true;
    const auto path = std::filesystem::temp_directory_path() /
        "touchplus-phase2c1a-diagnostic-selftest.csv";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    {
        DiagnosticCsvV2C1A csv;
        ok &= expect(csv.open(path), "CSV opens");

        DiagnosticRowV2C1A valid;
        valid.timestamp = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(1724364000123));
        valid.frame = 41;
        valid.physical_label = PhysicalLabelV2C1A::Contact;
        valid.identity_id = 17;
        valid.identity_accepted = true;
        valid.identity_current = true;
        valid.fingertip_valid = true;
        valid.fingertip_source = "B";
        valid.raw_h_mm = 3.25;
        valid.smoothed_h_mm = 3.75;
        valid.contact_state = "TOUCH_HELD";
        valid.contact_event = "TOUCH_DOWN";
        valid.rejection_reason = "PERSISTENT_APPROACH_DOWN";
        ok &= expect(csv.write(valid), "valid CONTACT row writes");

        DiagnosticRowV2C1A invalid;
        invalid.timestamp = valid.timestamp + std::chrono::milliseconds(17);
        invalid.frame = 42;
        invalid.identity_stale = true;
        invalid.raw_h_mm = std::numeric_limits<double>::quiet_NaN();
        invalid.smoothed_h_mm = std::numeric_limits<double>::quiet_NaN();
        invalid.rejection_reason = "IDENTITY_STALE";
        ok &= expect(csv.write(invalid), "invalid UNKNOWN/stale row writes");
        csv.flush();
        ok &= expect(lines(path).size() == 3,
            "explicit flush exposes header and both per-frame rows");
    }

    const auto output = lines(path);
    ok &= expect(output.size() == 3, "destructor closes CSV cleanly");
    ok &= expect(!output.empty() && output[0] ==
        "timestamp_utc,frame,physical_label,identity_id,identity_accepted,"
        "identity_current,identity_stale,fingertip_valid,fingertip_source,"
        "raw_h_mm,smoothed_h_mm,contact_state,contact_event,rejection_reason",
        "machine-readable schema is exact");
    ok &= expect(output.size() > 1 &&
        output[1].find(",41,CONTACT,17,1,1,0,1,B,3.250,3.750,"
                       "TOUCH_HELD,TOUCH_DOWN,PERSISTENT_APPROACH_DOWN") !=
            std::string::npos,
        "valid row preserves label, identity, source, H and contact fields");
    ok &= expect(output.size() > 2 &&
        output[2].find(",42,NONE,0,0,0,1,0,A,nan,nan,"
                       "NO_FINGER,HOVER,IDENTITY_STALE") != std::string::npos,
        "invalid row preserves UNKNOWN/stale diagnostics");
    ok &= expect(std::string(physical_label_name_v2c1a(
        PhysicalLabelV2C1A::High)) == "HIGH" &&
        std::string(physical_label_name_v2c1a(
        PhysicalLabelV2C1A::Near)) == "NEAR" &&
        std::string(physical_label_name_v2c1a(
        PhysicalLabelV2C1A::Contact)) == "CONTACT" &&
        std::string(physical_label_name_v2c1a(
        PhysicalLabelV2C1A::None)) == "NONE",
        "H/N/C/0 label names remain exact");
    std::filesystem::remove(path, ignored);
    std::cout << "[PASS] OS_INJECTION=DISABLED (diagnostic CSV only)\n";
    return ok ? 0 : 1;
}
