#include <windows.h>
#include <windowsx.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "EtronDI_O.h"
#include "eSPAEAWBCtrl.h"
#include "depth_math.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;
using touchplus::depth::Calibration;
using touchplus::depth::DepthWorkspace;
using touchplus::depth::PointDepth;
using touchplus::depth::RectifyMap;

namespace {

constexpr wchar_t kTouchPlusVidPid[] = L"vid_1e4e&pid_0107";
constexpr wchar_t kTouchPlusFriendlyName[] = L"touch+ camera";
constexpr UINT32 kStereoWidth = touchplus::depth::kStereoWidth;
constexpr UINT32 kStereoHeight = touchplus::depth::kStereoHeight;
constexpr UINT32 kEyeWidth = touchplus::depth::kEyeWidth;
constexpr UINT32 kEyeHeight = touchplus::depth::kEyeHeight;
constexpr wchar_t kWindowClass[] = L"TouchPlusRevivalDepthViewer";
constexpr UINT kFrameReadyMessage = WM_APP + 1;
constexpr UINT kCaptureErrorMessage = WM_APP + 2;

std::atomic<bool> g_running{true};
std::atomic<bool> g_frame_message_pending{false};
std::atomic<bool> g_depth_mode{true};
std::atomic<int> g_cursor_x{kEyeWidth / 2};
std::atomic<int> g_cursor_y{kEyeHeight / 2};
std::mutex g_frame_mutex;
std::vector<BYTE> g_latest_frame(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
std::vector<BYTE> g_display_frame(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
std::wstring g_latest_status = L"Waiting for Touch+ frames...";
std::wstring g_display_status = L"Waiting for Touch+ frames...";
bool g_have_frame = false;
std::string g_capture_error;
std::string g_device_serial;

HDC g_back_dc = nullptr;
HBITMAP g_back_bitmap = nullptr;
HGDIOBJ g_back_old_bitmap = nullptr;
int g_back_width = 0;
int g_back_height = 0;

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

std::wstring widen_ascii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring get_string(IMFActivate* activate, REFGUID key) {
    wchar_t* raw = nullptr;
    UINT32 length = 0;
    const HRESULT hr = activate->GetAllocatedString(key, &raw, &length);
    if (FAILED(hr) || raw == nullptr) {
        return L"";
    }
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
        static_cast<char>((value >> 24) & 0xff),
        '\0'
    };
    for (int i = 0; i < 4; ++i) {
        const unsigned char c = static_cast<unsigned char>(chars[i]);
        if (c < 32 || c > 126) {
            return "GUID";
        }
    }
    return std::string(chars);
}

void release_backbuffer() {
    if (g_back_dc != nullptr) {
        if (g_back_old_bitmap != nullptr) {
            SelectObject(g_back_dc, g_back_old_bitmap);
        }
        if (g_back_bitmap != nullptr) {
            DeleteObject(g_back_bitmap);
        }
        DeleteDC(g_back_dc);
    }
    g_back_dc = nullptr;
    g_back_bitmap = nullptr;
    g_back_old_bitmap = nullptr;
    g_back_width = 0;
    g_back_height = 0;
}

bool ensure_backbuffer(HDC reference_dc, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (g_back_dc != nullptr && g_back_bitmap != nullptr &&
        g_back_width == width && g_back_height == height) {
        return true;
    }

    release_backbuffer();
    g_back_dc = CreateCompatibleDC(reference_dc);
    if (g_back_dc == nullptr) {
        return false;
    }

    g_back_bitmap = CreateCompatibleBitmap(reference_dc, width, height);
    if (g_back_bitmap == nullptr) {
        DeleteDC(g_back_dc);
        g_back_dc = nullptr;
        return false;
    }

    g_back_old_bitmap = SelectObject(g_back_dc, g_back_bitmap);
    if (g_back_old_bitmap == nullptr || g_back_old_bitmap == HGDI_ERROR) {
        DeleteObject(g_back_bitmap);
        DeleteDC(g_back_dc);
        g_back_bitmap = nullptr;
        g_back_dc = nullptr;
        g_back_old_bitmap = nullptr;
        return false;
    }

    g_back_width = width;
    g_back_height = height;
    return true;
}

struct ComRuntime {
    bool owned = false;
    ComRuntime() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            owned = true;
            return;
        }
        check_hr(hr, "CoInitializeEx(capture thread)");
    }
    ~ComRuntime() {
        if (owned) {
            CoUninitialize();
        }
    }
};

