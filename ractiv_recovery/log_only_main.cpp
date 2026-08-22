// Touch+ Ractiv Recovery - minimal LOG_ONLY entrypoint
//
// Recovery-only wrapper around the July 2015 integrated vision lineage.
// Historical sources remain unchanged/auditable.
//
// Boundary: Camera -> MotionProcessorNew -> ForegroundExtractorNew ->
// HandSplitterNew -> MonoProcessorNew -> RecoveryHandResolver -> PoseEstimator
// telemetry/viewer only.
//
// NO PointerMapper, NO win_cursor_plus, NO UDP, NO OS input injection.

#include <Windows.h>
#include <opencv2/opencv.hpp>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "globals.h"
#include "Camera.h"
#include "camera_initializer_new.h"
#include "mat_functions.h"
#include "motion_processor_new.h"
#include "foreground_extractor_new.h"
#include "hand_splitter_new.h"
#include "mono_processor_new.h"
#include "pose_estimator.h"
#include "filesystem.h"
#include "recovery_hand_resolver.h"

using namespace cv;
using namespace std;

namespace
{
    mutex g_frame_mutex;
    Mat g_latest_frame;
    atomic<bool> g_frame_ready(false);
    atomic<bool> g_running(true);
    atomic<unsigned long long> g_callback_frames(0);

    const char* kDiagnosticWindow = "Touch+ Ractiv Recovery - LEFT | RIGHT";

    void on_frame(Mat& image_in)
    {
        if (image_in.empty())
            return;

        g_callback_frames.fetch_add(1, memory_order_relaxed);

        lock_guard<mutex> lock(g_frame_mutex);
        image_in.copyTo(g_latest_frame);
        g_frame_ready.store(true, memory_order_release);
    }

