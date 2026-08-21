// Touch+ Ractiv Recovery - minimal LOG_ONLY entrypoint
//
// This file is recovery-only. It intentionally lives outside the historical
// 2015 source tree so that master remains an auditable snapshot.
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

    void on_frame(Mat& image_in)
    {
        if (image_in.empty())
            return;

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

        // The historical processing code branches on this global.
        mode = "surface";

        // Recovery is console/log only. No OpenCV windows are opened here.
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

    // Intentionally leaked at process exit: the historical Camera destructor
    // frees legacy buffers incorrectly. Windows will reclaim process memory and
    // this avoids changing the preserved Camera implementation during R0/R1.
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

    // Keep the recovered historical sensor initializer, but deliberately skip
    // CameraInitializerNew::adjust_exposure() in this first recovery runtime.
    // The original adjustment routine contains fragile legacy image indexing;
    // the fixed 15 ms + preset1 state is already sufficient for hardware smoke.
    CameraInitializerNew::init(camera);
    cout << "[RACTIV_RECOVERY] sensor initializer applied: AE/AWB off, LEDs on, exposure=15ms, preset1" << endl;

    pose_estimator.init();

    bool normalized_preprocess = true;
    unsigned long long frame_num = 0;
    unsigned long long stereo_shape_failures = 0;
    unsigned long long motion_passes = 0;
    unsigned long long foreground_passes = 0;
    unsigned long long hand_passes = 0;
    unsigned long long mono_passes = 0;

    auto report_start = chrono::steady_clock::now();
    unsigned long long report_frames = 0;

    while (g_running.load())
    {
        Mat frame;
        {
            lock_guard<mutex> lock(g_frame_mutex);
            if (!g_frame_ready.load(memory_order_acquire))
            {
                // no new frame since last iteration
            }
            else
            {
                g_latest_frame.copyTo(frame);
                g_frame_ready.store(false, memory_order_release);
            }
        }

        if (frame.empty())
        {
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }

        ++frame_num;
        ++report_frames;

        if (frame.cols != 1280 || frame.rows != 480)
        {
            ++stereo_shape_failures;
            if ((stereo_shape_failures % 30) == 1)
                cerr << "[RACTIV_RECOVERY] WARN: unexpected frame shape " << frame.cols << "x" << frame.rows << endl;
            continue;
        }

        Mat flipped;
        flip(frame, flipped, 0); // exact historical vertical flip

        Mat image0 = flipped(Rect(0, 0, 640, 480));
        Mat image1 = flipped(Rect(640, 0, 640, 480));

        Mat small0;
        Mat small1;
        resize(image0, small0, Size(160, 120), 0, 0, INTER_LINEAR);
        resize(image1, small1, Size(160, 120), 0, 0, INTER_LINEAR);

        Mat prep0;
        Mat prep1;
        compute_channel_diff_image(small0, prep0, normalized_preprocess, "recovery0");
        compute_channel_diff_image(small1, prep1, normalized_preprocess, "recovery1");

        Mat smooth0;
        Mat smooth1;
        GaussianBlur(prep0, smooth0, Size(1, 19), 0, 0);
        GaussianBlur(prep1, smooth1, Size(1, 19), 0, 0);

        const bool motion_ok0 = motion0.compute(smooth0, "0", false);
        const bool motion_ok1 = motion1.compute(smooth1, "1", false);
        if (!(motion_ok0 && motion_ok1))
            continue;
        ++motion_passes;

        const bool fg_ok0 = foreground0.compute(prep0, smooth0, motion0, "0", false);
        const bool fg_ok1 = foreground1.compute(prep1, smooth1, motion1, "1", false);
        if (!(fg_ok0 && fg_ok1))
            continue;
        ++foreground_passes;

        const bool hand_ok0 = hand0.compute(foreground0, motion0, "0");
        const bool hand_ok1 = hand1.compute(foreground1, motion1, "1");
        if (!(hand_ok0 && hand_ok1))
            continue;
        ++hand_passes;

        const bool mono_ok0 = mono0.compute(hand0, "0", false);
        const bool mono_ok1 = mono1.compute(hand1, "1", false);
        if (!(mono_ok0 && mono_ok1))
            continue;
        ++mono_passes;

        // Original code estimated pose asynchronously every ~500 ms. For a
        // diagnostic runtime, sampling every 15 processed frames is sufficient
        // and avoids adding another legacy lifetime/thread hazard.
        if ((frame_num % 15) == 0 && !mono0.points_unwrapped_result.empty())
            pose_estimator.compute(mono0.points_unwrapped_result);

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

        const auto now = chrono::steady_clock::now();
        const double report_seconds = chrono::duration<double>(now - report_start).count();
        if (report_seconds >= 2.0)
        {
            const double fps = report_frames / report_seconds;
            cout << "[RACTIV_RECOVERY] heartbeat"
                 << " source_fps=" << fps
                 << " frames=" << frame_num
                 << " motion_pass=" << motion_passes
                 << " foreground_pass=" << foreground_passes
                 << " hand_pass=" << hand_passes
                 << " mono_pass=" << mono_passes
                 << " OS_INJECTION=DISABLED" << endl;
            report_start = now;
            report_frames = 0;
        }
    }

    cout << "[RACTIV_RECOVERY] stopped safely; OS input was never enabled" << endl;
    return 0;
}