struct MediaFoundationRuntime {
    MediaFoundationRuntime() { check_hr(MFStartup(MF_VERSION), "MFStartup"); }
    ~MediaFoundationRuntime() { MFShutdown(); }
};

struct Mode {
    ComPtr<IMFMediaType> type;
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 fps_num = 0;
    UINT32 fps_den = 1;
    GUID subtype{};
};

std::vector<Mode> enumerate_modes(IMFSourceReader* reader) {
    std::vector<Mode> modes;
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> type;
        const HRESULT hr = reader->GetNativeMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type);
        if (hr == MF_E_NO_MORE_TYPES) {
            break;
        }
        check_hr(hr, "GetNativeMediaType");

        GUID major{};
        if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || major != MFMediaType_Video) {
            continue;
        }

        Mode mode;
        mode.type = type;
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &mode.width, &mode.height);
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &mode.fps_num, &mode.fps_den);
        type->GetGUID(MF_MT_SUBTYPE, &mode.subtype);
        modes.push_back(mode);
    }
    return modes;
}

const Mode* choose_stereo_mode(const std::vector<Mode>& modes) {
    const Mode* best = nullptr;
    for (const auto& mode : modes) {
        if (mode.width != kStereoWidth || mode.height != kStereoHeight) {
            continue;
        }
        if (best == nullptr) {
            best = &mode;
            continue;
        }

        const bool mode_mjpg = mode.subtype == MFVideoFormat_MJPG;
        const bool best_mjpg = best->subtype == MFVideoFormat_MJPG;
        const double fps = mode.fps_den
            ? static_cast<double>(mode.fps_num) / static_cast<double>(mode.fps_den)
            : 0.0;
        const double best_fps = best->fps_den
            ? static_cast<double>(best->fps_num) / static_cast<double>(best->fps_den)
            : 0.0;

        if ((mode_mjpg && !best_mjpg) ||
            (mode_mjpg == best_mjpg && fps > best_fps)) {
            best = &mode;
        }
    }
    return best;
}

std::filesystem::path executable_dir() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("GetModuleFileNameW failed");
    }
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::string serial_from_flash_bytes(const BYTE* bytes, size_t count) {
    std::string serial;
    serial.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (bytes[i] > 9) {
            std::ostringstream oss;
            oss << "Unexpected Touch+ flash serial byte " << static_cast<int>(bytes[i])
                << " at index " << i;
            throw std::runtime_error(oss.str());
        }
        serial.push_back(static_cast<char>('0' + bytes[i]));
    }
    return serial;
}

