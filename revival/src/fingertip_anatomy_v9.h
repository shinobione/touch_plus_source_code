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

struct AnatomyObservationV9 {
    AnatomyStatusV9 status = AnatomyStatusV9::Unavailable;
    AnatomySourceV9 source = AnatomySourceV9::None;
    AnatomyPoseModeV9 pose_mode = AnatomyPoseModeV9::Unknown;
    std::uint32_t frame_id = 0, age_frames = 0, candidate_count = 0;
    int tip_x = -1, tip_y = -1;
    double axis_dx = 0.0, axis_dy = 0.0, axis_quality = 0.0;
    double hand_confidence = 0.0, continuity = 0.0, lateral_px = 0.0, extension_px = 0.0;
};

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
    int tip_x=-1, tip_y=-1;
    std::uint64_t anatomy_id=0;
    AnatomyTrackStateV9 state=AnatomyTrackStateV9::Unknown;
    std::string confidence="LOW";
    double tip_residual=0.0;
};

class TemporalAnatomyGateV9 {
public:
    AnatomyDecisionV9 update(const AnatomyObservationV9& obs, double palm_radius_full_px) {
        AnatomyDecisionV9 out; out.state=state_; out.anatomy_id=anatomy_id_;
        if (obs.status==AnatomyStatusV9::Stale) { out.stale=true; register_miss(); out.state=state_; return out; }
        if (obs.status==AnatomyStatusV9::Rejected) { out.explicit_reject=true; register_miss(); out.state=state_; return out; }
        if (obs.status!=AnatomyStatusV9::GuidedDistal || obs.tip_x<0 || obs.tip_y<0 || obs.hand_confidence<0.80 || obs.axis_quality<0.66 || obs.continuity<0.58) { register_miss(); out.state=state_; return out; }
        if (obs.frame_id==last_observation_frame_id_) { out.state=state_; out.anatomy_id=anatomy_id_; return out; }
        last_observation_frame_id_=obs.frame_id;
        if (!have_tip_) { start_acquisition(obs); fill_candidate(out,obs); out.state=state_; out.anatomy_id=anatomy_id_; return out; }
        out.tip_residual=std::hypot(static_cast<double>(obs.tip_x-tip_x_), static_cast<double>(obs.tip_y-tip_y_));
        const double jump_gate=std::max(24.0,palm_radius_full_px*1.15);
        if (out.tip_residual>jump_gate) { out.jump_rejected=true; register_miss(); out.state=state_; out.anatomy_id=anatomy_id_; return out; }
        tip_x_=obs.tip_x; tip_y_=obs.tip_y; misses_=0; ++acquire_count_;
        if (state_==AnatomyTrackStateV9::Acquiring && acquire_count_>=2) state_=AnatomyTrackStateV9::Locked;
        fill_candidate(out,obs); out.state=state_; out.anatomy_id=anatomy_id_;
        if (state_==AnatomyTrackStateV9::Locked) { out.publish=true; out.confidence=(obs.hand_confidence>=0.95 && obs.axis_quality>=0.85 && obs.continuity>=0.85)?"HIGH":"MEDIUM"; }
        return out;
    }
    void clear() { state_=AnatomyTrackStateV9::Unknown; have_tip_=false; acquire_count_=0; misses_=0; anatomy_id_=0; next_anatomy_id_=1; last_observation_frame_id_=0; tip_x_=tip_y_=-1; }
private:
    void fill_candidate(AnatomyDecisionV9& out,const AnatomyObservationV9& obs) const { out.has_candidate=true; out.tip_x=obs.tip_x; out.tip_y=obs.tip_y; }
    void start_acquisition(const AnatomyObservationV9& obs) { state_=AnatomyTrackStateV9::Acquiring; have_tip_=true; acquire_count_=1; misses_=0; anatomy_id_=next_anatomy_id_++; tip_x_=obs.tip_x; tip_y_=obs.tip_y; }
    void register_miss() { if (++misses_>3) { state_=AnatomyTrackStateV9::Unknown; have_tip_=false; acquire_count_=0; anatomy_id_=0; tip_x_=tip_y_=-1; } }
    AnatomyTrackStateV9 state_=AnatomyTrackStateV9::Unknown; bool have_tip_=false; int acquire_count_=0,misses_=0; std::uint64_t anatomy_id_=0,next_anatomy_id_=1; std::uint32_t last_observation_frame_id_=0; int tip_x_=-1,tip_y_=-1;
};

