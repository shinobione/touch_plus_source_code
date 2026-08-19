#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
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

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kTouchPlusVidPid[] = L"vid_1e4e&pid_0107";
constexpr wchar_t kTouchPlusFriendlyName[] = L"touch+ camera";
constexpr UINT32 kStereoWidth = 1280;
constexpr UINT32 kStereoHeight = 480;
constexpr UINT32 kEyeWidth = 640;
constexpr UINT32 kEyeHeight = 480;
constexpr wchar_t kWindowClass[] = L"TouchPlusRevivalCalibrationCapture";
constexpr UINT kFrameReadyMessage = WM_APP + 1;
constexpr UINT kCaptureErrorMessage = WM_APP + 2;

std::atomic<bool> g_running{true};
std::atomic<bool> g_frame_message_pending{false};
std::atomic<bool> g_snapshot_requested{false};
std::atomic<unsigned> g_saved_pairs{0};
std::mutex g_frame_mutex;
std::vector<BYTE> g_latest_frame(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
std::vector<BYTE> g_display_frame(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
std::wstring g_latest_status = L"Waiting for Touch+ frames...";
std::wstring g_display_status = L"Waiting for Touch+ frames...";
bool g_have_frame = false;
std::string g_capture_error;
std::filesystem::path g_output_dir;
unsigned g_target_pairs = 0;

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

void unlock_touchplus(bool legacy_init) {
    if (sizeof(void*) != 4) {
        throw std::runtime_error("Calibration capture must run as Win32 because the Etron SDK is 32-bit.");
    }

    std::cout << "[1/3] Vendor unlock\n";
    void* handle = nullptr;
    if (!EtronDI_Init(&handle) || handle == nullptr) {
        throw std::runtime_error("EtronDI_Init failed");
    }

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
    std::cout << "Vendor unlock: PASS\n\n";
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

void save_png(const std::filesystem::path& path,
              UINT32 width,
              UINT32 height,
              const BYTE* bgra,
              UINT32 stride) {
    ComPtr<IWICImagingFactory> factory;
    check_hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory)),
             "CoCreateInstance(WICImagingFactory)");

    ComPtr<IWICStream> stream;
    check_hr(factory->CreateStream(&stream), "IWICImagingFactory::CreateStream");
    check_hr(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE),
             "IWICStream::InitializeFromFilename");

    ComPtr<IWICBitmapEncoder> encoder;
    check_hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder),
             "IWICImagingFactory::CreateEncoder");
    check_hr(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
             "IWICBitmapEncoder::Initialize");

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    check_hr(encoder->CreateNewFrame(&frame, &options),
             "IWICBitmapEncoder::CreateNewFrame");
    check_hr(frame->Initialize(options.Get()), "IWICBitmapFrameEncode::Initialize");
    check_hr(frame->SetSize(width, height), "IWICBitmapFrameEncode::SetSize");

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    check_hr(frame->SetPixelFormat(&pixel_format), "IWICBitmapFrameEncode::SetPixelFormat");
    if (pixel_format != GUID_WICPixelFormat32bppBGRA) {
        throw std::runtime_error("WIC refused 32bpp BGRA output");
    }

    check_hr(frame->WritePixels(height, stride, stride * height,
                                const_cast<BYTE*>(bgra)),
             "IWICBitmapFrameEncode::WritePixels");
    check_hr(frame->Commit(), "IWICBitmapFrameEncode::Commit");
    check_hr(encoder->Commit(), "IWICBitmapEncoder::Commit");
}

std::vector<BYTE> copy_eye(const std::vector<BYTE>& frame, UINT32 x_offset) {
    const UINT32 full_stride = kStereoWidth * 4;
    const UINT32 eye_stride = kEyeWidth * 4;
    std::vector<BYTE> output(static_cast<size_t>(eye_stride) * kEyeHeight);
    for (UINT32 y = 0; y < kEyeHeight; ++y) {
        const BYTE* src = frame.data() + static_cast<size_t>(y) * full_stride
                        + static_cast<size_t>(x_offset) * 4;
        BYTE* dst = output.data() + static_cast<size_t>(y) * eye_stride;
        std::copy_n(src, eye_stride, dst);
    }
    return output;
}