    BOOL WINAPI console_handler(DWORD signal)
    {
        if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT)
        {
            g_running.store(false);
            return TRUE;
        }
        return FALSE;
    }

    string point_text(const Point& p)
    {
        return "(" + to_string(p.x) + "," + to_string(p.y) + ")";
    }

    string point2f_text(const Point2f& p)
    {
        if (p.x < 0.0f || p.y < 0.0f)
            return "(-1,-1)";

        ostringstream ss;
        ss << fixed << setprecision(1) << "(" << p.x << "," << p.y << ")";
        return ss.str();
    }

    bool valid_small_point(const Point& p)
    {
        return p.x >= 0 && p.y >= 0 && p.x < 160 && p.y < 120;
    }

    bool valid_full_point(const Point2f& p, const Mat& image)
    {
        return p.x >= 0.0f && p.y >= 0.0f && p.x < image.cols && p.y < image.rows;
    }

    Point small_to_full(const Point& p)
    {
        return Point(p.x * 4, p.y * 4);
    }

    Mat to_bgr(const Mat& image)
    {
        if (image.channels() == 3)
            return image.clone();

        Mat bgr;
        if (image.channels() == 1)
            cvtColor(image, bgr, CV_GRAY2BGR);
        else if (image.channels() == 4)
            cvtColor(image, bgr, CV_BGRA2BGR);
        else
            image.copyTo(bgr);
        return bgr;
    }

    void draw_landmark(Mat& image, const Point& small_point, const Scalar& color, const string& label)
    {
        if (!valid_small_point(small_point))
            return;

        const Point p = small_to_full(small_point);
        circle(image, p, 9, color, 2, CV_AA);
        line(image, Point(p.x - 12, p.y), Point(p.x + 12, p.y), color, 1, CV_AA);
        line(image, Point(p.x, p.y - 12), Point(p.x, p.y + 12), color, 1, CV_AA);

        const int text_x = std::max(4, std::min(image.cols - 190, p.x + 12));
        const int text_y = std::max(18, std::min(image.rows - 6, p.y - 12));
        putText(image, label + " " + point_text(small_point), Point(text_x, text_y),
                FONT_HERSHEY_SIMPLEX, 0.42, color, 1, CV_AA);
    }

    void draw_refined_landmark(Mat& image, const Point2f& refined_point, const Scalar& color, const string& label)
    {
        if (!valid_full_point(refined_point, image))
            return;

        const Point p(cvRound(refined_point.x), cvRound(refined_point.y));
        line(image, Point(p.x - 11, p.y - 11), Point(p.x + 11, p.y + 11), color, 2, CV_AA);
        line(image, Point(p.x - 11, p.y + 11), Point(p.x + 11, p.y - 11), color, 2, CV_AA);
        circle(image, p, 4, color, 1, CV_AA);

        const int text_x = std::max(4, std::min(image.cols - 210, p.x + 12));
        const int text_y = std::max(18, std::min(image.rows - 6, p.y + 18));
        putText(image, label + " " + point2f_text(refined_point), Point(text_x, text_y),
                FONT_HERSHEY_SIMPLEX, 0.42, color, 1, CV_AA);
    }

    void pump_diagnostic_viewer(
        const bool enabled,
        const Mat& left,
        const Mat& right,
        const string& stage,
        const MonoProcessorNew* mono_left,
        const MonoProcessorNew* mono_right,
        const RecoveryHandResolver* resolver)
    {
        if (!enabled || left.empty() || right.empty())
            return;

        Mat left_view = to_bgr(left);
        Mat right_view = to_bgr(right);

        putText(left_view, "LEFT", Point(12, 28), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255, 255, 255), 2, CV_AA);
        putText(right_view, "RIGHT", Point(12, 28), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255, 255, 255), 2, CV_AA);

        if (mono_left != NULL && mono_right != NULL)
        {
            // PALM = cyan, coarse INDEX = magenta, THUMB = yellow.
            draw_landmark(left_view, mono_left->pt_palm, Scalar(255, 255, 0), "PALM");
            draw_landmark(left_view, mono_left->pt_index, Scalar(255, 0, 255), "COARSE INDEX");
            draw_landmark(left_view, mono_left->pt_thumb, Scalar(0, 255, 255), "THUMB");

            draw_landmark(right_view, mono_right->pt_palm, Scalar(255, 255, 0), "PALM");
            draw_landmark(right_view, mono_right->pt_index, Scalar(255, 0, 255), "COARSE INDEX");
            draw_landmark(right_view, mono_right->pt_thumb, Scalar(0, 255, 255), "THUMB");
        }

        if (resolver != NULL)
        {
            // Recovery full-resolution refinement = green X. This is raw-eye
            // anatomy only; no historical Reprojector or metric stereo is used.
            draw_refined_landmark(left_view, resolver->pt_precise_index0, Scalar(0, 255, 0), "REFINED INDEX");
            draw_refined_landmark(right_view, resolver->pt_precise_index1, Scalar(0, 255, 0), "REFINED INDEX");
        }

        Mat stereo(left_view.rows, left_view.cols + right_view.cols, CV_8UC3);
        left_view.copyTo(stereo(Rect(0, 0, left_view.cols, left_view.rows)));
        right_view.copyTo(stereo(Rect(left_view.cols, 0, right_view.cols, right_view.rows)));

        const string pose_text = pose_name.empty() ? "<none>" : pose_name;
        putText(stereo, "stage=" + stage + " | pose=" + pose_text + " | magenta=coarse green=refined | Q/ESC quits",
                Point(12, stereo.rows - 14), FONT_HERSHEY_SIMPLEX, 0.52, Scalar(255, 255, 255), 1, CV_AA);

        imshow(kDiagnosticWindow, stereo);
        const int key = waitKey(1) & 0xff;
        if (key == 27 || key == 'q' || key == 'Q')
            g_running.store(false);
    }

    void init_paths_recovery()
    {
        char buffer[MAX_PATH] = {0};
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        string path(buffer);
        string::size_type pos = path.find_last_of("\\/");
        executable_path = (pos == string::npos) ? "." : path.substr(0, pos);

        data_path = executable_path + "\\userdata";
        pose_database_path = executable_path + "\\database";

        if (!directory_exists(data_path))
            create_directory(data_path);

        mode = "surface";
        enable_imshow = false;
    }
}

