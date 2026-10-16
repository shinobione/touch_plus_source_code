#pragma once

// Dedicated hardware surface calibrator: enforce the ACCEPTED dominant-plane
// fitter before exposing Phase 2A C/F/R/U/H hooks. Merely force-including
// depth_surface_frame.h would silently pull in the obsolete all-points fitter.
//
// This target deliberately excludes all tracking, anatomy, hybrid promotion
// and contact/OS output modules.
#include "surface_frame_robust.h"
#include "depth_surface_frame.h"