std::string unlock_touchplus(bool legacy_init) {
    if (sizeof(void*) != 4) {
        throw std::runtime_error("Depth viewer must run as Win32 because the Etron SDK is 32-bit.");
    }

    std::cout << "[1/4] Vendor unlock + serial\n";
    void* handle = nullptr;
    if (!EtronDI_Init(&handle) || handle == nullptr) {
        throw std::runtime_error("EtronDI_Init failed");
    }

    std::string serial;
    try {
        int count = 0;
        check_etron(eSPAEAWB_EnumDevice(&count), "eSPAEAWB_EnumDevice");

        int touch_index = -1;
        for (int i = 0; i < count; ++i) {
            WCHAR name[255] = {};
            const int ret = eSPAEAWB_GetDevicename(i, name, 255);
            std::wcout << L"  [" << i << L"] " << name << L" (ret=" << ret << L")\n";
            if (touch_index < 0 && lower(name).find(kTouchPlusFriendlyName) != std::wstring::npos) {
                touch_index = i;
            }
        }

        if (touch_index < 0) {
            throw std::runtime_error("Touch+ Camera not found by the Etron control layer");
        }

        check_etron(eSPAEAWB_SelectDevice(touch_index), "eSPAEAWB_SelectDevice");
        check_etron(eSPAEAWB_SetSensorType(ESPAEAWB_SENSOR_TYPE_OV7740),
                    "eSPAEAWB_SetSensorType(OV7740)");
        check_etron(eSPAEAWB_SWUnlock(0x0107), "eSPAEAWB_SWUnlock(0x0107)");

        BYTE flash[10] = {};
        const int flash_ret = eSPAEAWB_ReadFlash(flash, 10);
        std::cout << std::left << std::setw(34) << "eSPAEAWB_ReadFlash(10)";
        if (flash_ret < 0) {
            std::cout << "RET=" << flash_ret << "\n";
            throw std::runtime_error("Unable to recover Touch+ serial from flash");
        }
        std::cout << "OK (ret=" << flash_ret << ")\n";
        serial = serial_from_flash_bytes(flash, 10);
        std::cout << "Recovered serial: " << serial << "\n";
        if (serial.rfind("0101", 0) != 0) {
            throw std::runtime_error("Recovered flash serial does not have historical 0101 prefix");
        }

        if (legacy_init) {
            std::cout << "Applying recovered Ractiv camera initializer...\n";
            check_etron(eSPAEAWB_DisableAE(), "eSPAEAWB_DisableAE");
            check_etron(eSPAEAWB_DisableAWB(), "eSPAEAWB_DisableAWB");
            BYTE gpio = 0;
            check_etron(eSPAEAWB_GetGPIOValue(1, &gpio), "eSPAEAWB_GetGPIOValue(1)");
            gpio = static_cast<BYTE>(gpio | 0x08);
            check_etron(eSPAEAWB_SetGPIOValue(1, gpio), "LEDs ON / GPIO");
            check_etron(eSPAEAWB_SetExposureTime(ESPAEAWB_SENSOR_MODE_BOTH, 15.0f),
                        "Exposure both = 15 ms");
            check_etron(eSPAEAWB_SetGlobalGain(ESPAEAWB_SENSOR_MODE_LEFT, 1.0f),
                        "Global gain left = 1");
            check_etron(eSPAEAWB_SetGlobalGain(ESPAEAWB_SENSOR_MODE_RIGHT, 1.0f),
                        "Global gain right = 1");
            check_etron(eSPAEAWB_SetColorGain(ESPAEAWB_SENSOR_MODE_LEFT, 2.0f, 1.0f, 2.0f),
                        "Color gain left = 2/1/2");
            check_etron(eSPAEAWB_SetColorGain(ESPAEAWB_SENSOR_MODE_RIGHT, 2.0f, 1.0f, 2.0f),
                        "Color gain right = 2/1/2");
        }
    } catch (...) {
        EtronDI_Release(&handle);
        throw;
    }

    EtronDI_Release(&handle);
    std::cout << "Vendor unlock + serial: PASS\n\n";
    return serial;
}

inline BYTE clamp_byte(int value) {
    return static_cast<BYTE>(std::clamp(value, 0, 255));
}

inline void write_bgra(std::vector<BYTE>& out, UINT32 x, UINT32 y,
                       int y_value, int u_value, int v_value) {
    const int c = std::max(0, y_value - 16);
    const int d = u_value - 128;
    const int e = v_value - 128;
    const int r = (298 * c + 409 * e + 128) >> 8;
    const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    const int b = (298 * c + 516 * d + 128) >> 8;

    const size_t index = (static_cast<size_t>(y) * kStereoWidth + x) * 4;
    out[index + 0] = clamp_byte(b);
    out[index + 1] = clamp_byte(g);
    out[index + 2] = clamp_byte(r);
    out[index + 3] = 255;
}

void convert_yuy2_to_bgra_vertical_flip(const BYTE* input, DWORD length,
                                         std::vector<BYTE>& output) {
    const size_t required = static_cast<size_t>(kStereoWidth) * kStereoHeight * 2;
    if (length < required) {
        throw std::runtime_error("YUY2 frame is smaller than 1280x480x2 bytes");
    }

    for (UINT32 y = 0; y < kStereoHeight; ++y) {
        const BYTE* row = input + static_cast<size_t>(y) * kStereoWidth * 2;
        const UINT32 out_y = kStereoHeight - 1 - y;

        for (UINT32 x = 0; x < kStereoWidth; x += 2) {
            const size_t offset = static_cast<size_t>(x) * 2;
            const int y0 = row[offset + 0];
            const int u = row[offset + 1];
            const int y1 = row[offset + 2];
            const int v = row[offset + 3];
            write_bgra(output, x, out_y, y0, u, v);
            write_bgra(output, x + 1, out_y, y1, u, v);
        }
    }
}

