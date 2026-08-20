#pragma once

#include "fingertip_identity_v8.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace touchplus::tracking {

enum class AnatomyStatusV9 : std::uint32_t { Unavailable=0, GuidedDistal=1, Rejected=2, Error=3, Stale=4 };
enum class AnatomySourceV9 : std::uint32_t { None=0, FullFrame=1, Roi1=2, Roi2=3, Roi3=4 };
enum class AnatomyPoseModeV9 : std::uint32_t { Unknown=0, Strict2D=1, PerspectiveSilhouette=2, NoCoherentIndexAxis=3, PerspectiveAxisTooWeak=4, PerspectivePathTooWide=5 };
enum class AnatomySyncStatusV9 : std::uint32_t {
    Unchecked=0,
    CurrentFrame=1,
    MotionCompensated=2,
    MissingSourceFrame=3,
    PalmInvalid=4,
    PalmMotionTooLarge=5,
    PalmScaleChanged=6,
    ShapeChanged=7,
    TipNotCurrentDistal=8,
    TooOld=9
};

inline const char* anatomy_status_name_v9(AnatomyStatusV9 status) {
    switch (status) {
        case AnatomyStatusV9::GuidedDistal: return "GUIDED_DISTAL";
        case AnatomyStatusV9::Rejected: return "GUIDED_REJECTED";
        case AnatomyStatusV9::Error: return "ERROR";
        case AnatomyStatusV9::Stale: return "STALE";
        default: return "UNAVAILABLE";
    }
}
inline const char* anatomy_source_name_v9(AnatomySourceV9 source) {
    switch (source) {
        case AnatomySourceV9::FullFrame: return "FULL_FRAME";
        case AnatomySourceV9::Roi1: return "ROI_1";
        case AnatomySourceV9::Roi2: return "ROI_2";
        case AnatomySourceV9::Roi3: return "ROI_3";
        default: return "NONE";
    }
}
inline const char* anatomy_pose_name_v9(AnatomyPoseModeV9 mode) {
    switch (mode) {
        case AnatomyPoseModeV9::Strict2D: return "STRICT_2D";
        case AnatomyPoseModeV9::PerspectiveSilhouette: return "PERSPECTIVE_SILHOUETTE";
        case AnatomyPoseModeV9::NoCoherentIndexAxis: return "NO_COHERENT_INDEX_AXIS";
        case AnatomyPoseModeV9::PerspectiveAxisTooWeak: return "PERSPECTIVE_AXIS_TOO_WEAK";
        case AnatomyPoseModeV9::PerspectivePathTooWide: return "PERSPECTIVE_PATH_TOO_WIDE";
        default: return "UNKNOWN";
    }
}
inline const char* anatomy_sync_status_name_v9(AnatomySyncStatusV9 status) {
    switch (status) {
        case AnatomySyncStatusV9::CurrentFrame: return "CURRENT";
        case AnatomySyncStatusV9::MotionCompensated: return "MOTION_COMPENSATED";
        case AnatomySyncStatusV9::MissingSourceFrame: return "MISSING_SOURCE_FRAME";
        case AnatomySyncStatusV9::PalmInvalid: return "PALM_INVALID";
        case AnatomySyncStatusV9::PalmMotionTooLarge: return "PALM_MOTION_TOO_LARGE";
        case AnatomySyncStatusV9::PalmScaleChanged: return "PALM_SCALE_CHANGED";
        case AnatomySyncStatusV9::ShapeChanged: return "SHAPE_CHANGED";
        case AnatomySyncStatusV9::TipNotCurrentDistal: return "TIP_NOT_CURRENT_DISTAL";
        case AnatomySyncStatusV9::TooOld: return "TOO_OLD";
        default: return "UNCHECKED";
    }
}

