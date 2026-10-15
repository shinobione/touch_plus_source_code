#include "phase2c1i_shadow_dataset_csv.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    return condition;
}

} // namespace

int main() {
    using namespace touchplus::diagnostic;

    bool ok = true;
    ok &= expect(kShadowDatasetKeysV2C1I[0] == '1' &&
                 kShadowDatasetKeysV2C1I[1] == '2' &&
                 kShadowDatasetKeysV2C1I[2] == '3' &&
                 kShadowDatasetKeysV2C1I[3] == '0',
                 "numeric label keys are exactly 1/2/3/0");
    ok &= expect(std::string(shadow_dataset_label_name_v2c1i(
                     kShadowDatasetLabelsV2C1I[0])) == "HIGH" &&
                 std::string(shadow_dataset_label_name_v2c1i(
                     kShadowDatasetLabelsV2C1I[1])) == "NEAR" &&
                 std::string(shadow_dataset_label_name_v2c1i(
                     kShadowDatasetLabelsV2C1I[2])) == "CONTACT" &&
                 std::string(shadow_dataset_label_name_v2c1i(
                     kShadowDatasetLabelsV2C1I[3])) == "NONE",
                 "numeric keys map to HIGH/NEAR/CONTACT/NONE");

    const auto path = std::filesystem::current_path() /
        "touchplus-phase2c1i-selftest.csv";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    {
        ShadowDatasetCsvV2C1I csv;
        ok &= expect(csv.open(path), "dataset CSV opens");

        ShadowDatasetRowV2C1I invalid;
        invalid.timestamp = std::chrono::system_clock::now();
        invalid.frame = 10;
        invalid.physical_label = ShadowDatasetLabelV2C1I::High;
        invalid.sample_valid = false;
        invalid.target_source = "NONE";
        invalid.reason = "NO_SAMPLE";
        ok &= expect(csv.write(invalid), "invalid per-frame row writes instead of disappearing");

        ShadowDatasetRowV2C1I contact;
        contact.timestamp = std::chrono::system_clock::now();
        contact.frame = 11;
        contact.physical_label = ShadowDatasetLabelV2C1I::Contact;
        contact.sample_valid = true;
        contact.target_source = "ANATOMY";
        contact.target_x = 321;
        contact.target_y = 287;
        contact.raw_dense_count = 17;
        contact.nearest_px = 2.0;
        contact.nearest_h_mm = 18.0;
        contact.nearest_disparity_px = 166.0;
        contact.h_min_mm = 14.0;
        contact.h_p25_mm = 18.5;
        contact.h_median_mm = 22.0;
        contact.spread_mm = 3.5;
        contact.approach_drop_mm = 41.0;
        contact.plateau_h_span_mm = 2.0;
        contact.plateau_motion_px = 6.0;
        contact.trusted_target = true;
        contact.dense_enough = true;
        contact.low_band = true;
        contact.approach_seen = true;
        contact.plateau = true;
        contact.candidate_count = 3;
        contact.release_count = 0;
        contact.would_contact = true;
        contact.event = "WOULD_DOWN";
        contact.reason = "WOULD_CONTACT";
        ok &= expect(csv.write(contact), "valid CONTACT shadow row writes");
        csv.flush();
        csv.close();
    }

    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();

    ok &= expect(text.find(
        "timestamp_utc,frame,physical_label,sample_valid,target_source") !=
        std::string::npos,
        "schema begins with timestamp/frame/ground-truth/sample fields");
    ok &= expect(text.find(
        "approach_drop_mm,plateau_H_span_mm,plateau_motion_px") !=
        std::string::npos,
        "schema includes dynamic approach/plateau features");
    ok &= expect(text.find(
        "label_used_for_decision,shadow_only,authoritative,OS_INJECTION") !=
        std::string::npos,
        "schema carries explicit safety ownership fields");
    ok &= expect(text.find(",10,HIGH,0,NONE,") != std::string::npos,
                 "invalid HIGH frame is preserved");
    ok &= expect(text.find(",11,CONTACT,1,ANATOMY,") != std::string::npos,
                 "CONTACT ground truth and target source are preserved");
    ok &= expect(text.find(",WOULD_DOWN,WOULD_CONTACT,0,1,UNCHANGED,DISABLED") !=
                 std::string::npos,
                 "label is not used for decisions and authority/OS stay unchanged");

    std::filesystem::remove(path, ignored);

    if (!ok) {
        std::cerr << "Phase 2C.1I shadow dataset CSV self-test FAILED\n";
        return 1;
    }

    std::cout << "Phase 2C.1I shadow dataset CSV self-test PASS\n";
    return 0;
}
