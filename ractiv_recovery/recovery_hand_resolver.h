#pragma once

#include <opencv2/opencv.hpp>

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
// No stereo depth, pointer mapping, contact semantics, IPC, UDP or OS output is
// performed here.
class RecoveryHandResolver
{
public:
    cv::Point2f pt_precise_index0 = cv::Point2f(-1.0f, -1.0f);
    cv::Point2f pt_precise_index1 = cv::Point2f(-1.0f, -1.0f);

    float shift_index0_px = -1.0f;
    float shift_index1_px = -1.0f;

    bool pair_valid = false;

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
        BlobDetectorNew& blob_detector);
};