struct AnatomyObservationV9 {
    AnatomyStatusV9 status = AnatomyStatusV9::Unavailable;
    AnatomySourceV9 source = AnatomySourceV9::None;
    AnatomyPoseModeV9 pose_mode = AnatomyPoseModeV9::Unknown;
    AnatomySyncStatusV9 sync_status = AnatomySyncStatusV9::Unchecked;
    std::uint32_t frame_id = 0, age_frames = 0, candidate_count = 0;
    int tip_x = -1, tip_y = -1;
    int source_tip_x = -1, source_tip_y = -1;
    double axis_dx = 0.0, axis_dy = 0.0, axis_quality = 0.0;
    double hand_confidence = 0.0, continuity = 0.0, lateral_px = 0.0, extension_px = 0.0;
    double sync_palm_shift_px = 0.0, sync_scale_ratio = 1.0, sync_shape_overlap = 0.0;
};

struct AnatomyFrameSyncSnapshotV9 {
    std::uint32_t frame_id = 0;
    std::vector<std::uint8_t> mask;
    bool palm_valid = false;
    double palm_x_px = 0.0, palm_y_px = 0.0, palm_radius_px = 0.0;
};

inline AnatomyFrameSyncSnapshotV9 make_anatomy_sync_snapshot_v9(
    std::uint32_t frame_id,
    const std::vector<std::uint8_t>& mask,
    const IdentityObservationV8& identity,
    int depth_scale) {
    AnatomyFrameSyncSnapshotV9 out;
    out.frame_id = frame_id;
    out.mask = mask;
    out.palm_valid = identity.hand_valid && identity.palm_valid && identity.palm_gx >= 0 && identity.palm_gy >= 0 && identity.palm_radius > 0.0;
    if (out.palm_valid) {
        out.palm_x_px = identity.palm_gx * depth_scale + 1.0;
        out.palm_y_px = identity.palm_gy * depth_scale + 1.0;
        out.palm_radius_px = identity.palm_radius * depth_scale;
    }
    return out;
}

inline bool mask_cell_v9(const std::vector<std::uint8_t>& mask, int width, int height, int x, int y) {
    return !mask.empty() && x >= 0 && x < width && y >= 0 && y < height && mask[static_cast<std::size_t>(y) * width + x] != 0;
}

inline bool mask_near_fullres_v9(const std::vector<uint8_t>& mask,int mask_width,int mask_height,int full_x,int full_y,int depth_scale,int radius=2){
    if(mask.empty()||mask_width<=0||mask_height<=0||depth_scale<=0)return false; const int gx=full_x/depth_scale,gy=full_y/depth_scale;
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){const int x=gx+dx,y=gy+dy;if(x<0||x>=mask_width||y<0||y>=mask_height)continue;if(mask[static_cast<size_t>(y)*mask_width+x])return true;} return false;
}

inline bool current_distal_support_v9(
    const std::vector<std::uint8_t>& mask,
    int width,
    int height,
    int full_x,
    int full_y,
    double axis_dx,
    double axis_dy,
    int depth_scale) {
    if (mask.empty() || depth_scale <= 0) return false;
    const double n = std::hypot(axis_dx, axis_dy);
    if (!std::isfinite(n) || n < 0.50) return false;
    const double dx = axis_dx / n, dy = axis_dy / n;

    const int gx0 = static_cast<int>(std::lround(static_cast<double>(full_x) / depth_scale));
    const int gy0 = static_cast<int>(std::lround(static_cast<double>(full_y) / depth_scale));
    int bx = -1, by = -1, best_d2 = 999;
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            if (!mask_cell_v9(mask, width, height, gx0 + ox, gy0 + oy)) continue;
            const int d2 = ox * ox + oy * oy;
            if (d2 < best_d2) { best_d2 = d2; bx = gx0 + ox; by = gy0 + oy; }
        }
    }
    if (bx < 0) return false;

    bool boundary = false;
    for (int oy = -2; oy <= 2 && !boundary; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            if (!mask_cell_v9(mask, width, height, bx + ox, by + oy)) { boundary = true; break; }
        }
    }
    if (!boundary) return false;

    int inward_support = 0, outward_clear = 0;
    constexpr int steps[4] = {6, 10, 14, 18};
    for (const int step : steps) {
        const int in_x = static_cast<int>(std::lround(full_x - dx * step));
        const int in_y = static_cast<int>(std::lround(full_y - dy * step));
        const int out_x = static_cast<int>(std::lround(full_x + dx * step));
        const int out_y = static_cast<int>(std::lround(full_y + dy * step));
        if (mask_near_fullres_v9(mask, width, height, in_x, in_y, depth_scale, 1)) ++inward_support;
        if (!mask_near_fullres_v9(mask, width, height, out_x, out_y, depth_scale, 0)) ++outward_clear;
    }
    return inward_support >= 2 && outward_clear >= 3;
}