int main(int argc, char** argv)
{
    bool diagnostic_viewer = false;
    for (int i = 1; i < argc; ++i)
    {
        const string arg = argv[i];
        if (arg == "--viewer")
            diagnostic_viewer = true;
    }

    SetConsoleCtrlHandler(console_handler, TRUE);
    init_paths_recovery();

    cout << "============================================================" << endl;
    cout << " Touch+ Ractiv Recovery - R1 RAW-EYE HANDRESOLVER DIAGNOSTIC" << endl;
    cout << " historical algorithm lineage: July 2015 integrated core" << endl;
    cout << " OS_INJECTION=DISABLED" << endl;
    cout << " POINTER_MAPPER=DISABLED" << endl;
    cout << " UDP_CURSOR_OUTPUT=DISABLED" << endl;
    cout << " HISTORICAL_REPROJECTOR=DISABLED" << endl;
    cout << " RAW_EYE_INDEX_REFINER=" << (diagnostic_viewer ? "ENABLED" : "DISABLED") << endl;
    cout << " DIAGNOSTIC_VIEWER=" << (diagnostic_viewer ? "ENABLED" : "DISABLED") << endl;
    cout << "============================================================" << endl;

    if (diagnostic_viewer)
    {
        namedWindow(kDiagnosticWindow, CV_WINDOW_AUTOSIZE);
        cout << "[RACTIV_RECOVERY] viewer: PALM=cyan COARSE_INDEX=magenta REFINED_INDEX=green THUMB=yellow; Q/ESC exits" << endl;
    }

    MotionProcessorNew motion0;
    MotionProcessorNew motion1;
    ForegroundExtractorNew foreground0;
    ForegroundExtractorNew foreground1;
    HandSplitterNew hand0;
    HandSplitterNew hand1;
    MonoProcessorNew mono0;
    MonoProcessorNew mono1;
    RecoveryHandResolver recovery_hand_resolver;
    PoseEstimator pose_estimator;

    // Deliberately leaked at process exit: the historical Camera destructor
    // contains invalid-free-like legacy behavior. Windows reclaims process memory.
    Camera* camera = new Camera(true, 1280, 480, on_frame);

    if (camera->device_not_detected)
    {
        cerr << "[RACTIV_RECOVERY] FAIL: Touch+ camera not detected" << endl;
        return 2;
    }

    cout << "[RACTIV_RECOVERY] waiting for first 1280x480 stereo frame..." << endl;

    auto wait_start = chrono::steady_clock::now();
    while (g_running.load() && !g_frame_ready.load(memory_order_acquire))
    {
        if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - wait_start).count() >= 12)
        {
            cerr << "[RACTIV_RECOVERY] FAIL: no frame received within 12 seconds" << endl;
            return 3;
        }
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    serial_number = camera->getSerialNumber();
    data_path_current_module = data_path + "\\" + serial_number;

    cout << "[RACTIV_RECOVERY] serial=" << serial_number << endl;
    if (serial_number.size() != 10 || serial_number.substr(0, 4) != "0101")
        cout << "[RACTIV_RECOVERY] WARN: serial does not match historical 0101xxxxxxxx form" << endl;

    // Keep the recovered fixed sensor initializer, but deliberately skip the
    // fragile historical CameraInitializerNew::adjust_exposure() routine.
    CameraInitializerNew::init(camera);
    cout << "[RACTIV_RECOVERY] sensor initializer applied: AE/AWB off, LEDs on, exposure=15ms, preset1" << endl;

    pose_estimator.init();

    // The original July main did NOT feed MotionProcessorNew immediately after
    // camera creation. It repeatedly ran adjust_exposure() first. On the first
    // frame that finally reached MotionProcessorNew, channel-diff normalization
    // was still disabled; normalization became active on subsequent frames.
    //
    // Our recovery runtime intentionally skips adjust_exposure(), so reproduce
    // the useful bootstrap semantics safely and deterministically instead:
    //   1) discard any frame buffered before the fixed sensor init;
    //   2) allow fresh post-init frames to settle;
    //   3) seed MotionProcessorNew with one unnormalized frame;
    //   4) normalize all subsequent frames.
    {
        lock_guard<mutex> lock(g_frame_mutex);
        g_latest_frame.release();
        g_frame_ready.store(false, memory_order_release);
    }

    const unsigned int sensor_settle_frames_target = 30;
    unsigned int sensor_settle_frames_seen = 0;
    bool normalized_preprocess = false;
    bool motion_background_seeded = false;

    cout << "[RACTIV_RECOVERY] bootstrap: discarded pre-init buffered frame; "
         << "settle_frames=" << sensor_settle_frames_target
         << "; first motion frame normalization=OFF" << endl;

    unsigned long long frame_num = 0;
    unsigned long long stereo_shape_failures = 0;
    unsigned long long motion_passes = 0;
    unsigned long long foreground_passes = 0;
    unsigned long long hand_passes = 0;
    unsigned long long mono_passes = 0;
    unsigned long long refinement_attempts = 0;
    unsigned long long refinement_pairs = 0;
    unsigned long long opencv_exceptions = 0;

    string last_stage = "WAITING_FOR_FRESH_POST_INIT_FRAME";
    auto report_start = chrono::steady_clock::now();
    unsigned long long report_frames = 0;
    unsigned long long last_report_callbacks = g_callback_frames.load(memory_order_relaxed);

    while (g_running.load())
    {
        // Heartbeat is intentionally independent of pipeline success.
        const auto heartbeat_now = chrono::steady_clock::now();
        const double heartbeat_seconds = chrono::duration<double>(heartbeat_now - report_start).count();
        if (heartbeat_seconds >= 2.0)
        {
            const unsigned long long callbacks_now = g_callback_frames.load(memory_order_relaxed);
            const unsigned long long callback_delta = callbacks_now - last_report_callbacks;
            const double callback_fps = callback_delta / heartbeat_seconds;
            const double processed_fps = report_frames / heartbeat_seconds;

            cout << "[RACTIV_RECOVERY] heartbeat"
                 << " callback_fps=" << callback_fps
                 << " processed_fps=" << processed_fps
                 << " callbacks=" << callbacks_now
                 << " frames=" << frame_num
                 << " frame_ready=" << (g_frame_ready.load(memory_order_acquire) ? 1 : 0)
                 << " last_stage=" << last_stage
                 << " settle=" << sensor_settle_frames_seen << "/" << sensor_settle_frames_target
                 << " normalized=" << (normalized_preprocess ? 1 : 0)
                 << " motion_seeded=" << (motion_background_seeded ? 1 : 0)
                 << " motion_pass=" << motion_passes
                 << " foreground_pass=" << foreground_passes
                 << " hand_pass=" << hand_passes
                 << " mono_pass=" << mono_passes
                 << " refine_attempts=" << refinement_attempts
                 << " refined_pairs=" << refinement_pairs
                 << " opencv_exceptions=" << opencv_exceptions
                 << " OS_INJECTION=DISABLED" << endl;

            report_start = heartbeat_now;
            report_frames = 0;
            last_report_callbacks = callbacks_now;
        }

        Mat frame;
        {
            lock_guard<mutex> lock(g_frame_mutex);
            if (g_frame_ready.load(memory_order_acquire))
            {
                g_latest_frame.copyTo(frame);
                g_frame_ready.store(false, memory_order_release);
            }
        }

        if (frame.empty())
        {
            last_stage = "WAITING_FOR_FRAME";
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }

        ++frame_num;
        ++report_frames;

        if (frame.cols != 1280 || frame.rows != 480)
        {
            last_stage = "STEREO_SHAPE_REJECT";
            ++stereo_shape_failures;
            if ((stereo_shape_failures % 30) == 1)
                cerr << "[RACTIV_RECOVERY] WARN: unexpected frame shape " << frame.cols << "x" << frame.rows << endl;
            continue;
        }

        if (sensor_settle_frames_seen < sensor_settle_frames_target)
        {
            ++sensor_settle_frames_seen;
            last_stage = "SENSOR_SETTLE";
            if (sensor_settle_frames_seen == sensor_settle_frames_target)
                cout << "[RACTIV_RECOVERY] bootstrap: sensor settle complete; next frame seeds motion background with normalization OFF" << endl;
            continue;
        }

        const char* stage = "PREPROCESS";

        try
        {
            stage = "PREPROCESS_FLIP";
            last_stage = stage;
            Mat flipped;
            flip(frame, flipped, 0); // exact historical vertical flip

            Mat image0 = flipped(Rect(0, 0, 640, 480));
            Mat image1 = flipped(Rect(640, 0, 640, 480));

            stage = "PREPROCESS_RESIZE";
            last_stage = stage;
            Mat small0;
            Mat small1;
            resize(image0, small0, Size(160, 120), 0, 0, INTER_LINEAR);
            resize(image1, small1, Size(160, 120), 0, 0, INTER_LINEAR);

            stage = "PREPROCESS_CHANNEL_DIFF";
            last_stage = stage;
            Mat prep0;
            Mat prep1;
            compute_channel_diff_image(small0, prep0, normalized_preprocess, "recovery0");
            compute_channel_diff_image(small1, prep1, normalized_preprocess, "recovery1");

            stage = "PREPROCESS_BLUR";
            last_stage = stage;
            Mat smooth0;
            Mat smooth1;
            GaussianBlur(prep0, smooth0, Size(1, 19), 0, 0);
            GaussianBlur(prep1, smooth1, Size(1, 19), 0, 0);

            const bool bootstrap_motion_frame = !motion_background_seeded;

            stage = "MOTION_LEFT";
            last_stage = stage;
            const bool motion_ok0 = motion0.compute(smooth0, "0", false);

            stage = "MOTION_RIGHT";
            last_stage = stage;
            const bool motion_ok1 = motion1.compute(smooth1, "1", false);

            if (bootstrap_motion_frame)
            {
                motion_background_seeded = true;
                normalized_preprocess = true;
                last_stage = "MOTION_BACKGROUND_SEEDED";
                cout << "[RACTIV_RECOVERY] bootstrap: motion background seeded from unnormalized stereo frame; normalization=ON" << endl;
            }

            if (!(motion_ok0 && motion_ok1))
            {
                if (!bootstrap_motion_frame)
                    last_stage = "MOTION_REJECT";
                pump_diagnostic_viewer(diagnostic_viewer, image0, image1, last_stage, NULL, NULL, NULL);
                continue;
            }
            ++motion_passes;

            stage = "FOREGROUND_LEFT";
            last_stage = stage;
            const bool fg_ok0 = foreground0.compute(prep0, smooth0, motion0, "0", false);

            stage = "FOREGROUND_RIGHT";
            last_stage = stage;
            const bool fg_ok1 = foreground1.compute(prep1, smooth1, motion1, "1", false);
            if (!(fg_ok0 && fg_ok1))
            {
                last_stage = "FOREGROUND_REJECT";
                pump_diagnostic_viewer(diagnostic_viewer, image0, image1, last_stage, NULL, NULL, NULL);
                continue;
            }
            ++foreground_passes;

            stage = "HAND_LEFT";
            last_stage = stage;
            const bool hand_ok0 = hand0.compute(foreground0, motion0, "0");

            stage = "HAND_RIGHT";
            last_stage = stage;
            const bool hand_ok1 = hand1.compute(foreground1, motion1, "1");
            if (!(hand_ok0 && hand_ok1))
            {
                last_stage = "HAND_REJECT";
                pump_diagnostic_viewer(diagnostic_viewer, image0, image1, last_stage, NULL, NULL, NULL);
                continue;
            }
            ++hand_passes;

            stage = "MONO_LEFT";
            last_stage = stage;
            const bool mono_ok0 = mono0.compute(hand0, "0", false);

            stage = "MONO_RIGHT";
            last_stage = stage;
            const bool mono_ok1 = mono1.compute(hand1, "1", false);
            if (!(mono_ok0 && mono_ok1))
            {
                last_stage = "MONO_REJECT";
                pump_diagnostic_viewer(diagnostic_viewer, image0, image1, last_stage, NULL, NULL, NULL);
                continue;
            }
            ++mono_passes;
            last_stage = "MONO_PASS";

            if (diagnostic_viewer)
            {
                const bool coarse_pair = valid_small_point(mono0.pt_index) && valid_small_point(mono1.pt_index);
                if (coarse_pair)
                    ++refinement_attempts;

                stage = "REFINE_INDEX_RAW_EYES";
                last_stage = stage;
                recovery_hand_resolver.compute(mono0, mono1, motion0, motion1, image0, image1);
                if (recovery_hand_resolver.pair_valid)
                    ++refinement_pairs;

                last_stage = "MONO_PASS";
            }

            if ((frame_num % 15) == 0 && !mono0.points_unwrapped_result.empty())
            {
                stage = "POSE";
                last_stage = stage;
                pose_estimator.compute(mono0.points_unwrapped_result);
                last_stage = "MONO_PASS";
            }

            pump_diagnostic_viewer(
                diagnostic_viewer,
                image0,
                image1,
                last_stage,
                &mono0,
                &mono1,
                diagnostic_viewer ? &recovery_hand_resolver : NULL);

            if ((frame_num % 10) == 0)
            {
                cout << "[RACTIV_RECOVERY] frame=" << frame_num
                     << " stage=MONO_PASS"
                     << " left_blobs=" << hand0.primary_hand_blobs.size()
                     << " right_blobs=" << hand1.primary_hand_blobs.size()
                     << " left_points=" << mono0.points_unwrapped_result.size()
                     << " right_points=" << mono1.points_unwrapped_result.size()
                     << " left_palm=" << point_text(mono0.pt_palm)
                     << " left_index_coarse=" << point_text(mono0.pt_index)
                     << " left_index_refined=" << point2f_text(recovery_hand_resolver.pt_precise_index0)
                     << " left_refine_shift_px=" << recovery_hand_resolver.shift_index0_px
                     << " left_thumb=" << point_text(mono0.pt_thumb)
                     << " right_index_coarse=" << point_text(mono1.pt_index)
                     << " right_index_refined=" << point2f_text(recovery_hand_resolver.pt_precise_index1)
                     << " right_refine_shift_px=" << recovery_hand_resolver.shift_index1_px
                     << " refined_pair=" << (recovery_hand_resolver.pair_valid ? 1 : 0)
                     << " pose=" << (pose_name.empty() ? "<none>" : pose_name)
                     << " output=LOG_ONLY_2D" << endl;
            }
        }
        catch (const cv::Exception& e)
        {
            ++opencv_exceptions;
            last_stage = "EXCEPTION_" + string(stage);
            cerr << "[RACTIV_RECOVERY] WARN: OpenCV exception"
                 << " frame=" << frame_num
                 << " stage=" << stage
                 << " code=" << e.code
                 << " func=" << e.func
                 << " err=" << e.err
                 << " action=DROP_FRAME_CONTINUE"
                 << endl;
            continue;
        }
    }

    if (diagnostic_viewer)
        destroyWindow(kDiagnosticWindow);

    cout << "[RACTIV_RECOVERY] stopped safely; OS input was never enabled" << endl;
    return 0;
}
