#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "EtronDI_O.h"
#include "eSPAEAWBCtrl.h"
#include "com_compat.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kTouchPlusVidPid[] = L"vid_1e4e&pid_0107";
constexpr wchar_t kTouchPlusFriendlyName[] = L"touch+ camera";
constexpr UINT32 kStereoWidth = 1280;
constexpr UINT32 kStereoHeight = 480;

void check_hr(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(hr) << ")";
        throw std::runtime_error(oss.str());
    }
}

void check_etron(int code, const char* operation) {
    std::cout << std::left << std::setw(34) << operation;
    if (code == ESPAEAWB_RET_OK) {
        std::cout << "OK\n";
        return;
    }
    std::cout << "RET=" << code << "\n";
    std::ostringstream oss;
    oss << operation << " failed with Etron return code " << code;
    throw std::runtime_error(oss.str());
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

std::wstring get_string(IMFActivate* activate, REFGUID key) {
    wchar_t* raw = nullptr;
    UINT32 length = 0;
    const HRESULT hr = activate->GetAllocatedString(key, &raw, &length);
    if (FAILED(hr) || raw == nullptr) return L"";
    std::wstring result(raw, length);
    CoTaskMemFree(raw);
    return result;
}

std::string fourcc_from_guid(const GUID& guid) {
    const DWORD value = guid.Data1;
    char chars[5] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff), '\0'
    };
    for (int i = 0; i < 4; ++i) {
        const unsigned char c = static_cast<unsigned char>(chars[i]);
        if (c < 32 || c > 126) return "GUID";
    }
    return std::string(chars);
}

struct ComRuntime {
    ComRuntime() { check_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx"); }
    ~ComRuntime() { CoUninitialize(); }
};
struct MediaFoundationRuntime {
    MediaFoundationRuntime() { check_hr(MFStartup(MF_VERSION), "MFStartup"); }
    ~MediaFoundationRuntime() { MFShutdown(); }
};

struct Mode {
    ComPtr<IMFMediaType> type;
    UINT32 width = 0, height = 0, fps_num = 0, fps_den = 1;
    GUID subtype{};
};

std::vector<Mode> enumerate_modes(IMFSourceReader* reader) {
    std::vector<Mode> modes;
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> type;
        const HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type);
        if (hr == MF_E_NO_MORE_TYPES) break;
        check_hr(hr, "GetNativeMediaType");
        GUID major{};
        if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || major != MFMediaType_Video) continue;
        Mode mode;
        mode.type = type;
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &mode.width, &mode.height);
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &mode.fps_num, &mode.fps_den);
        type->GetGUID(MF_MT_SUBTYPE, &mode.subtype);
        modes.push_back(mode);
    }
    return modes;
}

const Mode* choose_mode(const std::vector<Mode>& modes) {
    const Mode* best = nullptr;
    for (const auto& mode : modes) {
        if (mode.width != kStereoWidth || mode.height != kStereoHeight || mode.subtype != MFVideoFormat_MJPG) continue;
        const double fps = mode.fps_den ? static_cast<double>(mode.fps_num) / mode.fps_den : 0.0;
        const double best_fps = best && best->fps_den ? static_cast<double>(best->fps_num) / best->fps_den : -1.0;
        if (!best || fps > best_fps) best = &mode;
    }
    return best;
}

void unlock_touchplus() {
    std::cout << "[1/3] Etron vendor unlock\n";
    void* handle = nullptr;
    if (!EtronDI_Init(&handle) || handle == nullptr) throw std::runtime_error("EtronDI_Init failed");
    try {
        int count = 0;
        check_etron(eSPAEAWB_EnumDevice(&count), "eSPAEAWB_EnumDevice");
        int touch_index = -1;
        for (int i = 0; i < count; ++i) {
            WCHAR name[255] = {};
            const int ret = eSPAEAWB_GetDevicename(i, name, 255);
            std::wcout << L"  [" << i << L"] " << name << L" (ret=" << ret << L")\n";
            if (touch_index < 0 && lower(name).find(kTouchPlusFriendlyName) != std::wstring::npos) touch_index = i;
        }
        if (touch_index < 0) throw std::runtime_error("Touch+ Camera not found by Etron");
        check_etron(eSPAEAWB_SelectDevice(touch_index), "eSPAEAWB_SelectDevice");
        check_etron(eSPAEAWB_SetSensorType(ESPAEAWB_SENSOR_TYPE_OV7740), "eSPAEAWB_SetSensorType(OV7740)");
        check_etron(eSPAEAWB_SWUnlock(0x0107), "eSPAEAWB_SWUnlock(0x0107)");
    } catch (...) {
        EtronDI_Release(&handle);
        throw;
    }
    EtronDI_Release(&handle);
    std::cout << "Vendor unlock: PASS\n\n";
}

static double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double pos = p * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    if (lo == hi) return values[lo];
    const double t = pos - static_cast<double>(lo);
    return values[lo] * (1.0 - t) + values[hi] * t;
}
}

