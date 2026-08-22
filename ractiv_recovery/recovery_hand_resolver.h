#pragma once

#include <opencv2/opencv.hpp>
#include <string>

#include "blob_detector_new.h"
#include "mono_processor_new.h"
#include "motion_processor_new.h"

// Recovery-only full-resolution refinement of the July MonoProcessorNew index.
//
// This deliberately does NOT use the historical Reprojector. The original
// HandResolver coupled its local 50x20 refinement window to rectification data
// downloaded from Ractiv's dead calibration CDN. For R1 anatomy evaluation we
// need only a per-eye RAW 640x480 refinement to answer one question: does the
// historical coarse pt_index become a visibly better distal fingertip when the
// original local background-subtraction idea is applied at full resolution?
//
// A recovery-only plausibility gate now separates the raw local-refinement
// candidate from an accepted fingertip. The gate uses only raw-eye anatomy:
// coarse-index distance, palm->index direction, distal progress, lateral drift,
// and agreement between LEFT/RIGHT refinement deltas. It does NOT perform
// stereo depth or any output mapping.
//
// No stereo depth, pointer mapping, contact semantics, IPC, UDP or OS output is
// performed here.
class RecoveryHandResolver
{
public:
    // Raw local 50x20 refinement candidates. These are diagnostic only and may
    // be rejected by the coherence gate.
    cv::Point2f pt_candidate_index0 = cv::Point2f(-1.0f, -1.0f);
    cv::Point2f pt_candidate_index1 = cv::Point2f(-1.0f, -1.0f);

    // Accepted refined pair. These stay invalid unless BOTH eyes pass their
    // anatomy gate and the pair passes LEFT/RIGHT coherence.
    cv::Point2f pt_precise_index0 = cv::Point2f(-1.0f, -1.0f);
    cv::Point2f pt_precise_index1 = cv::Point2f(-1.0f, -1.0f);

    float shift_index0_px = -1.0f;
    float shift_index1_px = -1.0f;
    float forward_index0_px = 0.0f;
    float forward_index1_px = 0.0f;
    float lateral_index0_px = 0.0f;
    float lateral_index1_px = 0.0f;
    float radial_delta_index0_px = 0.0f;
    float radial_delta_index1_px = 0.0f;
    float direction_cos_index0 = -2.0f;
    float direction_cos_index1 = -2.0f;

    bool eye0_valid = false;
    bool eye1_valid = false;
    bool pair_valid = false;
    std::string gate_reason = "NOT_RUN";

    void compute(
        MonoProcessorNew& mono_processor0,
        MonoProcessorNew& mono_processor1,
        MotionProcessorNew& motion_processor0,
        MotionProcessorNew& motion_processor1,
        const cv::Mat& image0,
        const cv::Mat& image1);

private:
    static const int window_width = 50;
    static const int window_height = 20;
    static const int window_width_half = window_width / 2;
    static const int window_height_half = window_height / 2;

    BlobDetectorNew blob_detector0;
    BlobDetectorNew blob_detector1;

    cv::Point2f increase_resolution_raw(
        const cv::Point& pt_in,
        const cv::Mat& image_in,
        const cv::Mat& image_background_in,
        unsigned char diff_threshold,
        unsigned char gray_threshold,
        BlobDetectorNew& blob_detector,
        const std::string& normalization_key);
};