inline double aligned_shape_overlap_v9(
    const AnatomyFrameSyncSnapshotV9& source,
    const AnatomyFrameSyncSnapshotV9& current,
    int width,
    int height,
    int depth_scale,
    double scale_ratio) {
    if (source.mask.empty() || current.mask.empty() || !source.palm_valid || !current.palm_valid) return 0.0;
    std::size_t source_count = 0, current_count = 0, overlap = 0;
    for (const auto v : current.mask) if (v) ++current_count;
    for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            if (!mask_cell_v9(source.mask, width, height, sx, sy)) continue;
            ++source_count;
            const double source_x_px = sx * depth_scale + 1.0;
            const double source_y_px = sy * depth_scale + 1.0;
            const double mapped_x_px = current.palm_x_px + (source_x_px - source.palm_x_px) * scale_ratio;
            const double mapped_y_px = current.palm_y_px + (source_y_px - source.palm_y_px) * scale_ratio;
            const int mx = static_cast<int>(std::lround(mapped_x_px / depth_scale));
            const int my = static_cast<int>(std::lround(mapped_y_px / depth_scale));
            bool hit = false;
            for (int oy = -1; oy <= 1 && !hit; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                    if (mask_cell_v9(current.mask, width, height, mx + ox, my + oy)) { hit = true; break; }
            if (hit) ++overlap;
        }
    }
    const std::size_t denom = std::min(source_count, current_count);
    return denom ? static_cast<double>(overlap) / static_cast<double>(denom) : 0.0;
}

