#include "recovery_hand_resolver.h"

#include <algorithm>
#include <cmath>

#include "mat_functions.h"

using namespace cv;
using namespace std;

namespace
{
    bool valid_small_point(const Point& p)
    {
        return p.x >= 0 && p.y >= 0 && p.x < 160 && p.y < 120;
    }

    bool valid_candidate(const Point2f& p)
    {
        return p.x >= 0.0f && p.y >= 0.0f;
    }

    float point_distance(const Point2f& a, const Point2f& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    struct EyeGateResult
    {
        bool valid = false;
        float shift_px = -1.0f;
        float forward_px = 0.0f;
        float lateral_px = 0.0f;
        float radial_delta_px = 0.0f;
        float direction_cos = -2.0f;
        string reason = "NOT_RUN";
    };

    EyeGateResult evaluate_eye_candidate(
        const Point& coarse_small,
        const Point& palm_small,
        const Point2f& candidate)
    {
        EyeGateResult result;

        if (!valid_small_point(coarse_small))
        {
            result.reason = "NO_COARSE_INDEX";
            return result;
        }

        if (!valid_small_point(palm_small))
        {
            result.reason = "NO_PALM";
            return result;
        }

        if (!valid_candidate(candidate))
        {
            result.reason = "NO_REFINED_CANDIDATE";
            return result;
        }

        const Point2f coarse(
            static_cast<float>(coarse_small.x * 4),
            static_cast<float>(coarse_small.y * 4));
        const Point2f palm(
            static_cast<float>(palm_small.x * 4),
            static_cast<float>(palm_small.y * 4));

        const Point2f palm_to_coarse = coarse - palm;
        const float palm_to_coarse_len = point_distance(coarse, palm);
        if (palm_to_coarse_len < 20.0f)
        {
            result.reason = "PALM_INDEX_VECTOR_TOO_SHORT";
            return result;
        }

        const Point2f delta = candidate - coarse;
        result.shift_px = point_distance(candidate, coarse);

        // The local search window is 50x20 around the coarse point. A shift
        // larger than ~30 px cannot be a faithful refinement of that coarse
        // landmark and is treated as a wrong edge/blob capture.
        if (result.shift_px > 30.0f)
        {
            result.reason = "SHIFT_TOO_LARGE";
            return result;
        }

        const Point2f outward(
            palm_to_coarse.x / palm_to_coarse_len,
            palm_to_coarse.y / palm_to_coarse_len);

        result.forward_px = delta.x * outward.x + delta.y * outward.y;
        result.lateral_px = std::abs(delta.x * outward.y - delta.y * outward.x);

        const float candidate_radius = point_distance(candidate, palm);
        result.radial_delta_px = candidate_radius - palm_to_coarse_len;

        if (result.shift_px > 0.001f)
            result.direction_cos = result.forward_px / result.shift_px;
        else
            result.direction_cos = 1.0f;

        // A refined fingertip may move a couple pixels sideways/backward due to
        // blur and integer coarse coordinates, but it must not meaningfully run
        // back toward the palm.
        if (result.forward_px < -2.0f || result.radial_delta_px < -2.0f)
        {
            result.reason = "MOVED_TOWARD_PALM";
            return result;
        }

        // For a non-trivial correction, require at least weak agreement with
        // the anatomical palm->coarse-index ray. This rejects the common open-
        // hand failure where the local blob centroid jumps toward another digit.
        if (result.shift_px >= 4.0f && result.direction_cos < 0.15f)
        {
            result.reason = "DIRECTION_MISMATCH";
            return result;
        }

        // The historical resolver's 50x20 crop is deliberately local. Large
        // tangential motion is more likely a neighboring finger/edge than the
        // same fingertip refined at full resolution.
        if (result.lateral_px > 14.0f)
        {
            result.reason = "LATERAL_DRIFT";
            return result;
        }

        result.valid = true;
        result.reason = "OK";
        return result;
    }
}

void RecoveryHandResolver::compute(
    MonoProcessorNew& mono_processor0,
    MonoProcessorNew& mono_processor1,
    MotionProcessorNew& motion_processor0,
    MotionProcessorNew& motion_processor1,
    const Mat& image0,
    const Mat& image1)
{
    pt_candidate_index0 = Point2f(-1.0f, -1.0f);
    pt_candidate_index1 = Point2f(-1.0f, -1.0f);
    pt_precise_index0 = Point2f(-1.0f, -1.0f);
    pt_precise_index1 = Point2f(-1.0f, -1.0f);

    shift_index0_px = -1.0f;
    shift_index1_px = -1.0f;
    forward_index0_px = 0.0f;
    forward_index1_px = 0.0f;
    lateral_index0_px = 0.0f;
    lateral_index1_px = 0.0f;
    radial_delta_index0_px = 0.0f;
    radial_delta_index1_px = 0.0f;
    direction_cos_index0 = -2.0f;
    direction_cos_index1 = -2.0f;

    eye0_valid = false;
    eye1_valid = false;
    pair_valid = false;
    gate_reason = "NOT_RUN";

    if (valid_small_point(mono_processor0.pt_index))
    {
        pt_candidate_index0 = increase_resolution_raw(
            mono_processor0.pt_index,
            image0,
            motion_processor0.image_background_static,
            motion_processor0.diff_threshold,
            motion_processor0.gray_threshold,
            blob_detector0,
            "recovery_hand_resolver_left");
    }

    if (valid_small_point(mono_processor1.pt_index))
    {
        pt_candidate_index1 = increase_resolution_raw(
            mono_processor1.pt_index,
            image1,
            motion_processor1.image_background_static,
            motion_processor1.diff_threshold,
            motion_processor1.gray_threshold,
            blob_detector1,
            "recovery_hand_resolver_right");
    }

    const EyeGateResult gate0 = evaluate_eye_candidate(
        mono_processor0.pt_index,
        mono_processor0.pt_palm,
        pt_candidate_index0);
    const EyeGateResult gate1 = evaluate_eye_candidate(
        mono_processor1.pt_index,
        mono_processor1.pt_palm,
        pt_candidate_index1);

    eye0_valid = gate0.valid;
    eye1_valid = gate1.valid;

    shift_index0_px = gate0.shift_px;
    shift_index1_px = gate1.shift_px;
    forward_index0_px = gate0.forward_px;
    forward_index1_px = gate1.forward_px;
    lateral_index0_px = gate0.lateral_px;
    lateral_index1_px = gate1.lateral_px;
    radial_delta_index0_px = gate0.radial_delta_px;
    radial_delta_index1_px = gate1.radial_delta_px;
    direction_cos_index0 = gate0.direction_cos;
    direction_cos_index1 = gate1.direction_cos;

    if (!eye0_valid)
    {
        gate_reason = "LEFT_" + gate0.reason;
        return;
    }

    if (!eye1_valid)
    {
        gate_reason = "RIGHT_" + gate1.reason;
        return;
    }

    // Raw-eye stereo coherence: do not compare absolute LEFT/RIGHT coordinates
    // because the images are intentionally unrectified here. Compare only the
    // local correction each eye made relative to its own coarse anatomy.
    const float shift_delta = std::abs(shift_index0_px - shift_index1_px);
    if (shift_delta > 14.0f)
    {
        gate_reason = "PAIR_SHIFT_MISMATCH";
        return;
    }

    const float radial_delta_difference = std::abs(
        radial_delta_index0_px - radial_delta_index1_px);
    if (radial_delta_difference > 12.0f)
    {
        gate_reason = "PAIR_DISTAL_PROGRESS_MISMATCH";
        return;
    }

    // If both eyes made a meaningful correction, their alignment with their
    // own palm->index rays should not disagree wildly.
    if (shift_index0_px >= 4.0f && shift_index1_px >= 4.0f &&
        std::abs(direction_cos_index0 - direction_cos_index1) > 0.70f)
    {
        gate_reason = "PAIR_DIRECTION_MISMATCH";
        return;
    }

    pt_precise_index0 = pt_candidate_index0;
    pt_precise_index1 = pt_candidate_index1;
    pair_valid = true;
    gate_reason = "ACCEPT";
}

Point2f RecoveryHandResolver::increase_resolution_raw(
    const Point& pt_in,
    const Mat& image_in,
    const Mat& image_background_in,
    unsigned char diff_threshold,
    unsigned char gray_threshold,
    BlobDetectorNew& blob_detector,
    const std::string& normalization_key)
{
    if (image_in.empty() || image_background_in.empty())
        return Point2f(-1.0f, -1.0f);

    const Point pt_large = pt_in * 4;

    const int x0 = std::max(0, pt_large.x - window_width_half);
    const int y0 = std::max(0, pt_large.y - window_height_half);
    const int x1 = std::min(image_in.cols, pt_large.x + window_width_half);
    const int y1 = std::min(image_in.rows, pt_large.y + window_height_half);

    if (x1 - x0 < 4 || y1 - y0 < 4)
        return Point2f(-1.0f, -1.0f);

    const Rect crop_rect(x0, y0, x1 - x0, y1 - y0);

    // Current full-resolution eye crop. Clone so the diagnostic image itself is
    // never modified by the historical in-place blur pattern.
    Mat image_cropped = image_in(crop_rect).clone();
    GaussianBlur(image_cropped, image_cropped, Size(21, 21), 0, 0);

    Mat image_cropped_preprocessed;
    compute_channel_diff_image(
        image_cropped,
        image_cropped_preprocessed,
        true,
        normalization_key);

    if (image_cropped_preprocessed.empty())
        return Point2f(-1.0f, -1.0f);

    // The historical background is 160x120. Upscale it to the native eye once
    // and crop in the exact same RAW coordinate system as the current image.
    Mat background_full;
    resize(image_background_in, background_full, image_in.size(), 0, 0, INTER_LINEAR);
    Mat image_background_cropped = background_full(crop_rect).clone();

    unsigned char gray_max = 0;
    for (int y = 0; y < image_background_cropped.rows; ++y)
    {
        const unsigned char* row = image_background_cropped.ptr<unsigned char>(y);
        for (int x = 0; x < image_background_cropped.cols; ++x)
        {
            const unsigned char gray = row[x];
            if (gray < 255 && gray > gray_max)
                gray_max = gray;
        }
    }

    for (int y = 0; y < image_background_cropped.rows; ++y)
    {
        unsigned char* row = image_background_cropped.ptr<unsigned char>(y);
        for (int x = 0; x < image_background_cropped.cols; ++x)
        {
            if (row[x] == 255)
                row[x] = gray_max;
        }
    }

    if (image_background_cropped.size() != image_cropped_preprocessed.size())
        return Point2f(-1.0f, -1.0f);

    Mat image_subtraction = Mat::zeros(
        image_cropped_preprocessed.rows,
        image_cropped_preprocessed.cols,
        CV_8UC1);

    for (int y = 0; y < image_cropped_preprocessed.rows; ++y)
    {
        const unsigned char* current_row = image_cropped_preprocessed.ptr<unsigned char>(y);
        const unsigned char* background_row = image_background_cropped.ptr<unsigned char>(y);
        unsigned char* subtraction_row = image_subtraction.ptr<unsigned char>(y);

        for (int x = 0; x < image_cropped_preprocessed.cols; ++x)
        {
            if (current_row[x] > gray_threshold)
            {
                subtraction_row[x] = static_cast<unsigned char>(
                    std::abs(static_cast<int>(current_row[x]) - static_cast<int>(background_row[x])));
            }
        }
    }

    threshold(image_subtraction, image_subtraction, diff_threshold, 254, THRESH_BINARY);
    blob_detector.compute(
        image_subtraction,
        254,
        0,
        image_subtraction.cols,
        0,
        image_subtraction.rows,
        true);

    if (blob_detector.blobs == NULL || blob_detector.blobs->empty() ||
        blob_detector.blob_max_size == NULL || blob_detector.blob_max_size->data.empty())
    {
        return Point2f(-1.0f, -1.0f);
    }

    double x_mean = 0.0;
    double y_mean = 0.0;
    size_t count = 0;

    for (const Point& pt : blob_detector.blob_max_size->data)
    {
        x_mean += pt.x;
        y_mean += pt.y;
        ++count;
    }

    if (count == 0)
        return Point2f(-1.0f, -1.0f);

    x_mean /= static_cast<double>(count);
    y_mean /= static_cast<double>(count);

    return Point2f(
        static_cast<float>(x_mean + x0),
        static_cast<float>(y_mean + y0));
}
