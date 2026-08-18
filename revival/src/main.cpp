#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kTouchPlusVidPid[] = L"vid_1e4e&pid_0107";
constexpr wchar_t kTouchPlusFriendlyName[] = L"touch+ camera";
constexpr UINT32 kExpectedWidth = 1280;
constexpr UINT32 kExpectedHeight = 480;
constexpr UINT32 kEyeWidth = 640;
constexpr UINT32 kEyeHeight = 480;

void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(hr) << ")";
        throw std::runtime_error(oss.str());
    }
}

struct ComRuntime {
    ComRuntime() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx"); }
    ~ComRuntime() { CoUninitialize(); }
};

struct MediaFoundationRuntime {
    MediaFoundationRuntime() { check(MFStartup(MF_VERSION), "MFStartup"); }
    ~MediaFoundationRuntime() { MFShutdown(); }
};

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
        if (!std::isprint(static_cast<unsigned char>(chars[i]))) {
            return "GUID";
        }
    }
    return std::string(chars);
}

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
        const HRESULT hr = reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type);
        if (hr == MF_E_NO_MORE_TYPES) {
            break;
        }
        check(hr, "GetNativeMediaType");

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

const Mode* choose_mode(const std::vector<Mode>& modes) {
    const Mode* best = nullptr;
    for (const auto& mode : modes) {
        if (mode.width != kExpectedWidth || mode.height != kExpectedHeight) {
            continue;
        }
        if (best == nullptr) {
            best = &mode;
            continue;
        }
        const double fps = mode.fps_den ? static_cast<double>(mode.fps_num) / mode.fps_den : 0.0;
        const double best_fps = best->fps_den ? static_cast<double>(best->fps_num) / best->fps_den : 0.0;
        if (fps > best_fps) {
            best = &mode;
        }
    }
    return best;
}

void save_png(const std::filesystem::path& path, UINT32 width, UINT32 height,
              const BYTE* bgra, UINT32 stride) {
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)),
          "CoCreateInstance(WICImagingFactory)");

    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream), "IWICImagingFactory::CreateStream");
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE),
          "IWICStream::InitializeFromFilename");

    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder),
          "IWICImagingFactory::CreateEncoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
          "IWICBitmapEncoder::Initialize");

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    check(encoder->CreateNewFrame(&frame, &options), "IWICBitmapEncoder::CreateNewFrame");
    check(frame->Initialize(options.Get()), "IWICBitmapFrameEncode::Initialize");
    check(frame->SetSize(width, height), "IWICBitmapFrameEncode::SetSize");

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&pixel_format), "IWICBitmapFrameEncode::SetPixelFormat");
    if (pixel_format != GUID_WICPixelFormat32bppBGRA) {
        throw std::runtime_error("WIC refused 32bpp BGRA output");
    }

    check(frame->WritePixels(height, stride, stride * height, const_cast<BYTE*>(bgra)),
          "IWICBitmapFrameEncode::WritePixels");
    check(frame->Commit(), "IWICBitmapFrameEncode::Commit");
    check(encoder->Commit(), "IWICBitmapEncoder::Commit");
}

