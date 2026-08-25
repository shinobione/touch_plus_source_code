#pragma once

// Phase 2C.1J diagnostic-only anatomy IPC.
//
// This deliberately uses different named shared-memory channels from the
// accepted Phase 2B.9C anatomy sidecar. Results read here are telemetry only
// and are never consumed by FingertipTrackerV9, fusion, stereo or contact.

#include "fingertip_anatomy_ipc_v9.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace touchplus::tracking {

constexpr wchar_t kShadowAnatomyFrameMapNameV2C1J[] =
    L"Local\\TouchPlusRevival2C1J_ShadowFrame_v1";
constexpr wchar_t kShadowAnatomyResultMapNameV2C1J[] =
    L"Local\\TouchPlusRevival2C1J_ShadowResult_v1";

struct ShadowAnatomyObservationV2C1J {
    AnatomyStatusV9 status = AnatomyStatusV9::Unavailable;
    AnatomySourceV9 source = AnatomySourceV9::None;
    AnatomyPoseModeV9 pose_mode = AnatomyPoseModeV9::Unknown;
    std::uint32_t frame_id = 0;
    std::uint32_t age_frames = 0;
    std::uint32_t candidate_count = 0;
    int tip_x = -1;
    int tip_y = -1;
    double axis_dx = 0.0;
    double axis_dy = 0.0;
    double axis_quality = 0.0;
    double hand_confidence = 0.0;
    double continuity = 0.0;
    double lateral_px = 0.0;
    double extension_px = 0.0;
    std::uint32_t reason_code = 0;
};

#ifdef _WIN32
class ShadowAnatomySidecarBridgeV2C1J {
public:
    ShadowAnatomySidecarBridgeV2C1J() = default;
    ShadowAnatomySidecarBridgeV2C1J(
        const ShadowAnatomySidecarBridgeV2C1J&) = delete;
    ShadowAnatomySidecarBridgeV2C1J& operator=(
        const ShadowAnatomySidecarBridgeV2C1J&) = delete;
    ~ShadowAnatomySidecarBridgeV2C1J() { close(); }