bool frame_content_ok(const std::vector<BYTE>& frame) {
    int min_luma = 255;
    int max_luma = 0;
    for (UINT32 y = 0; y < kStereoHeight; y += 8) {
        for (UINT32 x = 0; x < kStereoWidth; x += 8) {
            const size_t i = (static_cast<size_t>(y) * kStereoWidth + x) * 4;
            const int luma = (static_cast<int>(frame[i + 0]) +
                              static_cast<int>(frame[i + 1]) +
                              static_cast<int>(frame[i + 2])) / 3;
            min_luma = std::min(min_luma, luma);
            max_luma = std::max(max_luma, luma);
        }
    }
    return (max_luma - min_luma) >= 10;
}

std::wstring pair_stem(unsigned index) {
    std::wostringstream oss;
    oss << L"pair-" << std::setw(3) << std::setfill(L'0') << index;
    return oss.str();
}

void save_pair(const std::vector<BYTE>& frame, unsigned index) {
    std::filesystem::create_directories(g_output_dir);
    const std::wstring stem = pair_stem(index);
    const auto left = copy_eye(frame, 0);
    const auto right = copy_eye(frame, kEyeWidth);

    const UINT32 full_stride = kStereoWidth * 4;
    const UINT32 eye_stride = kEyeWidth * 4;
    save_png(g_output_dir / (stem + L"-full.png"),
             kStereoWidth, kStereoHeight, frame.data(), full_stride);
    save_png(g_output_dir / (stem + L"-left.png"),
             kEyeWidth, kEyeHeight, left.data(), eye_stride);
    save_png(g_output_dir / (stem + L"-right.png"),
             kEyeWidth, kEyeHeight, right.data(), eye_stride);

    std::ofstream meta(g_output_dir / (stem + L".json"), std::ios::binary);
    meta << "{\n"
         << "  \"schema\": \"touchplus-revival-calibration-pair-v2\",\n"
         << "  \"pair\": " << index << ",\n"
         << "  \"serial\": \"0101007379\",\n"
         << "  \"stereo\": \"1280x480 split 640x480 + 640x480\",\n"
         << "  \"orientation\": \"historical Ractiv vertical flip already applied\",\n"
         << "  \"square_mm\": 25.0,\n"
         << "  \"inner_corners\": [9, 6]\n"
         << "}\n";
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

void capture_thread(HWND hwnd) {
    try {
        ComRuntime com;
        MediaFoundationRuntime mf;

        std::cout << "[2/3] Opening persistent native MJPEG -> YUY2 stream\n";

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

        std::cout << "[3/3] Live calibration preview running\n";
        std::wcout << L"Output: " << g_output_dir.wstring() << L"\n";

        std::vector<BYTE> local_frame(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
        std::uint64_t frames_total = 0;
        std::uint64_t frames_window = 0;
        auto fps_epoch = std::chrono::steady_clock::now();
        double capture_fps = 0.0;
        std::wstring event = L"SPACE = capture pair";

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
                convert_yuy2_to_bgra_vertical_flip(data, current_length, local_frame);
            } catch (...) {
                buffer->Unlock();
                throw;
            }
            buffer->Unlock();

            ++frames_total;
            ++frames_window;

            if (g_snapshot_requested.exchange(false)) {
                if (!frame_content_ok(local_frame)) {
                    event = L"CAPTURE REJECTED: frame looks gray/uniform";
                    MessageBeep(MB_ICONWARNING);
                    std::cout << "Capture rejected: frame looks gray/uniform. Stream remains open.\n";
                } else if (g_target_pairs > 0 && g_saved_pairs.load() >= g_target_pairs) {
                    event = L"Target already reached. Q/ESC to finish.";
                    MessageBeep(MB_ICONINFORMATION);
                } else {
                    const unsigned index = g_saved_pairs.load() + 1;
                    save_pair(local_frame, index);
                    g_saved_pairs.store(index);
                    std::wostringstream msg;
                    msg << L"SAVED pair-" << std::setw(3) << std::setfill(L'0') << index;
                    if (g_target_pairs > 0) {
                        msg << L" / " << g_target_pairs;
                    }
                    event = msg.str();
                    MessageBeep(MB_OK);
                    std::wcout << L"Saved " << pair_stem(index) << L" to "
                               << g_output_dir.wstring() << L"\n";
                }
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fps_epoch).count();
            if (elapsed >= 1.0) {
                capture_fps = static_cast<double>(frames_window) / elapsed;
                frames_window = 0;
                fps_epoch = now;
            }

            std::wostringstream status;
            status << L"Touch+ live " << std::fixed << std::setprecision(1) << capture_fps
                   << L" fps | saved " << g_saved_pairs.load();
            if (g_target_pairs > 0) {
                status << L"/" << g_target_pairs;
            }
            status << L" | " << event;
            publish_frame(hwnd, local_frame, status.str());
        }

        source->Shutdown();
        std::cout << "Capture thread stopped after " << frames_total << " frames.\n";
    } catch (const std::exception& error) {
        g_capture_error = error.what();
        std::cerr << "\nPHASE 1B.2A RESULT: FAIL\n" << error.what() << "\n";
        PostMessageW(hwnd, kCaptureErrorMessage, 0, 0);
    }
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
        SetWindowTextW(hwnd, (L"TouchPlus Calibration Capture - " + g_display_status).c_str());
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case kCaptureErrorMessage:
        MessageBoxA(hwnd, g_capture_error.c_str(), "TouchPlus Calibration Capture", MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
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
        if (wparam == VK_SPACE) {
            if (!g_snapshot_requested.exchange(true)) {
                MessageBeep(MB_ICONINFORMATION);
            }
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
        const int status_height = 58;
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
        TextOutW(g_back_dc, 12, 10, L"LEFT", 4);
        TextOutW(g_back_dc, half_width + 12, 10, L"RIGHT", 5);

        SetTextColor(g_back_dc, RGB(180, 220, 255));
        TextOutW(g_back_dc, 12, view_height + 6,
                 g_display_status.c_str(), static_cast<int>(g_display_status.size()));
        SetTextColor(g_back_dc, RGB(210, 210, 210));
        const wchar_t* help = L"SPACE: save synchronized pair    Q / ESC: quit";
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

HWND create_window(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
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
        L"TouchPlus Revival - Calibration Capture",
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

unsigned parse_unsigned(const wchar_t* text, const char* option) {
    try {
        const unsigned long value = std::stoul(text);
        if (value > 999) {
            throw std::runtime_error("value too large");
        }
        return static_cast<unsigned>(value);
    } catch (...) {
        std::ostringstream oss;
        oss << option << " requires an integer from 0 to 999";
        throw std::runtime_error(oss.str());
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    bool legacy_init = false;
    g_output_dir = std::filesystem::current_path() /
                   L"calibration-captures" / L"0101007379" / L"raw";

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--legacy-init") {
            legacy_init = true;
        } else if (arg == L"--pairs" && i + 1 < argc) {
            g_target_pairs = parse_unsigned(argv[++i], "--pairs");
        } else if (arg == L"--output" && i + 1 < argc) {
            g_output_dir = argv[++i];
        } else if (arg == L"--help" || arg == L"-h") {
            std::cout << "TouchPlus calibration capture\n"
                      << "  --pairs N       optional target count; 3 is valid for smoke testing\n"
                      << "  --output PATH   output raw pair directory\n"
                      << "  --legacy-init   apply recovered Ractiv AE/AWB/exposure/gain init\n"
                      << "Keys: SPACE save pair, Q/ESC quit\n";
            return 0;
        } else {
            std::wcerr << L"Unknown/incomplete option: " << arg << L"\n";
            return 2;
        }
    }

    try {
        std::cout << "TouchPlus Revival - Phase 1B.2a LIVE calibration capture\n";
        std::cout << "Persistent stream: unlock once -> preview continuously -> SPACE saves pair\n";
        if (g_target_pairs > 0) {
            std::cout << "Target pairs: " << g_target_pairs << "\n";
        }
        std::wcout << L"Output dir: " << g_output_dir.wstring() << L"\n\n";

        unlock_touchplus(legacy_init);
        std::filesystem::create_directories(g_output_dir);

        HWND hwnd = create_window(GetModuleHandleW(nullptr));
        std::thread worker(capture_thread, hwnd);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        g_running.store(false);
        if (worker.joinable()) {
            worker.join();
        }

        std::cout << "\nPHASE 1B.2A RESULT: " << g_saved_pairs.load() << " pair(s) saved\n";
        std::wcout << L"Dataset: " << g_output_dir.wstring() << L"\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nPHASE 1B.2A RESULT: FAIL\n" << error.what() << "\n";
        MessageBoxA(nullptr, error.what(), "TouchPlus Calibration Capture", MB_OK | MB_ICONERROR);
        return 1;
    }
}
