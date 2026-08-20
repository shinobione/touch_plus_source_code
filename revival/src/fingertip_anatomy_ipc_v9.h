#pragma once

#include "fingertip_anatomy_v9.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace touchplus::tracking {

constexpr std::uint32_t kAnatomyFrameMagicV9 = 0x39465054U;
constexpr std::uint32_t kAnatomyResultMagicV9 = 0x39525054U;
constexpr std::uint32_t kAnatomyProtocolVersionV9 = 1U;
constexpr int kAnatomyFrameWidthV9 = 640;
constexpr int kAnatomyFrameHeightV9 = 480;
constexpr int kAnatomyMaskWidthV9 = 320;
constexpr int kAnatomyMaskHeightV9 = 240;
constexpr size_t kAnatomyLeftBytesV9 = static_cast<size_t>(kAnatomyFrameWidthV9) * kAnatomyFrameHeightV9;
constexpr size_t kAnatomyMaskBytesV9 = static_cast<size_t>(kAnatomyMaskWidthV9) * kAnatomyMaskHeightV9;
constexpr wchar_t kAnatomyFrameMapNameV9[] = L"Local\\TouchPlusRevival2B9C_Frame_v1";
constexpr wchar_t kAnatomyResultMapNameV9[] = L"Local\\TouchPlusRevival2B9C_Result_v1";

#pragma pack(push, 1)
struct AnatomyFrameHeaderV9 {
    volatile long sequence_lock = 0;
    std::uint32_t magic = 0, version = 0, frame_id = 0, width = 0, height = 0, mask_width = 0, mask_height = 0, hand_valid = 0, background_ready = 0;
    std::uint32_t reserved[6]{};
};
struct AnatomyResultPacketV9 {
    volatile long sequence_lock = 0;
    std::uint32_t magic = 0, version = 0, frame_id = 0, status = 0, source = 0, pose_mode = 0, candidate_count = 0;
    float tip_x = -1.0f, tip_y = -1.0f, axis_dx = 0.0f, axis_dy = 0.0f, axis_quality = 0.0f, hand_confidence = 0.0f, continuity = 0.0f, lateral_px = 0.0f, extension_px = 0.0f;
    std::uint32_t reason_code = 0;
    std::uint32_t reserved[6]{};
};
#pragma pack(pop)

static_assert(sizeof(AnatomyFrameHeaderV9) == 64, "Phase 2B.9C frame header ABI changed");
static_assert(sizeof(AnatomyResultPacketV9) == 96, "Phase 2B.9C result packet ABI changed");
constexpr size_t kAnatomyFrameMapBytesV9 = sizeof(AnatomyFrameHeaderV9) + kAnatomyLeftBytesV9 + kAnatomyMaskBytesV9;
constexpr size_t kAnatomyResultMapBytesV9 = sizeof(AnatomyResultPacketV9);

#ifdef _WIN32
class AnatomySidecarBridgeV9 {
public:
    ~AnatomySidecarBridgeV9() { close(); }
    AnatomySidecarBridgeV9() = default;
    AnatomySidecarBridgeV9(const AnatomySidecarBridgeV9&) = delete;
    AnatomySidecarBridgeV9& operator=(const AnatomySidecarBridgeV9&) = delete;

