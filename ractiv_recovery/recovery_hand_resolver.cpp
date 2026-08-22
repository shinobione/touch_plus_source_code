#include "recovery_hand_resolver.h"

#include <algorithm>
#include <cmath>

#include "mat_functions.h"

using namespace cv;
using namespace std;

namespace
{
    bool valid_coarse_index(const Point& p)
    {
        return p.x >= 0 && p.y >= 0 && p.x < 160 && p.y < 120;
    }

    float point_distance(const Point2f& a, const Point2f& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
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
    pt_precise_index0 = Point2f(-1.0f, -1.0f);
    pt_precise_index1 = Point2f(-1.0f, -1.0f);
    shift_index0_px = -1.0f;
    shift_index1_px = -1.0f;
    pair_valid = false;

    if (valid_coarse_index(mono_processor0.pt_index))
    {
        pt_precise_index0 = increase_resolution_raw(
            mono_processor0.pt_index,
            image0,
            motion_processor0.image_background_static,
            motion_processor0.diff_threshold,
            motion_processor0.gray_threshold,
            blob_detector0,
            "recovery_hand_resolver_left");
    }

    if (valid_coarse_index(mono_processor1.pt_index))
    {
        pt_precise_index1 = increase_resolution_raw(
            mono_processor1.pt_index,
            image1,
            motion_processor1.image_background_static,
            motion_processor1.diff_threshold,
            motion_processor1.gray_threshold,
            blob_detector1,
            "recovery_hand_resolver_right");
    }

    if (pt_precise_index0.x >= 0.0f)
    {
        const Point2f coarse0(
            static_cast<float>(mono_processor0.pt_index.x * 4),
            static_cast<float>(mono_processor0.pt_index.y * 4));
        shift_index0_px = point_distance(pt_precise_index0, coarse0);
    }

    if (pt_precise_index1.x >= 0.0f)
    {
        const Point2f coarse1(
            static_cast<float>(mono_processor1.pt_index.x * 4),
            static_cast<float>(mono_processor1.pt_index.y * 4));
        shift_index1_px = point_distance(pt_precise_index1, coarse1);
    }

    // Keep the historical pair semantics for any later stereo experiment:
    // both eyes must have a refined index. The viewer may still show a valid
    // refinement in only one eye for diagnostic purposes.
    pair_valid = pt_precise_index0.x >= 0.0f && pt_precise_index1.x >= 0.0f;
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