void publish_frame(HWND hwnd, std::vector<BYTE>& local_frame,
                   const std::wstring& status) {
    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        g_latest_frame.swap(local_frame);
        g_latest_status = status;
    }

    if (!g_frame_message_pending.exchange(true)) {
        PostMessageW(hwnd, kFrameReadyMessage, 0, 0);
    }
}

void capture_thread(HWND hwnd, Calibration calibration, RectifyMap map_left, RectifyMap map_right) {
    try {
        ComRuntime com;
        MediaFoundationRuntime mf;

        std::cout << "[3/4] Opening persistent MJPEG -> YUY2 stereo stream\n";

        ComPtr<IMFAttributes> enum_attributes;
        check_hr(MFCreateAttributes(&enum_attributes, 1), "MFCreateAttributes");
        check_hr(enum_attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
                 "Set video capture source type");

        IMFActivate** devices = nullptr;
        UINT32 device_count = 0;
        check_hr(MFEnumDeviceSources(enum_attributes.Get(), &devices, &device_count),
                 "MFEnumDeviceSources");

        ComPtr<IMFActivate> selected;
        for (UINT32 i = 0; i < device_count; ++i) {
            const std::wstring name = get_string(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            const std::wstring link = get_string(
                devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            const bool match = lower(link).find(kTouchPlusVidPid) != std::wstring::npos ||
                               lower(name).find(kTouchPlusFriendlyName) != std::wstring::npos;
            if (match && !selected) {
                selected = devices[i];
                std::wcout << L"  [MATCH] " << name << L"\n";
            }
        }
        for (UINT32 i = 0; i < device_count; ++i) {
            devices[i]->Release();
        }
        CoTaskMemFree(devices);

        if (!selected) {
            throw std::runtime_error("Touch+ Camera not found by Media Foundation");
        }

        ComPtr<IMFMediaSource> source;
        check_hr(selected->ActivateObject(IID_PPV_ARGS(&source)),
                 "ActivateObject(IMFMediaSource)");

        ComPtr<IMFSourceReader> reader;
        check_hr(MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader),
                 "MFCreateSourceReaderFromMediaSource");

        const auto modes = enumerate_modes(reader.Get());
        const Mode* chosen = choose_stereo_mode(modes);
        if (!chosen) {
            source->Shutdown();
            throw std::runtime_error("No native 1280x480 stereo mode exposed");
        }

        const double target_fps = chosen->fps_den
            ? static_cast<double>(chosen->fps_num) / static_cast<double>(chosen->fps_den)
            : 0.0;
        std::cout << "Native stereo mode: " << chosen->width << "x" << chosen->height
                  << " @ " << std::fixed << std::setprecision(2) << target_fps
                  << " fps [" << fourcc_from_guid(chosen->subtype) << "]\n";

        check_hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr,
                                              chosen->type.Get()),
                 "Select native MJPEG mode");

        ComPtr<IMFMediaType> yuy2_type;
        check_hr(MFCreateMediaType(&yuy2_type), "MFCreateMediaType(YUY2)");
        check_hr(yuy2_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video),
                 "Set YUY2 major type");
        check_hr(yuy2_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YUY2),
                 "Set YUY2 subtype");
        check_hr(MFSetAttributeSize(yuy2_type.Get(), MF_MT_FRAME_SIZE,
                                    kStereoWidth, kStereoHeight),
                 "Set YUY2 frame size");
        check_hr(MFSetAttributeRatio(yuy2_type.Get(), MF_MT_FRAME_RATE,
                                     chosen->fps_num, chosen->fps_den),
                 "Set YUY2 frame rate");
        check_hr(MFSetAttributeRatio(yuy2_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
                 "Set YUY2 pixel aspect ratio");
        check_hr(yuy2_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
                 "Set YUY2 progressive mode");
        check_hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr,
                                              yuy2_type.Get()),
                 "Request decoded YUY2 output");

        ComPtr<IMFMediaType> actual_type;
        check_hr(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual_type),
                 "GetCurrentMediaType");
        GUID actual_subtype{};
        UINT32 actual_width = 0;
        UINT32 actual_height = 0;
        actual_type->GetGUID(MF_MT_SUBTYPE, &actual_subtype);
        MFGetAttributeSize(actual_type.Get(), MF_MT_FRAME_SIZE, &actual_width, &actual_height);
        if (actual_subtype != MFVideoFormat_YUY2 ||
            actual_width != kStereoWidth || actual_height != kStereoHeight) {
            source->Shutdown();
            throw std::runtime_error("Source Reader did not accept 1280x480 YUY2 output");
        }

        std::cout << "[4/4] Live rectify + depth processing running\n";

        std::vector<BYTE> stereo_bgra(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
        std::vector<BYTE> rect_left_bgra;
        std::vector<BYTE> rect_right_bgra;
        std::vector<BYTE> rect_left_gray;
        std::vector<BYTE> rect_right_gray;
        std::vector<BYTE> composed(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
        DepthWorkspace depth_workspace;

        std::uint64_t frames_total = 0;
        std::uint64_t frames_window = 0;
        std::uint64_t depth_updates_window = 0;
        auto fps_epoch = std::chrono::steady_clock::now();
        LONGLONG previous_timestamp = -1;
        double source_hz = 0.0;
        double capture_fps = 0.0;
        double depth_hz = 0.0;
        double last_depth_ms = 0.0;

        while (g_running.load()) {
            DWORD actual_stream = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            check_hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                        0,
                                        &actual_stream,
                                        &flags,
                                        &timestamp,
                                        &sample),
                     "ReadSample");

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                throw std::runtime_error("Touch+ stream ended unexpectedly");
            }
            if (!sample) {
                continue;
            }

            ComPtr<IMFMediaBuffer> buffer;
            check_hr(sample->ConvertToContiguousBuffer(&buffer), "ConvertToContiguousBuffer");
            BYTE* data = nullptr;
            DWORD max_length = 0;
            DWORD current_length = 0;
            check_hr(buffer->Lock(&data, &max_length, &current_length), "IMFMediaBuffer::Lock");
            try {
                convert_yuy2_to_bgra_vertical_flip(data, current_length, stereo_bgra);
            } catch (...) {
                buffer->Unlock();
                throw;
            }
            buffer->Unlock();

            touchplus::depth::rectify_eye_bgra(
                stereo_bgra, 0, map_left, rect_left_bgra, rect_left_gray);
            touchplus::depth::rectify_eye_bgra(
                stereo_bgra, kEyeWidth, map_right, rect_right_bgra, rect_right_gray);

            ++frames_total;
            ++frames_window;

            if (previous_timestamp >= 0 && timestamp > previous_timestamp) {
                const double instant_hz = 10000000.0 /
                    static_cast<double>(timestamp - previous_timestamp);
                if (instant_hz > 1.0 && instant_hz < 240.0) {
                    source_hz = source_hz == 0.0
                        ? instant_hz
                        : (source_hz * 0.90 + instant_hz * 0.10);
                }
            }
            previous_timestamp = timestamp;

            const bool depth_mode = g_depth_mode.load();
            if (depth_mode && (frames_total == 1 || frames_total % 2 == 0)) {
                const auto depth_start = std::chrono::steady_clock::now();
                touchplus::depth::compute_depth_heatmap(
                    calibration, rect_left_gray, rect_right_gray, depth_workspace);
                last_depth_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - depth_start).count();
                ++depth_updates_window;
            }

            const int cursor_x = std::clamp(g_cursor_x.load(), 0, static_cast<int>(kEyeWidth) - 1);
            const int cursor_y = std::clamp(g_cursor_y.load(), 0, static_cast<int>(kEyeHeight) - 1);
            const PointDepth cursor_depth = touchplus::depth::point_depth(
                calibration, rect_left_gray, rect_right_gray, cursor_x, cursor_y);

            if (depth_mode) {
                touchplus::depth::compose_stereo_or_depth(
                    rect_left_bgra, depth_workspace.heatmap_bgra, composed);
            } else {
                touchplus::depth::compose_stereo_or_depth(
                    rect_left_bgra, rect_right_bgra, composed);
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fps_epoch).count();
            if (elapsed >= 1.0) {
                capture_fps = static_cast<double>(frames_window) / elapsed;
                depth_hz = static_cast<double>(depth_updates_window) / elapsed;
                frames_window = 0;
                depth_updates_window = 0;
                fps_epoch = now;
            }

            std::wostringstream status;
            status << L"capture " << std::fixed << std::setprecision(1) << capture_fps
                   << L" fps | source " << source_hz << L" Hz";
            if (depth_mode) {
                status << L" | depth " << depth_hz << L" Hz / " << last_depth_ms << L" ms";
            }
            status << L" | cursor " << cursor_x << L"," << cursor_y;
            if (cursor_depth.valid) {
                status << L" d=" << std::setprecision(2) << cursor_depth.disparity_px
                       << L" px | camera Z=" << std::setprecision(1) << cursor_depth.z_mm << L" mm";
            } else {
                status << L" | camera Z=invalid (move onto texture)";
            }
            publish_frame(hwnd, composed, status.str());
        }

        source->Shutdown();
        std::cout << "Depth capture thread stopped after " << frames_total << " frames.\n";
    } catch (const std::exception& error) {
        g_capture_error = error.what();
        std::cerr << "\nPHASE 1C LIVE DEPTH RESULT: FAIL\n" << error.what() << "\n";
        PostMessageW(hwnd, kCaptureErrorMessage, 0, 0);
    }
}

