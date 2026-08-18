#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "EtronDI_O.h"
#include "eSPAEAWBCtrl.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kTouchPlusVidPid[] = L"vid_1e4e&pid_0107";
constexpr wchar_t kTouchPlusFriendlyName[] = L"touch+ camera";
constexpr UINT32 kStereoWidth = 1280;
constexpr UINT32 kStereoHeight = 480;
constexpr UINT32 kEyeWidth = 640;
constexpr UINT32 kEyeHeight = 480;

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

        const double fps = mode.fps_den
            ? static_cast<double>(mode.fps_num) / static_cast<double>(mode.fps_den)
            : 0.0;
        const double best_fps = best->fps_den
            ? static_cast<double>(best->fps_num) / static_cast<double>(best->fps_den)
            : 0.0;
        const bool mode_is_mjpg = mode.subtype == MFVideoFormat_MJPG;
        const bool best_is_mjpg = best->subtype == MFVideoFormat_MJPG;

        if ((mode_is_mjpg && !best_is_mjpg) ||
            (mode_is_mjpg == best_is_mjpg && fps > best_fps)) {
            best = &mode;
        }
    }
    return best;
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
    check_hr(frame->Initialize(options.Get()),
             "IWICBitmapFrameEncode::Initialize");
    check_hr(frame->SetSize(width, height), "IWICBitmapFrameEncode::SetSize");

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    check_hr(frame->SetPixelFormat(&pixel_format),
             "IWICBitmapFrameEncode::SetPixelFormat");
    if (pixel_format != GUID_WICPixelFormat32bppBGRA) {
        throw std::runtime_error("WIC refused 32bpp BGRA output");
    }

    check_hr(frame->WritePixels(height, stride, stride * height,
                                const_cast<BYTE*>(bgra)),
             "IWICBitmapFrameEncode::WritePixels");
    check_hr(frame->Commit(), "IWICBitmapFrameEncode::Commit");
    check_hr(encoder->Commit(), "IWICBitmapEncoder::Commit");
}

std::vector<BYTE> copy_eye(const BYTE* frame, UINT32 full_stride, UINT32 x_offset) {
    const UINT32 eye_stride = kEyeWidth * 4;
    std::vector<BYTE> output(static_cast<size_t>(eye_stride) * kEyeHeight);
    for (UINT32 y = 0; y < kEyeHeight; ++y) {
        const BYTE* src = frame + static_cast<size_t>(y) * full_stride
                        + static_cast<size_t>(x_offset) * 4;
        BYTE* dst = output.data() + static_cast<size_t>(y) * eye_stride;
        std::copy_n(src, eye_stride, dst);
    }
    return output;
}

struct FrameStats {
    int min_luma = 255;
    int max_luma = 0;
    double mean = 0.0;
    double stddev = 0.0;
};

FrameStats frame_stats(const BYTE* data, UINT32 width, UINT32 height, UINT32 stride) {
    FrameStats stats;
    long double sum = 0.0;
    long double sum_sq = 0.0;
    std::uint64_t count = 0;

    for (UINT32 y = 0; y < height; ++y) {
        const BYTE* row = data + static_cast<size_t>(y) * stride;
        for (UINT32 x = 0; x < width; ++x) {
            const BYTE* px = row + static_cast<size_t>(x) * 4;
            const int luma = (static_cast<int>(px[0]) +
                              static_cast<int>(px[1]) +
                              static_cast<int>(px[2])) / 3;
            stats.min_luma = std::min(stats.min_luma, luma);
            stats.max_luma = std::max(stats.max_luma, luma);
            sum += luma;
            sum_sq += static_cast<long double>(luma) * luma;
            ++count;
        }
    }

    if (count > 0) {
        stats.mean = static_cast<double>(sum / count);
        const long double mean_sq = (sum / count) * (sum / count);
        const long double variance = std::max<long double>(0.0, (sum_sq / count) - mean_sq);
        stats.stddev = std::sqrt(static_cast<double>(variance));
    }
    return stats;
}

void print_stats(const char* label, const FrameStats& stats) {
    std::cout << label << ": luma min=" << stats.min_luma
              << " max=" << stats.max_luma
              << " mean=" << std::fixed << std::setprecision(2) << stats.mean
              << " stddev=" << stats.stddev << "\n";
}