    bool ensure_open() {
        if (frame_view_ && result_view_) return true;
        close();
        frame_mapping_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(kAnatomyFrameMapBytesV9),
            kShadowAnatomyFrameMapNameV2C1J);
        if (!frame_mapping_) {
            last_error_ = GetLastError();
            close();
            return false;
        }
        result_mapping_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(kAnatomyResultMapBytesV9),
            kShadowAnatomyResultMapNameV2C1J);
        if (!result_mapping_) {
            last_error_ = GetLastError();
            close();
            return false;
        }
        frame_view_ = static_cast<std::uint8_t*>(MapViewOfFile(
            frame_mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kAnatomyFrameMapBytesV9));
        result_view_ = static_cast<std::uint8_t*>(MapViewOfFile(
            result_mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kAnatomyResultMapBytesV9));
        if (!frame_view_ || !result_view_) {
            last_error_ = GetLastError();
            close();
            return false;
        }
        last_error_ = ERROR_SUCCESS;
        return true;
    }

    bool publish_frame(
        std::uint32_t frame_id,
        const std::vector<std::uint8_t>& left_gray,
        const std::vector<std::uint8_t>& shadow_mask,
        bool accepted_hand_valid,
        bool background_ready) {

        if (!ensure_open() ||
            left_gray.size() < kAnatomyLeftBytesV9 ||
            shadow_mask.size() < kAnatomyMaskBytesV9) {
            return false;
        }

        auto* header = reinterpret_cast<AnatomyFrameHeaderV9*>(frame_view_);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&header->sequence_lock));
        MemoryBarrier();
        header->magic = kAnatomyFrameMagicV9;
        header->version = kAnatomyProtocolVersionV9;
        header->frame_id = frame_id;
        header->width = kAnatomyFrameWidthV9;
        header->height = kAnatomyFrameHeightV9;
        header->mask_width = kAnatomyMaskWidthV9;
        header->mask_height = kAnatomyMaskHeightV9;
        // Preserve the accepted hand gate as telemetry. The shadow sidecar is
        // explicitly allowed to ignore this field on its separate IPC only.
        header->hand_valid = accepted_hand_valid ? 1U : 0U;
        header->background_ready = background_ready ? 1U : 0U;

        std::uint8_t* left_dst = frame_view_ + sizeof(AnatomyFrameHeaderV9);
        std::uint8_t* mask_dst = left_dst + kAnatomyLeftBytesV9;
        std::memcpy(left_dst, left_gray.data(), kAnatomyLeftBytesV9);
        std::memcpy(mask_dst, shadow_mask.data(), kAnatomyMaskBytesV9);
        MemoryBarrier();
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&header->sequence_lock));
        return true;
    }

    ShadowAnatomyObservationV2C1J read_result(
        std::uint32_t current_frame_id,
        std::uint32_t max_age_frames = 3) {

        ShadowAnatomyObservationV2C1J out;
        if (!ensure_open()) return out;
        auto* shared = reinterpret_cast<AnatomyResultPacketV9*>(result_view_);
        const LONG seq1 = shared->sequence_lock;
        if ((seq1 & 1L) != 0L) return out;
        MemoryBarrier();
        AnatomyResultPacketV9 local{};
        std::memcpy(&local, result_view_, sizeof(local));
        MemoryBarrier();
        const LONG seq2 = shared->sequence_lock;
        if (seq1 != seq2 || (seq2 & 1L) != 0L ||
            local.magic != kAnatomyResultMagicV9 ||
            local.version != kAnatomyProtocolVersionV9 ||
            local.frame_id == 0) {
            return out;
        }

        out.frame_id = local.frame_id;
        out.status = static_cast<AnatomyStatusV9>(local.status);
        out.source = static_cast<AnatomySourceV9>(local.source);
        out.pose_mode = static_cast<AnatomyPoseModeV9>(local.pose_mode);
        out.candidate_count = local.candidate_count;
        out.tip_x = static_cast<int>(std::lround(local.tip_x));
        out.tip_y = static_cast<int>(std::lround(local.tip_y));
        out.axis_dx = local.axis_dx;
        out.axis_dy = local.axis_dy;
        out.axis_quality = local.axis_quality;
        out.hand_confidence = local.hand_confidence;
        out.continuity = local.continuity;
        out.lateral_px = local.lateral_px;
        out.extension_px = local.extension_px;
        out.reason_code = local.reason_code;

        if (local.frame_id > current_frame_id) {
            out.status = AnatomyStatusV9::Stale;
            return out;
        }
        out.age_frames = current_frame_id - local.frame_id;
        if (out.age_frames > max_age_frames) {
            out.status = AnatomyStatusV9::Stale;
        }
        return out;
    }

    DWORD last_error() const { return last_error_; }

    void close() {
        if (frame_view_) {
            UnmapViewOfFile(frame_view_);
            frame_view_ = nullptr;
        }
        if (result_view_) {
            UnmapViewOfFile(result_view_);
            result_view_ = nullptr;
        }
        if (frame_mapping_) {
            CloseHandle(frame_mapping_);
            frame_mapping_ = nullptr;
        }
        if (result_mapping_) {
            CloseHandle(result_mapping_);
            result_mapping_ = nullptr;
        }
    }

private:
    HANDLE frame_mapping_ = nullptr;
    HANDLE result_mapping_ = nullptr;
    std::uint8_t* frame_view_ = nullptr;
    std::uint8_t* result_view_ = nullptr;
    DWORD last_error_ = ERROR_SUCCESS;
};
#else
class ShadowAnatomySidecarBridgeV2C1J {
public:
    bool ensure_open() { return false; }
    bool publish_frame(std::uint32_t, const std::vector<std::uint8_t>&,
        const std::vector<std::uint8_t>&, bool, bool) { return false; }
    ShadowAnatomyObservationV2C1J read_result(std::uint32_t, std::uint32_t = 3) {
        return {};
    }
    std::uint32_t last_error() const { return 0; }
    void close() {}
};
#endif

} // namespace touchplus::tracking