    bool ensure_open() {
        if (frame_view_ && result_view_) return true;
        close();
        frame_mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(kAnatomyFrameMapBytesV9), kAnatomyFrameMapNameV9);
        if (!frame_mapping_) { last_error_ = GetLastError(); close(); return false; }
        result_mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(kAnatomyResultMapBytesV9), kAnatomyResultMapNameV9);
        if (!result_mapping_) { last_error_ = GetLastError(); close(); return false; }
        frame_view_ = static_cast<std::uint8_t*>(MapViewOfFile(frame_mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kAnatomyFrameMapBytesV9));
        result_view_ = static_cast<std::uint8_t*>(MapViewOfFile(result_mapping_, FILE_MAP_ALL_ACCESS, 0, 0, kAnatomyResultMapBytesV9));
        if (!frame_view_ || !result_view_) { last_error_ = GetLastError(); close(); return false; }
        last_error_ = ERROR_SUCCESS;
        return true;
    }

    bool publish_frame(std::uint32_t frame_id, const std::vector<std::uint8_t>& left_gray, const std::vector<std::uint8_t>& selected_mask, bool hand_valid, bool background_ready) {
        if (!ensure_open() || left_gray.size() < kAnatomyLeftBytesV9 || (!selected_mask.empty() && selected_mask.size() < kAnatomyMaskBytesV9)) return false;
        auto* header = reinterpret_cast<AnatomyFrameHeaderV9*>(frame_view_);
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&header->sequence_lock));
        MemoryBarrier();
        header->magic=kAnatomyFrameMagicV9; header->version=kAnatomyProtocolVersionV9; header->frame_id=frame_id; header->width=kAnatomyFrameWidthV9; header->height=kAnatomyFrameHeightV9; header->mask_width=kAnatomyMaskWidthV9; header->mask_height=kAnatomyMaskHeightV9; header->hand_valid=hand_valid?1U:0U; header->background_ready=background_ready?1U:0U;
        std::uint8_t* left_dst=frame_view_+sizeof(AnatomyFrameHeaderV9); std::uint8_t* mask_dst=left_dst+kAnatomyLeftBytesV9;
        std::memcpy(left_dst,left_gray.data(),kAnatomyLeftBytesV9);
        if(selected_mask.empty()) std::memset(mask_dst,0,kAnatomyMaskBytesV9); else std::memcpy(mask_dst,selected_mask.data(),kAnatomyMaskBytesV9);
        MemoryBarrier(); InterlockedIncrement(reinterpret_cast<volatile LONG*>(&header->sequence_lock)); return true;
    }

    AnatomyObservationV9 read_result(std::uint32_t current_frame_id, std::uint32_t max_age_frames=3) {
        AnatomyObservationV9 out; if(!ensure_open()) return out;
        auto* shared=reinterpret_cast<AnatomyResultPacketV9*>(result_view_); const LONG seq1=shared->sequence_lock; if((seq1&1L)!=0L) return out; MemoryBarrier();
        AnatomyResultPacketV9 local{}; std::memcpy(&local,result_view_,sizeof(local)); MemoryBarrier(); const LONG seq2=shared->sequence_lock;
        if(seq1!=seq2||(seq2&1L)!=0L||local.magic!=kAnatomyResultMagicV9||local.version!=kAnatomyProtocolVersionV9||local.frame_id==0) return out;
        out.frame_id=local.frame_id; out.status=static_cast<AnatomyStatusV9>(local.status); out.source=static_cast<AnatomySourceV9>(local.source); out.pose_mode=static_cast<AnatomyPoseModeV9>(local.pose_mode); out.candidate_count=local.candidate_count; out.tip_x=static_cast<int>(std::lround(local.tip_x)); out.tip_y=static_cast<int>(std::lround(local.tip_y)); out.axis_dx=local.axis_dx; out.axis_dy=local.axis_dy; out.axis_quality=local.axis_quality; out.hand_confidence=local.hand_confidence; out.continuity=local.continuity; out.lateral_px=local.lateral_px; out.extension_px=local.extension_px;
        if(local.frame_id>current_frame_id){out.status=AnatomyStatusV9::Stale;return out;} out.age_frames=current_frame_id-local.frame_id; if(out.age_frames>max_age_frames) out.status=AnatomyStatusV9::Stale; return out;
    }

    DWORD last_error() const { return last_error_; }
    void close() { if(frame_view_){UnmapViewOfFile(frame_view_);frame_view_=nullptr;} if(result_view_){UnmapViewOfFile(result_view_);result_view_=nullptr;} if(frame_mapping_){CloseHandle(frame_mapping_);frame_mapping_=nullptr;} if(result_mapping_){CloseHandle(result_mapping_);result_mapping_=nullptr;} }
private:
    HANDLE frame_mapping_=nullptr,result_mapping_=nullptr; std::uint8_t* frame_view_=nullptr; std::uint8_t* result_view_=nullptr; DWORD last_error_=ERROR_SUCCESS;
};
#else
class AnatomySidecarBridgeV9 {
public:
    bool ensure_open(){return false;} bool publish_frame(std::uint32_t,const std::vector<std::uint8_t>&,const std::vector<std::uint8_t>&,bool,bool){return false;} AnatomyObservationV9 read_result(std::uint32_t,std::uint32_t=3){return{};} std::uint32_t last_error()const{return 0;} void close(){}
};
#endif

} // namespace touchplus::tracking