inline AnatomyObservationV9 synchronize_anatomy_observation_v9(
    AnatomyObservationV9 obs,
    const std::vector<AnatomyFrameSyncSnapshotV9>& history,
    const AnatomyFrameSyncSnapshotV9& current,
    int mask_width,
    int mask_height,
    int depth_scale) {
    obs.source_tip_x = obs.tip_x;
    obs.source_tip_y = obs.tip_y;
    if (obs.status != AnatomyStatusV9::GuidedDistal || obs.tip_x < 0 || obs.tip_y < 0) return obs;
    if (obs.age_frames > 2) { obs.status = AnatomyStatusV9::Stale; obs.sync_status = AnatomySyncStatusV9::TooOld; return obs; }
    if (!current.palm_valid || current.mask.empty()) { obs.status = AnatomyStatusV9::Rejected; obs.sync_status = AnatomySyncStatusV9::PalmInvalid; return obs; }

    if (obs.frame_id == current.frame_id) {
        if (!current_distal_support_v9(current.mask, mask_width, mask_height, obs.tip_x, obs.tip_y, obs.axis_dx, obs.axis_dy, depth_scale)) {
            obs.status = AnatomyStatusV9::Rejected;
            obs.sync_status = AnatomySyncStatusV9::TipNotCurrentDistal;
            return obs;
        }
        obs.sync_status = AnatomySyncStatusV9::CurrentFrame;
        obs.sync_shape_overlap = 1.0;
        return obs;
    }

    const AnatomyFrameSyncSnapshotV9* source = nullptr;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->frame_id == obs.frame_id) { source = &*it; break; }
    }
    if (!source) { obs.status = AnatomyStatusV9::Rejected; obs.sync_status = AnatomySyncStatusV9::MissingSourceFrame; return obs; }
    if (!source->palm_valid || source->mask.empty()) { obs.status = AnatomyStatusV9::Rejected; obs.sync_status = AnatomySyncStatusV9::PalmInvalid; return obs; }

    obs.sync_palm_shift_px = std::hypot(current.palm_x_px - source->palm_x_px, current.palm_y_px - source->palm_y_px);
    obs.sync_scale_ratio = current.palm_radius_px / std::max(1.0, source->palm_radius_px);
    const double max_shift = std::max(72.0, current.palm_radius_px * 1.65);
    if (obs.sync_palm_shift_px > max_shift) { obs.status = AnatomyStatusV9::Rejected; obs.sync_status = AnatomySyncStatusV9::PalmMotionTooLarge; return obs; }
    if (obs.sync_scale_ratio < 0.75 || obs.sync_scale_ratio > 1.33) { obs.status = AnatomyStatusV9::Rejected; obs.sync_status = AnatomySyncStatusV9::PalmScaleChanged; return obs; }

    obs.sync_shape_overlap = aligned_shape_overlap_v9(*source, current, mask_width, mask_height, depth_scale, obs.sync_scale_ratio);
    const double min_overlap = obs.age_frames >= 2 ? 0.68 : 0.56;
    if (obs.sync_shape_overlap < min_overlap) { obs.status = AnatomyStatusV9::Rejected; obs.sync_status = AnatomySyncStatusV9::ShapeChanged; return obs; }

    const double rel_x = static_cast<double>(obs.source_tip_x) - source->palm_x_px;
    const double rel_y = static_cast<double>(obs.source_tip_y) - source->palm_y_px;
    obs.tip_x = static_cast<int>(std::lround(current.palm_x_px + rel_x * obs.sync_scale_ratio));
    obs.tip_y = static_cast<int>(std::lround(current.palm_y_px + rel_y * obs.sync_scale_ratio));

    if (!current_distal_support_v9(current.mask, mask_width, mask_height, obs.tip_x, obs.tip_y, obs.axis_dx, obs.axis_dy, depth_scale)) {
        obs.status = AnatomyStatusV9::Rejected;
        obs.sync_status = AnatomySyncStatusV9::TipNotCurrentDistal;
        return obs;
    }
    obs.sync_status = AnatomySyncStatusV9::MotionCompensated;
    return obs;
}

enum class AnatomyTrackStateV9 { Unknown, Acquiring, Locked };
inline const char* anatomy_track_state_name_v9(AnatomyTrackStateV9 state) {
    switch (state) {
        case AnatomyTrackStateV9::Acquiring: return "ACQUIRING";
        case AnatomyTrackStateV9::Locked: return "LOCKED";
        default: return "UNKNOWN";
    }
}

struct AnatomyDecisionV9 {
    bool has_candidate=false, publish=false, explicit_reject=false, stale=false, jump_rejected=false;
    int tip_x=-1, tip_y=-1, source_tip_x=-1, source_tip_y=-1;
    std::uint32_t age_frames=0;
    std::uint64_t anatomy_id=0;
    AnatomyTrackStateV9 state=AnatomyTrackStateV9::Unknown;
    AnatomySyncStatusV9 sync_status=AnatomySyncStatusV9::Unchecked;
    std::string confidence="LOW";
    double tip_residual=0.0, sync_palm_shift_px=0.0, sync_scale_ratio=1.0, sync_shape_overlap=0.0;
};

