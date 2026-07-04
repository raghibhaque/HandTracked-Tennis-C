#include "hand_tracker.h"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

struct HandTracker {
    cv::VideoCapture capture;
    cv::Mat frame;
    cv::Mat hsv;
    cv::Mat mask;
    cv::Mat eroded;
    cv::Mat dilated;
    int frame_width = 0;
    int frame_height = 0;
};

static constexpr int SKIN_H_MIN = 0;
static constexpr int SKIN_H_MAX = 20;
static constexpr int SKIN_S_MIN = 10;
static constexpr int SKIN_S_MAX = 255;
static constexpr int SKIN_V_MIN = 60;
static constexpr int SKIN_V_MAX = 255;

static bool open_camera_index(HandTracker *tracker, int camera_index) {
    if (!tracker->capture.open(camera_index, cv::CAP_ANY)) {
        return false;
    }

    tracker->capture.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    tracker->capture.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    tracker->capture.set(cv::CAP_PROP_FPS, 30);

    for (int attempt = 0; attempt < 50; ++attempt) {
        tracker->capture.read(tracker->frame);
        if (!tracker->frame.empty()) {
            tracker->frame_width = tracker->frame.cols;
            tracker->frame_height = tracker->frame.rows;
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    tracker->capture.release();
    return false;
}

HandTracker* hand_tracker_init(void) {
    HandTracker *tracker = new HandTracker();

    const char *camera_index_env = std::getenv("HAND_TENNIS_CAMERA_INDEX");
    if (camera_index_env && *camera_index_env) {
        int camera_index = std::atoi(camera_index_env);
        if (open_camera_index(tracker, camera_index)) {
            std::printf("Using camera index %d (HAND_TENNIS_CAMERA_INDEX)\n", camera_index);
        }
    }

    if (!tracker->capture.isOpened()) {
        for (int camera_index = 0; camera_index < 10; ++camera_index) {
            if (open_camera_index(tracker, camera_index)) {
                std::printf("Using camera index %d\n", camera_index);
                break;
            }
        }
    }

    if (!tracker->capture.isOpened()) {
        std::fprintf(stderr, "Error: Could not open any camera index 0-9\n");
        delete tracker;
        return nullptr;
    }

    std::printf("Hand tracker initialized: %dx%d\n", tracker->frame_width, tracker->frame_height);
    return tracker;
}

bool hand_tracker_detect(HandTracker *tracker, float *x, float *y) {
    if (!tracker || !tracker->capture.isOpened()) return false;

    if (!tracker->capture.read(tracker->frame) || tracker->frame.empty()) {
        return false;
    }

    cv::cvtColor(tracker->frame, tracker->hsv, cv::COLOR_BGR2HSV);

    const cv::Scalar lower(SKIN_H_MIN, SKIN_S_MIN, SKIN_V_MIN);
    const cv::Scalar upper(SKIN_H_MAX, SKIN_S_MAX, SKIN_V_MAX);
    cv::inRange(tracker->hsv, lower, upper, tracker->mask);

    cv::erode(tracker->mask, tracker->eroded, cv::Mat(), cv::Point(-1, -1), 2);
    cv::dilate(tracker->eroded, tracker->dilated, cv::Mat(), cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(tracker->dilated, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    bool detected = false;
    double max_area = 500.0;
    std::vector<cv::Point> largest_contour;

    for (const auto &contour : contours) {
        double area = cv::contourArea(contour);
        if (area > max_area) {
            max_area = area;
            largest_contour = contour;
            detected = true;
        }
    }

    if (detected) {
        cv::Rect rect = cv::boundingRect(largest_contour);
        *x = (rect.x + rect.width / 2.0f) / tracker->frame_width * 100.0f;
        *y = (rect.y + rect.height / 2.0f) / tracker->frame_height * 100.0f;
    }

    return detected;
}

void hand_tracker_cleanup(HandTracker *tracker) {
    if (!tracker) return;

    if (tracker->capture.isOpened()) {
        tracker->capture.release();
    }

    delete tracker;
}