void update_cursor_from_mouse(HWND hwnd, LPARAM lparam) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int client_width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int client_height = std::max(1, static_cast<int>(rc.bottom - rc.top));
    constexpr int status_height = 58;
    const int view_height = std::max(1, client_height - status_height);

    const int mx = std::clamp(GET_X_LPARAM(lparam), 0, client_width - 1);
    const int my = std::clamp(GET_Y_LPARAM(lparam), 0, view_height - 1);
    int image_x = static_cast<int>(static_cast<long long>(mx) * kStereoWidth / client_width);
    const int image_y = static_cast<int>(static_cast<long long>(my) * kEyeHeight / view_height);
    if (image_x >= static_cast<int>(kEyeWidth)) {
        image_x -= kEyeWidth;
    }
    g_cursor_x.store(std::clamp(image_x, 0, static_cast<int>(kEyeWidth) - 1));
    g_cursor_y.store(std::clamp(image_y, 0, static_cast<int>(kEyeHeight) - 1));
}

void draw_crosshair(HDC dc, int x, int y) {
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x - 8, y, nullptr);
    LineTo(dc, x + 9, y);
    MoveToEx(dc, x, y - 8, nullptr);
    LineTo(dc, x, y + 9);
    SelectObject(dc, old);
    DeleteObject(pen);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case kFrameReadyMessage: {
        {
            std::lock_guard<std::mutex> lock(g_frame_mutex);
            g_display_frame.swap(g_latest_frame);
            g_display_status = g_latest_status;
            g_have_frame = true;
        }
        g_frame_message_pending.store(false);
        const std::wstring title = L"TouchPlus Revival - Live Depth - " +
            widen_ascii(g_device_serial) + L" - " + g_display_status;
        SetWindowTextW(hwnd, title.c_str());
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case kCaptureErrorMessage:
        MessageBoxA(hwnd, g_capture_error.c_str(), "TouchPlus Live Depth", MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        update_cursor_from_mouse(hwnd, lparam);
        return 0;
    case WM_CLOSE:
        g_running.store(false);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        release_backbuffer();
        g_running.store(false);
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE || wparam == 'Q') {
            g_running.store(false);
            DestroyWindow(hwnd);
            return 0;
        }
        if (wparam == 'D') {
            g_depth_mode.store(true);
            return 0;
        }
        if (wparam == 'S') {
            g_depth_mode.store(false);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        const int client_width = static_cast<int>(rc.right - rc.left);
        const int client_height = static_cast<int>(rc.bottom - rc.top);
        constexpr int status_height = 58;
        const int view_height = std::max(1, client_height - status_height);
        const int half_width = std::max(1, client_width / 2);

        if (!ensure_backbuffer(dc, client_width, client_height)) {
            FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd, &ps);
            return 0;
        }

        RECT back_rect{0, 0, client_width, client_height};
        FillRect(g_back_dc, &back_rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        if (g_have_frame) {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = static_cast<LONG>(kStereoWidth);
            bmi.bmiHeader.biHeight = -static_cast<LONG>(kStereoHeight);
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(g_back_dc, COLORONCOLOR);
            StretchDIBits(g_back_dc,
                          0, 0, client_width, view_height,
                          0, 0, kStereoWidth, kStereoHeight,
                          g_display_frame.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        }

        HPEN divider = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ old_pen = SelectObject(g_back_dc, divider);
        MoveToEx(g_back_dc, half_width, 0, nullptr);
        LineTo(g_back_dc, half_width, view_height);
        SelectObject(g_back_dc, old_pen);
        DeleteObject(divider);

        SetBkMode(g_back_dc, TRANSPARENT);
        SetTextColor(g_back_dc, RGB(255, 255, 255));
        const wchar_t* left_label = L"RECTIFIED LEFT";
        TextOutW(g_back_dc, 12, 10, left_label, static_cast<int>(wcslen(left_label)));
        const wchar_t* right_label = g_depth_mode.load()
            ? L"DEPTH HEATMAP - CAMERA Z"
            : L"RECTIFIED RIGHT";
        TextOutW(g_back_dc, half_width + 12, 10,
                 right_label, static_cast<int>(wcslen(right_label)));

        const int cursor_x = g_cursor_x.load();
        const int cursor_y = g_cursor_y.load();
        const int cy = static_cast<int>(static_cast<long long>(cursor_y) * view_height / kEyeHeight);
        const int cx_left = static_cast<int>(static_cast<long long>(cursor_x) * client_width / kStereoWidth);
        const int cx_right = static_cast<int>(
            static_cast<long long>(cursor_x + kEyeWidth) * client_width / kStereoWidth);
        draw_crosshair(g_back_dc, cx_left, cy);
        draw_crosshair(g_back_dc, cx_right, cy);

        SetTextColor(g_back_dc, RGB(180, 220, 255));
        TextOutW(g_back_dc, 12, view_height + 6,
                 g_display_status.c_str(), static_cast<int>(g_display_status.size()));
        SetTextColor(g_back_dc, RGB(190, 190, 190));
        const wchar_t* help =
            L"Mouse: sample Z | D: depth | S: rectified stereo | ESC/Q: quit | Z is camera-coordinate mm";
        TextOutW(g_back_dc, 12, view_height + 30,
                 help, static_cast<int>(wcslen(help)));

        BitBlt(dc, 0, 0, client_width, client_height, g_back_dc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND create_viewer_window(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClassExW failed");
    }

    RECT desired{0, 0, 1280, 538};
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(
        0,
        kWindowClass,
        L"TouchPlus Revival - Live Depth",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        throw std::runtime_error("CreateWindowExW failed");
    }
    return hwnd;
}

int run_self_test(const std::filesystem::path& calibration_path) {
    std::cout << "TouchPlus Revival - Phase 1C live depth self-test\n";
    std::cout << "Calibration: " << calibration_path.string() << "\n";
    const Calibration c = touchplus::depth::load_calibration(calibration_path);
    const RectifyMap left = touchplus::depth::build_rectify_map(c.K1, c.D1, c.R1, c.P1);
    const RectifyMap right = touchplus::depth::build_rectify_map(c.K2, c.D2, c.R2, c.P2);

    const double left_coverage = static_cast<double>(left.valid_points) /
        static_cast<double>(static_cast<size_t>(kEyeWidth) * kEyeHeight);
    const double right_coverage = static_cast<double>(right.valid_points) /
        static_cast<double>(static_cast<size_t>(kEyeWidth) * kEyeHeight);
    const double baseline = touchplus::depth::inferred_baseline_mm(c);
    const double z_near = touchplus::depth::camera_z_from_q(c, c.P1[2], c.P1[6], 82.0);
    const double z_far = touchplus::depth::camera_z_from_q(c, c.P1[2], c.P1[6], 49.5);

    std::cout << std::fixed << std::setprecision(3)
              << "Serial: " << c.serial << "\n"
              << "State: " << c.promotion_state << "\n"
              << "Inferred baseline: " << baseline << " mm\n"
              << "Rectify coverage LEFT: " << left_coverage * 100.0 << "%\n"
              << "Rectify coverage RIGHT: " << right_coverage * 100.0 << "%\n"
              << "Synthetic Q near Z (d=82): " << z_near << " mm\n"
              << "Synthetic Q far Z  (d=49.5): " << z_far << " mm\n";

    if (c.serial.empty() || baseline < 55.0 || baseline > 65.0 ||
        left_coverage < 0.70 || right_coverage < 0.70 ||
        !std::isfinite(z_near) || !std::isfinite(z_far) ||
        z_near <= 0.0 || z_far <= z_near) {
        std::cerr << "PHASE 1C LIVE DEPTH SELF-TEST: FAIL\n";
        return 1;
    }

    std::cout << "PHASE 1C LIVE DEPTH SELF-TEST: PASS\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    bool legacy_init = false;
    bool self_test = false;
    bool enable_hybrid_promotion = false;
    std::filesystem::path self_test_calibration;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--legacy-init") {
            legacy_init = true;
        } else if (arg == L"--enable-hybrid-promotion") {
            enable_hybrid_promotion = true;
        } else if (arg == L"--self-test") {
            self_test = true;
            if (i + 1 < argc) {
                self_test_calibration = argv[++i];
            }
        }
    }

    touchplus::depth::set_hybrid_promotion_enabled_v10d(
        enable_hybrid_promotion);

    try {
        std::cout
            << "[HYBRID] promotion_mode="
            << (touchplus::depth::hybrid_promotion_enabled_v10d()
                    ? "ENABLED" : "DISABLED")
            << " | opt_in_flag=--enable-hybrid-promotion"
            << " | OS_INJECTION=DISABLED\n";
        if (self_test) {
            if (self_test_calibration.empty()) {
                throw std::runtime_error("--self-test requires a calibration JSON path");
            }
            return run_self_test(self_test_calibration);
        }

        std::cout << "TouchPlus Revival - Phase 1C live metric depth viewer\n";
        std::cout << "Process architecture: " << (sizeof(void*) * 8) << "-bit\n";
        std::cout << "Pipeline: Etron unlock -> persistent stereo -> rectification -> disparity -> Q -> camera Z mm\n";
        std::cout << "Dense heatmap: dependency-free half-resolution CPU matcher; cursor Z: full-resolution subpixel patch match\n";
        std::cout << "Offline StereoSGBM remains the metric reference diagnostic.\n\n";

        g_device_serial = unlock_touchplus(legacy_init);

        const std::filesystem::path calibration_path =
            executable_dir() / L"calibration" / (widen_ascii(g_device_serial) + L".json");
        std::cout << "[2/4] Loading physically validated calibration\n";
        std::cout << "Calibration path: " << calibration_path.string() << "\n";
        const Calibration calibration = touchplus::depth::load_calibration(calibration_path);
        if (calibration.serial != g_device_serial) {
            throw std::runtime_error(
                "Calibration serial mismatch: hardware=" + g_device_serial +
                " calibration=" + calibration.serial);
        }

        const RectifyMap map_left = touchplus::depth::build_rectify_map(
            calibration.K1, calibration.D1, calibration.R1, calibration.P1);
        const RectifyMap map_right = touchplus::depth::build_rectify_map(
            calibration.K2, calibration.D2, calibration.R2, calibration.P2);
        std::cout << std::fixed << std::setprecision(3)
                  << "Calibration state: " << calibration.promotion_state << "\n"
                  << "Q baseline: " << touchplus::depth::inferred_baseline_mm(calibration) << " mm\n"
                  << "Rectification maps: LEFT " << map_left.valid_points
                  << " / RIGHT " << map_right.valid_points << " valid source samples\n"
                  << "Calibration load: PASS\n\n";

        HWND hwnd = create_viewer_window(GetModuleHandleW(nullptr));
        std::thread worker(capture_thread, hwnd, calibration, map_left, map_right);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        g_running.store(false);
        if (worker.joinable()) {
            worker.join();
        }
        std::cout << "Live depth viewer closed cleanly.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nPHASE 1C LIVE DEPTH RESULT: FAIL\n" << error.what() << "\n";
        MessageBoxA(nullptr, error.what(), "TouchPlus Live Depth", MB_OK | MB_ICONERROR);
        return 1;
    }
}