std::vector<BYTE> copy_eye(const BYTE* frame, UINT32 full_stride, UINT32 x_offset) {
    const UINT32 eye_stride = kEyeWidth * 4;
    std::vector<BYTE> output(static_cast<size_t>(eye_stride) * kEyeHeight);
    for (UINT32 y = 0; y < kEyeHeight; ++y) {
        const BYTE* src = frame + static_cast<size_t>(y) * full_stride + static_cast<size_t>(x_offset) * 4;
        BYTE* dst = output.data() + static_cast<size_t>(y) * eye_stride;
        std::copy_n(src, eye_stride, dst);
    }
    return output;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const bool list_only = argc > 1 && std::wstring(argv[1]) == L"--list";

    try {
        ComRuntime com;
        MediaFoundationRuntime media_foundation;

        ComPtr<IMFAttributes> enum_attributes;
        check(MFCreateAttributes(&enum_attributes, 1), "MFCreateAttributes");
        check(enum_attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
              "Set video capture source type");

        IMFActivate** devices = nullptr;
        UINT32 device_count = 0;
        check(MFEnumDeviceSources(enum_attributes.Get(), &devices, &device_count),
              "MFEnumDeviceSources");

        std::wcout << L"TouchPlus Revival - Phase 0 hardware probe\n\n";
        std::wcout << L"Video capture devices: " << device_count << L"\n";

        ComPtr<IMFActivate> selected;
        for (UINT32 i = 0; i < device_count; ++i) {
            const std::wstring name = get_string(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            const std::wstring link = get_string(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            const std::wstring name_lower = lower(name);
            const std::wstring link_lower = lower(link);
            const bool match = link_lower.find(kTouchPlusVidPid) != std::wstring::npos ||
                               name_lower.find(kTouchPlusFriendlyName) != std::wstring::npos;

            std::wcout << (match ? L"  [MATCH] " : L"  [     ] ") << i << L": " << name << L"\n";
            std::wcout << L"          " << link << L"\n";
            if (match && !selected) {
                selected = devices[i];
            }
        }

        for (UINT32 i = 0; i < device_count; ++i) {
            devices[i]->Release();
        }
        CoTaskMemFree(devices);

        if (list_only) {
            return selected ? 0 : 2;
        }

        if (!selected) {
            std::cerr << "\nTouch+ not found. Expected USB identity VID_1E4E / PID_0107.\n";
            std::cerr << "Run with --list after reconnecting the sensor and try another USB port.\n";
            return 2;
        }

        std::cout << "\nUSB/UVC identity: PASS\n";

        ComPtr<IMFMediaSource> source;
        check(selected->ActivateObject(IID_PPV_ARGS(&source)), "ActivateObject(IMFMediaSource)");

        ComPtr<IMFAttributes> reader_attributes;
        check(MFCreateAttributes(&reader_attributes, 2), "MFCreateAttributes(source reader)");
        check(reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE),
              "Enable Media Foundation video processing");

        ComPtr<IMFSourceReader> reader;
        check(MFCreateSourceReaderFromMediaSource(source.Get(), reader_attributes.Get(), &reader),
              "MFCreateSourceReaderFromMediaSource");

        const auto modes = enumerate_modes(reader.Get());
        std::cout << "Native video modes:\n";
        for (const auto& mode : modes) {
            const double fps = mode.fps_den ? static_cast<double>(mode.fps_num) / mode.fps_den : 0.0;
            std::cout << "  " << mode.width << "x" << mode.height << " @ "
                      << std::fixed << std::setprecision(2) << fps << " fps  ["
                      << fourcc_from_guid(mode.subtype) << "]\n";
        }

        const Mode* chosen = choose_mode(modes);
        if (!chosen) {
            std::cerr << "\nStereo stream: FAIL - no native 1280x480 mode was exposed.\n";
            std::cerr << "The mode list above is the important diagnostic output.\n";
            source->Shutdown();
            return 3;
        }

        const double chosen_fps = chosen->fps_den
            ? static_cast<double>(chosen->fps_num) / chosen->fps_den
            : 0.0;
        std::cout << "\nStereo stream mode: " << chosen->width << "x" << chosen->height
                  << " @ " << std::fixed << std::setprecision(2) << chosen_fps << " fps\n";

        check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, chosen->type.Get()),
              "Set native Touch+ media type");

        ComPtr<IMFMediaType> rgb_type;
        check(MFCreateMediaType(&rgb_type), "MFCreateMediaType(RGB32)");
        check(rgb_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set RGB major type");
        check(rgb_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set RGB32 subtype");
        check(MFSetAttributeSize(rgb_type.Get(), MF_MT_FRAME_SIZE, chosen->width, chosen->height),
              "Set RGB frame size");
        check(MFSetAttributeRatio(rgb_type.Get(), MF_MT_FRAME_RATE, chosen->fps_num, chosen->fps_den),
              "Set RGB frame rate");
        check(MFSetAttributeRatio(rgb_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
              "Set pixel aspect ratio");
        check(rgb_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
              "Set progressive mode");
        check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgb_type.Get()),
              "Request RGB32 output");

        ComPtr<IMFSample> sample;
        for (int attempt = 0; attempt < 180 && !sample; ++attempt) {
            DWORD actual_stream = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &actual_stream,
                                     &flags, &timestamp, &sample),
                  "ReadSample");
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                throw std::runtime_error("video stream ended before a frame was received");
            }
        }
        if (!sample) {
            throw std::runtime_error("no video frame received from Touch+");
        }

        ComPtr<IMFMediaBuffer> buffer;
        check(sample->ConvertToContiguousBuffer(&buffer), "ConvertToContiguousBuffer");

        BYTE* data = nullptr;
        DWORD max_length = 0;
        DWORD current_length = 0;
        check(buffer->Lock(&data, &max_length, &current_length), "IMFMediaBuffer::Lock");

        const UINT32 stride = chosen->width * 4;
        const size_t required = static_cast<size_t>(stride) * chosen->height;
        if (current_length < required) {
            buffer->Unlock();
            throw std::runtime_error("RGB frame was smaller than expected");
        }

        const std::filesystem::path output_dir = std::filesystem::current_path() / "touchplus-probe";
        std::filesystem::create_directories(output_dir);

        save_png(output_dir / L"touchplus-full.png", chosen->width, chosen->height, data, stride);
        const auto left = copy_eye(data, stride, 0);
        const auto right = copy_eye(data, stride, kEyeWidth);
        save_png(output_dir / L"touchplus-left.png", kEyeWidth, kEyeHeight, left.data(), kEyeWidth * 4);
        save_png(output_dir / L"touchplus-right.png", kEyeWidth, kEyeHeight, right.data(), kEyeWidth * 4);
        buffer->Unlock();

        std::cout << "Frame capture: PASS\n";
        std::cout << "Stereo split:  PASS (640x480 + 640x480)\n";
        std::wcout << L"PNG output:    " << output_dir.wstring() << L"\n";
        std::cout << "\nPHASE 0A RESULT: PASS\n";

        source->Shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nPHASE 0A RESULT: FAIL\n" << error.what() << "\n";
        return 1;
    }
}