int wmain() {
    try {
        std::cout << "TouchPlus Revival - RAW MJPEG timing probe\n";
        std::cout << "No decode, no GDI, no RGB conversion.\n\n";
        unlock_touchplus();

        std::cout << "[2/3] Opening native Media Foundation stream\n";
        ComRuntime com;
        MediaFoundationRuntime mf;

        ComPtr<IMFAttributes> attrs;
        check_hr(MFCreateAttributes(&attrs, 1), "MFCreateAttributes");
        check_hr(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
                 "Set video source type");

        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        check_hr(MFEnumDeviceSources(attrs.Get(), &devices, &count), "MFEnumDeviceSources");
        ComPtr<IMFActivate> selected;
        for (UINT32 i = 0; i < count; ++i) {
            const auto name = get_string(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            const auto link = get_string(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            if (!selected && (lower(name).find(kTouchPlusFriendlyName) != std::wstring::npos ||
                              lower(link).find(kTouchPlusVidPid) != std::wstring::npos)) {
                selected = devices[i];
                std::wcout << L"  [MATCH] " << name << L"\n";
            }
        }
        for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
        CoTaskMemFree(devices);
        if (!selected) throw std::runtime_error("Touch+ Camera not found by Media Foundation");

        ComPtr<IMFMediaSource> source;
        check_hr(selected->ActivateObject(IID_PPV_ARGS(&source)), "ActivateObject(IMFMediaSource)");
        ComPtr<IMFSourceReader> reader;
        check_hr(MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader),
                 "MFCreateSourceReaderFromMediaSource");

        const auto modes = enumerate_modes(reader.Get());
        const Mode* chosen = choose_mode(modes);
        if (!chosen) throw std::runtime_error("Native 1280x480 MJPEG mode not found");
        const double declared = chosen->fps_den ? static_cast<double>(chosen->fps_num) / chosen->fps_den : 0.0;
        std::cout << "Selected native mode: " << chosen->width << "x" << chosen->height
                  << " @ " << std::fixed << std::setprecision(2) << declared
                  << " fps [" << fourcc_from_guid(chosen->subtype) << "]\n";
        check_hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, chosen->type.Get()),
                 "Select native MJPEG mode");

        std::cout << "[3/3] Measuring 6 seconds of RAW samples...\n";
        constexpr int warmup = 15;
        constexpr double seconds_to_measure = 6.0;
        int seen = 0;
        std::uint64_t measured_frames = 0;
        std::uint64_t null_samples = 0;
        std::uint64_t discontinuities = 0;
        std::vector<double> timestamp_ms;
        LONGLONG previous_ts = -1;
        bool started = false;
        std::chrono::steady_clock::time_point start;
        std::chrono::steady_clock::time_point end;

        while (true) {
            DWORD stream = 0, flags = 0;
            LONGLONG ts = 0;
            ComPtr<IMFSample> sample;
            check_hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, &sample),
                     "ReadSample");
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) throw std::runtime_error("Unexpected end of stream");
            if (flags & MF_SOURCE_READERF_STREAMTICK) ++discontinuities;
            if (!sample) { ++null_samples; continue; }
            ++seen;
            if (seen <= warmup) { previous_ts = ts; continue; }
            if (!started) {
                started = true;
                start = std::chrono::steady_clock::now();
                previous_ts = ts;
            } else if (ts > previous_ts) {
                timestamp_ms.push_back(static_cast<double>(ts - previous_ts) / 10000.0);
                previous_ts = ts;
            }
            ++measured_frames;
            end = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(end - start).count();
            if (elapsed >= seconds_to_measure) break;
        }

        const double elapsed = std::chrono::duration<double>(end - start).count();
        const double wall_fps = elapsed > 0.0 ? static_cast<double>(measured_frames) / elapsed : 0.0;
        const double median_ms = percentile(timestamp_ms, 0.50);
        const double p10_ms = percentile(timestamp_ms, 0.10);
        const double p90_ms = percentile(timestamp_ms, 0.90);
        const double source_hz = median_ms > 0.0 ? 1000.0 / median_ms : 0.0;

        std::cout << "\n========== RAW TIMING RESULT ==========\n";
        std::cout << "Declared native mode : " << std::fixed << std::setprecision(2) << declared << " fps\n";
        std::cout << "Measured wall rate   : " << wall_fps << " samples/s\n";
        std::cout << "Timestamp median     : " << median_ms << " ms => " << source_hz << " Hz\n";
        std::cout << "Timestamp p10..p90   : " << p10_ms << " .. " << p90_ms << " ms\n";
        std::cout << "Measured samples     : " << measured_frames << " in " << elapsed << " s\n";
        std::cout << "Null samples         : " << null_samples << "\n";
        std::cout << "Stream ticks         : " << discontinuities << "\n";

        if (wall_fps >= 50.0 && source_hz >= 50.0) {
            std::cout << "VERDICT: RAW_60_CLASS - Touch+ / driver really delivers ~60 fps.\n";
        } else if (wall_fps >= 24.0 && wall_fps <= 36.0) {
            std::cout << "VERDICT: RAW_30_CLASS - native path itself is delivering ~30 fps despite @60 declaration.\n";
        } else {
            std::cout << "VERDICT: RAW_OTHER - inspect timing values before changing the viewer.\n";
        }
        std::cout << "=======================================\n";

        source->Shutdown();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTIMING PROBE FAIL: " << e.what() << "\n";
        return 1;
    }
}