class TemporalAnatomyGateV9 {
public:
    AnatomyDecisionV9 update(const AnatomyObservationV9& obs, double palm_radius_full_px) {
        AnatomyDecisionV9 out; out.state=state_; out.anatomy_id=anatomy_id_; copy_sync(out,obs);
        if (obs.status==AnatomyStatusV9::Stale) { out.stale=true; register_miss(); out.state=state_; return out; }
        if (obs.status==AnatomyStatusV9::Rejected) { out.explicit_reject=true; register_miss(); out.state=state_; return out; }
        if (obs.status!=AnatomyStatusV9::GuidedDistal || obs.tip_x<0 || obs.tip_y<0 || obs.hand_confidence<0.80 || obs.axis_quality<0.66 || obs.continuity<0.58) { register_miss(); out.state=state_; return out; }
        if (obs.sync_status!=AnatomySyncStatusV9::CurrentFrame && obs.sync_status!=AnatomySyncStatusV9::MotionCompensated) { out.explicit_reject=true; register_miss(); out.state=state_; return out; }
        if (obs.frame_id==last_observation_frame_id_) { out.state=state_; out.anatomy_id=anatomy_id_; return out; }
        last_observation_frame_id_=obs.frame_id;
        if (!have_tip_) { start_acquisition(obs); fill_candidate(out,obs); out.state=state_; out.anatomy_id=anatomy_id_; return out; }
        out.tip_residual=std::hypot(static_cast<double>(obs.tip_x-tip_x_), static_cast<double>(obs.tip_y-tip_y_));
        const double jump_gate=std::max(24.0,palm_radius_full_px*1.15);
        if (out.tip_residual>jump_gate) { out.jump_rejected=true; register_miss(); out.state=state_; out.anatomy_id=anatomy_id_; return out; }
        tip_x_=obs.tip_x; tip_y_=obs.tip_y; misses_=0; ++acquire_count_;
        if (state_==AnatomyTrackStateV9::Acquiring && acquire_count_>=2) state_=AnatomyTrackStateV9::Locked;
        fill_candidate(out,obs); out.state=state_; out.anatomy_id=anatomy_id_;
        if (state_==AnatomyTrackStateV9::Locked) {
            out.publish=true;
            const bool top_quality=obs.hand_confidence>=0.95 && obs.axis_quality>=0.85 && obs.continuity>=0.85 && (obs.sync_status==AnatomySyncStatusV9::CurrentFrame || obs.sync_shape_overlap>=0.78);
            out.confidence=top_quality?"HIGH":"MEDIUM";
        }
        return out;
    }
    void clear() { state_=AnatomyTrackStateV9::Unknown; have_tip_=false; acquire_count_=0; misses_=0; anatomy_id_=0; next_anatomy_id_=1; last_observation_frame_id_=0; tip_x_=tip_y_=-1; }
private:
    static void copy_sync(AnatomyDecisionV9& out,const AnatomyObservationV9& obs) { out.age_frames=obs.age_frames; out.source_tip_x=obs.source_tip_x; out.source_tip_y=obs.source_tip_y; out.sync_status=obs.sync_status; out.sync_palm_shift_px=obs.sync_palm_shift_px; out.sync_scale_ratio=obs.sync_scale_ratio; out.sync_shape_overlap=obs.sync_shape_overlap; }
    void fill_candidate(AnatomyDecisionV9& out,const AnatomyObservationV9& obs) const { out.has_candidate=true; out.tip_x=obs.tip_x; out.tip_y=obs.tip_y; copy_sync(out,obs); }
    void start_acquisition(const AnatomyObservationV9& obs) { state_=AnatomyTrackStateV9::Acquiring; have_tip_=true; acquire_count_=1; misses_=0; anatomy_id_=next_anatomy_id_++; tip_x_=obs.tip_x; tip_y_=obs.tip_y; }
    void register_miss() { if (++misses_>3) { state_=AnatomyTrackStateV9::Unknown; have_tip_=false; acquire_count_=0; anatomy_id_=0; tip_x_=tip_y_=-1; } }
    AnatomyTrackStateV9 state_=AnatomyTrackStateV9::Unknown; bool have_tip_=false; int acquire_count_=0,misses_=0; std::uint64_t anatomy_id_=0,next_anatomy_id_=1; std::uint32_t last_observation_frame_id_=0; int tip_x_=-1,tip_y_=-1;
};