enum class FusionModeV9 { Unknown, GeometryAnatomyAgree, AnatomyOnly };
inline const char* fusion_mode_name_v9(FusionModeV9 mode) { switch(mode){case FusionModeV9::GeometryAnatomyAgree:return "GEOMETRY+ANATOMY";case FusionModeV9::AnatomyOnly:return "ANATOMY_ONLY";default:return "UNKNOWN";} }
struct FusedIdentityV9 { bool publish=false; int pixel_x=-1,pixel_y=-1; std::uint64_t identity_id=0; std::string confidence="LOW"; FusionModeV9 mode=FusionModeV9::Unknown; std::string reason="anatomy-not-ready"; double agreement_distance_px=0.0; };

inline bool mask_near_fullres_v9(const std::vector<uint8_t>& mask,int mask_width,int mask_height,int full_x,int full_y,int depth_scale,int radius=2){
    if(mask.empty()||mask_width<=0||mask_height<=0||depth_scale<=0)return false; const int gx=full_x/depth_scale,gy=full_y/depth_scale;
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){const int x=gx+dx,y=gy+dy;if(x<0||x>=mask_width||y<0||y>=mask_height)continue;if(mask[static_cast<size_t>(y)*mask_width+x])return true;} return false;
}

inline FusedIdentityV9 fuse_identity_v9(const IdentityObservationV8& go,const IdentityDecisionV8& gd,const AnatomyDecisionV9& ad,const std::vector<uint8_t>& mask,int mw,int mh,int scale){
    FusedIdentityV9 out;
    if(ad.explicit_reject){out.reason="anatomy-reject";return out;} if(ad.stale){out.reason="anatomy-stale";return out;} if(ad.jump_rejected){out.reason="anatomy-jump-reject";return out;} if(!ad.publish){out.reason="anatomy-not-locked";return out;}
    if(!go.hand_valid||!go.palm_valid){out.reason="palm-invalid";return out;} if(gd.palm_rejected){out.reason="palm-temporal-reject";return out;} if(gd.jump_rejected){out.reason="geometry-jump-reject";return out;}
    if(!mask_near_fullres_v9(mask,mw,mh,ad.tip_x,ad.tip_y,scale,2)){out.reason="anatomy-tip-outside-current-silhouette";return out;}
    if(gd.publish&&gd.has_candidate){const int gx=gd.tip_gx*scale+1,gy=gd.tip_gy*scale+1;out.agreement_distance_px=std::hypot(static_cast<double>(gx-ad.tip_x),static_cast<double>(gy-ad.tip_y));const double gate=std::max(22.0,go.palm_radius*scale*1.15);if(out.agreement_distance_px>gate){out.reason="geometry-anatomy-disagree";return out;}out.publish=true;out.pixel_x=ad.tip_x;out.pixel_y=ad.tip_y;out.identity_id=gd.branch_id;out.mode=FusionModeV9::GeometryAnatomyAgree;out.confidence=(gd.confidence=="HIGH"&&ad.confidence=="HIGH")?"HIGH":"MEDIUM";out.reason="geometry-anatomy-agree";return out;}
    out.publish=true;out.pixel_x=ad.tip_x;out.pixel_y=ad.tip_y;out.identity_id=0x8000000000000000ULL|ad.anatomy_id;out.mode=FusionModeV9::AnatomyOnly;out.confidence=ad.confidence;out.reason="anatomy-only-with-valid-v8-palm";return out;
}

inline bool final_identity_stereo_gate_v9(const std::string& i,const std::string& s){return confidence_passes_v8(i)&&confidence_passes_v8(s);}

} // namespace touchplus::tracking