void unlock_touchplus(bool legacy_init) {
    if (sizeof(void*) != 4) {
        throw std::runtime_error(
            "Phase 0C must run as a 32-bit process because the recovered Etron DLLs are Win32.");
    }

    std::cout << "\n[1/3] Etron vendor control / unlock\n";

    void* handle = nullptr;
    const bool init_ok = EtronDI_Init(&handle);
    std::cout << std::left << std::setw(34) << "EtronDI_Init"
              << (init_ok ? "True" : "False") << "\n";
    if (!init_ok || handle == nullptr) {
        throw std::runtime_error("EtronDI_Init failed");
    }

    try {
        std::cout << std::left << std::setw(34) << "EtronDI_GetDeviceNumber"
                  << EtronDI_GetDeviceNumber(handle) << "\n";
        std::cout << std::left << std::setw(34) << "EtronDI_FindDevice"
                  << EtronDI_FindDevice(handle) << "\n";

        int count = 0;
        check_etron(eSPAEAWB_EnumDevice(&count), "eSPAEAWB_EnumDevice");
        std::cout << "Etron camera count: " << count << "\n";

        int touch_index = -1;
        for (int i = 0; i < count; ++i) {
            WCHAR name[255] = {};
            const int ret = eSPAEAWB_GetDevicename(i, name, 255);
            std::wcout << L"  [" << i << L"] " << name << L"  (ret=" << ret << L")\n";
            if (touch_index < 0 && lower(name).find(kTouchPlusFriendlyName) != std::wstring::npos) {
                touch_index = i;
            }
        }

        if (touch_index < 0) {
            throw std::runtime_error("Etron control layer did not enumerate Touch+ Camera");
        }

        std::cout << "Selected Touch+ index: " << touch_index << "\n";
        check_etron(eSPAEAWB_SelectDevice(touch_index), "eSPAEAWB_SelectDevice");
        check_etron(eSPAEAWB_SetSensorType(ESPAEAWB_SENSOR_TYPE_OV7740),
                    "eSPAEAWB_SetSensorType(OV7740)");
        check_etron(eSPAEAWB_SWUnlock(0x0107), "eSPAEAWB_SWUnlock(0x0107)");

        int ax = 0;
        int ay = 0;
        int az = 0;
        check_etron(eSPAEAWB_GetAccMeterValue(&ax, &ay, &az),
                    "eSPAEAWB_GetAccMeterValue");
        std::cout << "  accelerometer X/Y/Z = " << ax << " / " << ay << " / " << az << "\n";

        if (legacy_init) {
            std::cout << "Applying recovered Ractiv camera initializer...\n";
            check_etron(eSPAEAWB_DisableAE(), "eSPAEAWB_DisableAE");
            check_etron(eSPAEAWB_DisableAWB(), "eSPAEAWB_DisableAWB");

            BYTE gpio = 0;
            check_etron(eSPAEAWB_GetGPIOValue(1, &gpio), "eSPAEAWB_GetGPIOValue(1)");
            gpio = static_cast<BYTE>(gpio | 0x08);
            check_etron(eSPAEAWB_SetGPIOValue(1, gpio), "LEDs ON / eSPAEAWB_SetGPIOValue");
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

    // The physical-device smoke proved that the unlock survives release of the Etron
    // handle long enough for the video stack to open. Releasing here also avoids the
    // control SDK unnecessarily retaining the camera before Media Foundation activation.
    EtronDI_Release(&handle);
    std::cout << "Vendor unlock: PASS\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    bool legacy_init = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--legacy-init") {
            legacy_init = true;
        }
    }

    try {
        std::cout << "TouchPlus Revival - Phase 0C atomic unlock + stereo capture\n";
        std::cout << "Process architecture: " << (sizeof(void*) * 8) << "-bit\n";
        std::cout << "Policy: unlock first, then open video immediately in this process.\n";

        unlock_touchplus(legacy_init);

        std::cout << "\n[2/3] Media Foundation stereo stream\n";
        ComRuntime com;
        MediaFoundationRuntime media_foundation;

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
            const std::wstring name_lower = lower(name);
            const std::wstring link_lower = lower(link);
            const bool match = link_lower.find(kTouchPlusVidPid) != std::wstring::npos ||
                               name_lower.find(kTouchPlusFriendlyName) != std::wstring::npos;

            std::wcout << (match ? L"  [MATCH] " : L"  [     ] ")
                       << i << L": " << name << L"\n";
            if (match && !selected) {
                selected = devices[i];
            }
        }

        for (UINT32 i = 0; i < device_count; ++i) {
            devices[i]->Release();
        }
        CoTaskMemFree(devices);

        if (!selected) {
            throw std::runtime_error("Touch+ Camera disappeared before Media Foundation activation");
        }

        ComPtr<IMFMediaSource> source;
        check_hr(selected->ActivateObject(IID_PPV_ARGS(&source)),
                 "ActivateObject(IMFMediaSource)");

        ComPtr<IMFAttributes> reader_attributes;
        check_hr(MFCreateAttributes(&reader_attributes, 2),
                 "MFCreateAttributes(source reader)");
        check_hr(reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE),
                 "Enable Media Foundation video processing");

        ComPtr<IMFSourceReader> reader;
        check_hr(MFCreateSourceReaderFromMediaSource(source.Get(),
                                                      reader_attributes.Get(),
                                                      &reader),
                 "MFCreateSourceReaderFromMediaSource");

        const auto modes = enumerate_modes(reader.Get());
        std::cout << "Native video modes:\n";
        for (const auto& mode : modes) {
            const double fps = mode.fps_den
                ? static_cast<double>(mode.fps_num) / static_cast<double>(mode.fps_den)
                : 0.0;
            std::cout << "  " << mode.width << "x" << mode.height
                      << " @ " << std::fixed << std::setprecision(2) << fps
                      << " fps [" << fourcc_from_guid(mode.subtype) << "]\n";
        }

        const Mode* chosen = choose_stereo_mode(modes);
        if (!chosen) {
            source->Shutdown();
            throw std::runtime_error("No native 1280x480 stereo mode was exposed");
        }

        const double chosen_fps = chosen->fps_den
            ? static_cast<double>(chosen->fps_num) / static_cast<double>(chosen->fps_den)
            : 0.0;
        std::cout << "Selected stereo mode: " << chosen->width << "x" << chosen->height
                  << " @ " << std::fixed << std::setprecision(2) << chosen_fps
                  << " fps [" << fourcc_from_guid(chosen->subtype) << "]\n";

        check_hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr,
                                              chosen->type.Get()),
                 "Set native Touch+ media type");

        ComPtr<IMFMediaType> rgb_type;
        check_hr(MFCreateMediaType(&rgb_type), "MFCreateMediaType(RGB32)");
        check_hr(rgb_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video),
                 "Set RGB major type");
        check_hr(rgb_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32),
                 "Set RGB32 subtype");
        check_hr(MFSetAttributeSize(rgb_type.Get(), MF_MT_FRAME_SIZE,
                                    chosen->width, chosen->height),
                 "Set RGB frame size");
        check_hr(MFSetAttributeRatio(rgb_type.Get(), MF_MT_FRAME_RATE,
                                     chosen->fps_num, chosen->fps_den),
                 "Set RGB frame rate");
        check_hr(MFSetAttributeRatio(rgb_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
                 "Set pixel aspect ratio");
        check_hr(rgb_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
                 "Set progressive mode");
        check_hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr,
                                              rgb_type.Get()),
                 "Request RGB32 output");

        std::cout << "\n[3/3] Capture + stereo split\n";
        ComPtr<IMFSample> sample;
        for (int attempt = 0; attempt < 240 && !sample; ++attempt) {
            DWORD actual_stream = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            check_hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                        0,
                                        &actual_stream,
                                        &flags,
                                        &timestamp,
                                        &sample),
                     "ReadSample");
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                throw std::runtime_error("Video stream ended before a frame arrived");
            }
        }

        if (!sample) {
            throw std::runtime_error("No video frame received from Touch+");
        }

        ComPtr<IMFMediaBuffer> buffer;
        check_hr(sample->ConvertToContiguousBuffer(&buffer),
                 "ConvertToContiguousBuffer");

        BYTE* data = nullptr;
        DWORD max_length = 0;
        DWORD current_length = 0;
        check_hr(buffer->Lock(&data, &max_length, &current_length),
                 "IMFMediaBuffer::Lock");

        const UINT32 stride = chosen->width * 4;
        const size_t required = static_cast<size_t>(stride) * chosen->height;
        if (current_length < required) {
            buffer->Unlock();
            source->Shutdown();
            throw std::runtime_error("Decoded RGB frame was smaller than expected");
        }

        const auto output_dir = std::filesystem::current_path() / L"touchplus-atomic";
        std::filesystem::create_directories(output_dir);

        const auto left = copy_eye(data, stride, 0);
        const auto right = copy_eye(data, stride, kEyeWidth);

        save_png(output_dir / L"touchplus-full.png",
                 kStereoWidth, kStereoHeight, data, stride);
        save_png(output_dir / L"touchplus-left.png",
                 kEyeWidth, kEyeHeight, left.data(), kEyeWidth * 4);
        save_png(output_dir / L"touchplus-right.png",
                 kEyeWidth, kEyeHeight, right.data(), kEyeWidth * 4);

        const FrameStats full_stats = frame_stats(data, kStereoWidth, kStereoHeight, stride);
        const FrameStats left_stats = frame_stats(left.data(), kEyeWidth, kEyeHeight, kEyeWidth * 4);
        const FrameStats right_stats = frame_stats(right.data(), kEyeWidth, kEyeHeight, kEyeWidth * 4);

        buffer->Unlock();
        source->Shutdown();

        std::cout << "Frame capture: PASS\n";
        std::cout << "Stereo split:  PASS (640x480 + 640x480)\n";
        print_stats("Full frame", full_stats);
        print_stats("Left eye ", left_stats);
        print_stats("Right eye", right_stats);

        const bool suspicious_gray = full_stats.stddev < 2.0 ||
                                     (full_stats.max_luma - full_stats.min_luma) < 8;
        if (suspicious_gray) {
            std::cout << "Image-content check: SUSPICIOUS / nearly uniform.\n";
            std::cout << "Retry immediately, then retry once with --legacy-init if needed.\n";
        } else {
            std::cout << "Image-content check: PASS / non-uniform real image detected.\n";
        }

        std::wcout << L"PNG output: " << output_dir.wstring() << L"\n";
        std::cout << "\nPHASE 0C RESULT: "
                  << (suspicious_gray ? "CAPTURED BUT IMAGE LOOKS GRAY" : "PASS")
                  << "\n";
        return suspicious_gray ? 4 : 0;
    } catch (const std::exception& error) {
        std::cerr << "\nPHASE 0C RESULT: FAIL\n" << error.what() << "\n";
        return 1;
    }
}