enum class FusionModeV9 { Unknown, GeometryAnatomyAgree, AnatomyOnly };
inline const char* fusion_mode_name_v9(FusionModeV9 mode) { switch(mode){case FusionModeV9::GeometryAnatomyAgree:return "GEOMETRY+ANATOMY";case FusionModeV9::AnatomyOnly:return "ANATOMY_ONLY";default:return "UNKNOWN";} }
struct FusedIdentityV9 { bool publish=false; int pixel_x=-1,pixel_y=-1; std::uint64_t identity_id=0; std::string confidence="LOW"; FusionModeV9 mode=FusionModeV9::Unknown; std::string reason="anatomy-not-ready"; double agreement_distance_px=0.0; };

inline bool sync_reject_is_motion_v9(AnatomySyncStatusV9 status) {
    return status==AnatomySyncStatusV9::MissingSourceFrame || status==AnatomySyncStatusV9::PalmInvalid || status==AnatomySyncStatusV9::PalmMotionTooLarge || status==AnatomySyncStatusV9::PalmScaleChanged || status==AnatomySyncStatusV9::ShapeChanged || status==AnatomySyncStatusV9::TooOld;
}

inline FusedIdentityV9 fuse_identity_v9(const IdentityObservationV8& go,const IdentityDecisionV8& gd,const AnatomyDecisionV9& ad,const std::vector<uint8_t>& mask,int mw,int mh,int scale){
    FusedIdentityV9 out;
    if(ad.sync_status==AnatomySyncStatusV9::TipNotCurrentDistal){out.reason="anatomy-not-current-distal";return out;}
    if(sync_reject_is_motion_v9(ad.sync_status)){out.reason="anatomy-stale-motion";return out;}
    if(ad.explicit_reject){out.reason="anatomy-reject";return out;} if(ad.stale){out.reason="anatomy-stale";return out;} if(ad.jump_rejected){out.reason="anatomy-jump-reject";return out;} if(!ad.publish){out.reason="anatomy-not-locked";return out;}
    if(!go.hand_valid||!go.palm_valid){out.reason="palm-invalid";return out;} if(gd.palm_rejected){out.reason="palm-temporal-reject";return out;} if(gd.jump_rejected){out.reason="geometry-jump-reject";return out;}
    if(!mask_near_fullres_v9(mask,mw,mh,ad.tip_x,ad.tip_y,scale,2)){out.reason="anatomy-tip-outside-current-silhouette";return out;}
    if(gd.publish&&gd.has_candidate){const int gx=gd.tip_gx*scale+1,gy=gd.tip_gy*scale+1;out.agreement_distance_px=std::hypot(static_cast<double>(gx-ad.tip_x),static_cast<double>(gy-ad.tip_y));const double gate=std::max(22.0,go.palm_radius*scale*1.15);if(out.agreement_distance_px>gate){out.reason="geometry-anatomy-disagree";return out;}out.publish=true;out.pixel_x=ad.tip_x;out.pixel_y=ad.tip_y;out.identity_id=gd.branch_id;out.mode=FusionModeV9::GeometryAnatomyAgree;out.confidence=(gd.confidence=="HIGH"&&ad.confidence=="HIGH")?"HIGH":"MEDIUM";out.reason="geometry-anatomy-agree";return out;}
    if(ad.age_frames>1){out.reason="anatomy-only-too-old";return out;}
    if(ad.sync_status==AnatomySyncStatusV9::MotionCompensated && ad.sync_shape_overlap<0.72){out.reason="anatomy-only-sync-shape-weak";return out;}
    out.publish=true;out.pixel_x=ad.tip_x;out.pixel_y=ad.tip_y;out.identity_id=0x8000000000000000ULL|ad.anatomy_id;out.mode=FusionModeV9::AnatomyOnly;out.confidence=ad.confidence;out.reason="anatomy-only-current-distal";return out;
}

inline bool final_identity_stereo_gate_v9(const std::string& i,const std::string& s){return confidence_passes_v8(i)&&confidence_passes_v8(s);}

} // namespace touchplus::tracking
