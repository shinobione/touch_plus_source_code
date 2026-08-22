// Touch+ Ractiv Recovery - minimal LOG_ONLY entrypoint
//
// Recovery-only wrapper around the July 2015 integrated vision lineage.
// Historical sources remain unchanged/auditable.
//
// Boundary: Camera -> MotionProcessorNew -> ForegroundExtractorNew ->
// HandSplitterNew -> MonoProcessorNew -> PoseEstimator telemetry.
//
// NO PointerMapper, NO win_cursor_plus, NO UDP, NO OS input injection.

#include <Windows.h>
#include <opencv2/opencv.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
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

using namespace cv;
using namespace std;

namespace
{
    mutex g_frame_mutex;
    Mat g_latest_frame;
    atomic<bool> g_frame_ready(false);
    atomic<bool> g_running(true);
    atomic<unsigned long long> g_callback_frames(0);

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

int main()
{
    SetConsoleCtrlHandler(console_handler, TRUE);
    init_paths_recovery();

    cout << "============================================================" << endl;
    cout << " Touch+ Ractiv Recovery - R0/R1 LOG_ONLY" << endl;
    cout << " historical algorithm lineage: July 2015 integrated core" << endl;
    cout << " OS_INJECTION=DISABLED" << endl;
    cout << " POINTER_MAPPER=DISABLED" << endl;
    cout << " UDP_CURSOR_OUTPUT=DISABLED" << endl;
    cout << "============================================================" << endl;

    MotionProcessorNew motion0;
    MotionProcessorNew motion1;
    ForegroundExtractorNew foreground0;
    ForegroundExtractorNew foreground1;
    HandSplitterNew hand0;
    HandSplitterNew hand1;
    MonoProcessorNew mono0;
    MonoProcessorNew mono1;
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
                continue;
            }
            ++mono_passes;
            last_stage = "MONO_PASS";

            if ((frame_num % 15) == 0 && !mono0.points_unwrapped_result.empty())
            {
                stage = "POSE";
                last_stage = stage;
                pose_estimator.compute(mono0.points_unwrapped_result);
                last_stage = "MONO_PASS";
            }

            if ((frame_num % 10) == 0)
            {
                cout << "[RACTIV_RECOVERY] frame=" << frame_num
                     << " stage=MONO_PASS"
                     << " left_blobs=" << hand0.primary_hand_blobs.size()
                     << " right_blobs=" << hand1.primary_hand_blobs.size()
                     << " left_points=" << mono0.points_unwrapped_result.size()
                     << " right_points=" << mono1.points_unwrapped_result.size()
                     << " left_palm=" << point_text(mono0.pt_palm)
                     << " left_index=" << point_text(mono0.pt_index)
                     << " left_thumb=" << point_text(mono0.pt_thumb)
                     << " right_index=" << point_text(mono1.pt_index)
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

    cout << "[RACTIV_RECOVERY] stopped safely; OS input was never enabled" << endl;
    return 0;
}
